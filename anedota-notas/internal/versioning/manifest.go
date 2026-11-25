package versioning

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
)

const (
	anedotaDirName   = ".anedota"
	manifestFileName = "notas_commits.json"
)

// commitManifest é uma camada temporária armazenada em .anedota/referencias/notas_commits.json.
//
// TODO(anedota): Quando o ANEDOTA implementar nativamente na API C:
//   - objetos Tree (ex: anedota_tree_write / anedota_tree_read)
//   - objetos Commit (ex: anedota_commit_create / anedota_commit_read)
//   - histórico e referências (ex: anedota_log / HEAD / branches)
//   - restauração (ex: anedota_checkout / anedota_restore)
// esta estrutura temporária pode ser substituída diretamente pelas chamadas C
// mantendo exatamente a mesma interface Repository para o restante da aplicação.
type commitManifest struct {
	HeadCommitID string   `json:"headCommitId,omitempty"`
	Commits      []Commit `json:"commits"`
}

// AnedotaRepository implementa Repository usando BlobDriver para o armazenamento
// de objetos .anedota/objetos/<sha256> e um manifesto temporário para commits/restauração.
type AnedotaRepository struct {
	mu           sync.Mutex
	workspaceDir string
	anedotaRoot  string
	manifestPath string
	driver       BlobDriver
}

// NewRepositoryWithDriver permite injetar um BlobDriver customizado (útil para testes).
func NewRepositoryWithDriver(workspaceDir string, driver BlobDriver) (*AnedotaRepository, error) {
	if strings.TrimSpace(workspaceDir) == "" {
		return nil, errors.New("diretório de trabalho inválido")
	}
	absDir, err := filepath.Abs(workspaceDir)
	if err != nil {
		return nil, err
	}
	anedotaRoot := filepath.Join(absDir, anedotaDirName)
	return &AnedotaRepository{
		workspaceDir: absDir,
		anedotaRoot:  anedotaRoot,
		manifestPath: filepath.Join(anedotaRoot, "referencias", manifestFileName),
		driver:       driver,
	}, nil
}

func (r *AnedotaRepository) DriverName() string {
	return r.driver.Name()
}

// Init inicializa a estrutura .anedota/ no diretório de notas via API do ANEDOTA.
func (r *AnedotaRepository) Init() error {
	r.mu.Lock()
	defer r.mu.Unlock()

	if err := os.MkdirAll(r.workspaceDir, 0o755); err != nil {
		return fmt.Errorf("erro ao preparar diretório de notas: %w", err)
	}
	return r.driver.Init(r.anedotaRoot)
}

// Commit grava todos os ficheiros de notas como Blobs no ANEDOTA e registra um snapshot completo.
//
// TODO(anedota): Substituir a montagem do commitManifest pela criação de objeto Tree e objeto Commit
// da API C do ANEDOTA assim que estiverem disponíveis.
func (r *AnedotaRepository) Commit(message string) (Commit, error) {
	msg := strings.TrimSpace(message)
	if msg == "" {
		return Commit{}, ErrEmptyCommitMessage
	}

	r.mu.Lock()
	defer r.mu.Unlock()

	if err := r.ensureInitializedLocked(); err != nil {
		return Commit{}, err
	}

	manifest, err := r.loadManifestLocked()
	if err != nil {
		return Commit{}, err
	}

	folders, relFiles, err := r.scanWorkspaceLocked()
	if err != nil {
		return Commit{}, err
	}

	entries := make([]FileEntry, 0, len(relFiles))
	for _, relPath := range relFiles {
		absPath := filepath.Join(r.workspaceDir, filepath.FromSlash(relPath))
		info, err := os.Stat(absPath)
		if err != nil {
			return Commit{}, fmt.Errorf("erro ao inspecionar %q: %w", relPath, err)
		}

		// Usa a API real do ANEDOTA (anedota_blob_write_file quando compilado com -tags anedota).
		blobHash, err := r.driver.WriteBlobFile(r.anedotaRoot, absPath)
		if err != nil {
			return Commit{}, fmt.Errorf("erro ao gravar blob para %q: %w", relPath, err)
		}

		entries = append(entries, FileEntry{
			Path:     relPath,
			BlobHash: blobHash,
			Size:     info.Size(),
		})
	}

	now := time.Now().UTC()
	commit := Commit{
		ParentID:  manifest.HeadCommitID,
		Message:   msg,
		CreatedAt: now,
		Folders:   folders,
		Files:     entries,
	}
	commit.ID = computeCommitID(commit)

	manifest.Commits = append(manifest.Commits, commit)
	manifest.HeadCommitID = commit.ID

	if err := r.saveManifestLocked(manifest); err != nil {
		return Commit{}, err
	}

	return commit, nil
}

// ListCommits retorna o histórico de commits do mais recente para o mais antigo.
//
// TODO(anedota): Substituir pela consulta de histórico nativa da API C do ANEDOTA.
func (r *AnedotaRepository) ListCommits() ([]Commit, error) {
	r.mu.Lock()
	defer r.mu.Unlock()

	manifest, err := r.loadManifestLocked()
	if err != nil {
		return nil, err
	}

	result := make([]Commit, len(manifest.Commits))
	for i := range manifest.Commits {
		result[i] = manifest.Commits[len(manifest.Commits)-1-i]
	}
	return result, nil
}

// GetCommit busca um commit pelo hash completo ou prefixo único.
func (r *AnedotaRepository) GetCommit(id string) (Commit, error) {
	r.mu.Lock()
	defer r.mu.Unlock()

	manifest, err := r.loadManifestLocked()
	if err != nil {
		return Commit{}, err
	}

	commit, ok := findCommit(manifest.Commits, id)
	if !ok {
		return Commit{}, ErrCommitNotFound
	}
	return commit, nil
}

// Diff compara dois commits no modo Go puro (fallback).
func (r *AnedotaRepository) Diff(fromID, toID string) ([]Change, error) {
	r.mu.Lock()
	defer r.mu.Unlock()

	if err := r.ensureInitializedLocked(); err != nil {
		return nil, err
	}

	manifest, err := r.loadManifestLocked()
	if err != nil {
		return nil, err
	}

	fromCommit, ok := findCommit(manifest.Commits, fromID)
	if !ok {
		return nil, ErrCommitNotFound
	}
	toCommit, ok := findCommit(manifest.Commits, toID)
	if !ok {
		return nil, ErrCommitNotFound
	}

	fromFiles := make(map[string]string, len(fromCommit.Files))
	for _, f := range fromCommit.Files {
		fromFiles[f.Path] = f.BlobHash
	}
	toFiles := make(map[string]string, len(toCommit.Files))
	for _, f := range toCommit.Files {
		toFiles[f.Path] = f.BlobHash
	}

	changes := []Change{}
	for path, toHash := range toFiles {
		fromHash, existed := fromFiles[path]
		if !existed {
			changes = append(changes, Change{Path: path, Kind: "added"})
		} else if fromHash != toHash {
			changes = append(changes, Change{Path: path, Kind: "modified"})
		}
	}
	for path := range fromFiles {
		if _, exists := toFiles[path]; !exists {
			changes = append(changes, Change{Path: path, Kind: "deleted"})
		}
	}

	sort.Slice(changes, func(i, j int) bool {
		if changes[i].Path == changes[j].Path {
			return changes[i].Kind < changes[j].Kind
		}
		return changes[i].Path < changes[j].Path
	})
	return changes, nil
}

// Restore restaura o diretório de notas exatamente para o snapshot salvo no commit especificado.
// Todos os Blobs são lidos e validados via ANEDOTA (anedota_blob_read) antes de modificar o disco.
//
// TODO(anedota): Substituir pela função nativa de restauração/checkout da API C do ANEDOTA.
func (r *AnedotaRepository) Restore(commitID string) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	if err := r.ensureInitializedLocked(); err != nil {
		return err
	}

	manifest, err := r.loadManifestLocked()
	if err != nil {
		return err
	}

	target, ok := findCommit(manifest.Commits, commitID)
	if !ok {
		return ErrCommitNotFound
	}

	// 1. Lê e valida todos os Blobs antes de alterar o diretório de trabalho.
	contents := make(map[string][]byte, len(target.Files))
	for _, f := range target.Files {
		data, err := r.driver.ReadBlob(r.anedotaRoot, f.BlobHash)
		if err != nil {
			return fmt.Errorf("falha ao ler blob %s (%s): %w", f.BlobHash, f.Path, err)
		}
		contents[f.Path] = data
	}

	// 2. Limpa ficheiros e pastas do workspace preservando estritamente .anedota/.
	dirEntries, err := os.ReadDir(r.workspaceDir)
	if err != nil {
		return fmt.Errorf("erro ao listar workspace: %w", err)
	}
	for _, entry := range dirEntries {
		if entry.Name() == anedotaDirName {
			continue
		}
		targetPath := filepath.Join(r.workspaceDir, entry.Name())
		if err := os.RemoveAll(targetPath); err != nil {
			return fmt.Errorf("erro ao limpar %q: %w", entry.Name(), err)
		}
	}

	// 3. Recria todas as pastas registradas no snapshot (incluindo pastas vazias).
	for _, folder := range target.Folders {
		absFolder := filepath.Join(r.workspaceDir, filepath.FromSlash(folder))
		if err := os.MkdirAll(absFolder, 0o755); err != nil {
			return fmt.Errorf("erro ao recriar pasta %q: %w", folder, err)
		}
	}

	// 4. Recria todos os ficheiros com os bytes restaurados dos Blobs.
	for _, f := range target.Files {
		absFile := filepath.Join(r.workspaceDir, filepath.FromSlash(f.Path))
		if err := os.MkdirAll(filepath.Dir(absFile), 0o755); err != nil {
			return fmt.Errorf("erro ao criar diretório pai de %q: %w", f.Path, err)
		}
		if err := os.WriteFile(absFile, contents[f.Path], 0o644); err != nil {
			return fmt.Errorf("erro ao restaurar ficheiro %q: %w", f.Path, err)
		}
	}

	// 5. Atualiza HEAD para o commit restaurado.
	manifest.HeadCommitID = target.ID
	return r.saveManifestLocked(manifest)
}

// Status compara o estado atual das pastas e notas em disco com o commit apontado por HEAD.
//
// TODO(anedota): Poderá utilizar a futura API de comparação/diff do ANEDOTA.
func (r *AnedotaRepository) Status() (WorkspaceStatus, error) {
	r.mu.Lock()
	defer r.mu.Unlock()

	status := WorkspaceStatus{
		Driver:  r.driver.Name(),
		Changes: []Change{},
	}

	if info, err := os.Stat(filepath.Join(r.anedotaRoot, "objetos")); err == nil && info.IsDir() {
		status.Initialized = true
	}

	manifest, err := r.loadManifestLocked()
	if err != nil {
		return status, err
	}
	status.HeadCommitID = manifest.HeadCommitID
	status.TotalCommits = len(manifest.Commits)

	curFolders, curFiles, err := r.scanWorkspaceLocked()
	if err != nil {
		return status, err
	}

	curFileHashes := make(map[string]string, len(curFiles))
	for _, relPath := range curFiles {
		absPath := filepath.Join(r.workspaceDir, filepath.FromSlash(relPath))
		hash, err := hashFileSHA256(absPath)
		if err != nil {
			return status, err
		}
		curFileHashes[relPath] = hash
	}

	curFolderSet := make(map[string]struct{}, len(curFolders))
	for _, f := range curFolders {
		curFolderSet[f] = struct{}{}
	}

	headFiles := make(map[string]string)
	headFolders := make(map[string]struct{})
	if manifest.HeadCommitID != "" {
		if head, ok := findCommit(manifest.Commits, manifest.HeadCommitID); ok {
			for _, f := range head.Files {
				headFiles[f.Path] = f.BlobHash
			}
			for _, dir := range head.Folders {
				headFolders[dir] = struct{}{}
			}
		}
	}

	for path, curHash := range curFileHashes {
		prevHash, existed := headFiles[path]
		if !existed {
			status.Changes = append(status.Changes, Change{Path: path, Kind: "added"})
		} else if prevHash != curHash {
			status.Changes = append(status.Changes, Change{Path: path, Kind: "modified"})
		}
	}
	for path := range headFiles {
		if _, exists := curFileHashes[path]; !exists {
			status.Changes = append(status.Changes, Change{Path: path, Kind: "deleted"})
		}
	}

	for dir := range curFolderSet {
		if _, existed := headFolders[dir]; !existed {
			status.Changes = append(status.Changes, Change{Path: dir, Kind: "added", IsDir: true})
		}
	}
	for dir := range headFolders {
		if _, exists := curFolderSet[dir]; !exists {
			status.Changes = append(status.Changes, Change{Path: dir, Kind: "deleted", IsDir: true})
		}
	}

	sort.Slice(status.Changes, func(i, j int) bool {
		if status.Changes[i].Path == status.Changes[j].Path {
			return status.Changes[i].Kind < status.Changes[j].Kind
		}
		return status.Changes[i].Path < status.Changes[j].Path
	})

	status.Dirty = len(status.Changes) > 0
	return status, nil
}

func (r *AnedotaRepository) ensureInitializedLocked() error {
	if info, err := os.Stat(filepath.Join(r.anedotaRoot, "objetos")); err == nil && info.IsDir() {
		return nil
	}
	return r.driver.Init(r.anedotaRoot)
}

func (r *AnedotaRepository) loadManifestLocked() (commitManifest, error) {
	data, err := os.ReadFile(r.manifestPath)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return commitManifest{Commits: []Commit{}}, nil
		}
		return commitManifest{}, fmt.Errorf("erro ao ler manifesto de commits: %w", err)
	}

	var m commitManifest
	if err := json.Unmarshal(data, &m); err != nil {
		return commitManifest{}, fmt.Errorf("manifesto de commits corrompido: %w", err)
	}
	if m.Commits == nil {
		m.Commits = []Commit{}
	}
	return m, nil
}

func (r *AnedotaRepository) saveManifestLocked(m commitManifest) error {
	if err := os.MkdirAll(filepath.Dir(r.manifestPath), 0o755); err != nil {
		return err
	}
	data, err := json.MarshalIndent(m, "", "  ")
	if err != nil {
		return err
	}

	tmp, err := os.CreateTemp(filepath.Dir(r.manifestPath), ".manifest-*")
	if err != nil {
		return err
	}
	tmpName := tmp.Name()
	if _, err := tmp.Write(data); err != nil {
		tmp.Close()
		os.Remove(tmpName)
		return err
	}
	if err := tmp.Close(); err != nil {
		os.Remove(tmpName)
		return err
	}
	return os.Rename(tmpName, r.manifestPath)
}

func (r *AnedotaRepository) scanWorkspaceLocked() (folders []string, files []string, err error) {
	folders = []string{}
	files = []string{}

	if _, statErr := os.Stat(r.workspaceDir); errors.Is(statErr, os.ErrNotExist) {
		return folders, files, nil
	}

	err = filepath.WalkDir(r.workspaceDir, func(path string, d fs.DirEntry, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		if path == r.workspaceDir {
			return nil
		}

		name := d.Name()
		if name == anedotaDirName {
			if d.IsDir() {
				return filepath.SkipDir
			}
			return nil
		}
		if name == ".DS_Store" || strings.HasPrefix(name, ".") {
			if d.IsDir() {
				return filepath.SkipDir
			}
			return nil
		}

		rel, err := filepath.Rel(r.workspaceDir, path)
		if err != nil {
			return err
		}
		relSlash := filepath.ToSlash(rel)

		if d.IsDir() {
			folders = append(folders, relSlash)
		} else {
			files = append(files, relSlash)
		}
		return nil
	})
	if err != nil {
		return nil, nil, err
	}

	sort.Strings(folders)
	sort.Strings(files)
	return folders, files, nil
}

func findCommit(commits []Commit, id string) (Commit, bool) {
	id = strings.TrimSpace(id)
	if id == "" {
		return Commit{}, false
	}

	var matched Commit
	matches := 0
	for _, c := range commits {
		if c.ID == id {
			return c, true
		}
		if strings.HasPrefix(c.ID, id) {
			matched = c
			matches++
		}
	}
	if matches == 1 {
		return matched, true
	}
	return Commit{}, false
}

func computeCommitID(c Commit) string {
	h := sha256.New()
	fmt.Fprintf(h, "parent:%s\n", c.ParentID)
	fmt.Fprintf(h, "time:%d\n", c.CreatedAt.UnixNano())
	fmt.Fprintf(h, "message:%s\n", c.Message)
	for _, dir := range c.Folders {
		fmt.Fprintf(h, "dir:%s\n", dir)
	}
	for _, f := range c.Files {
		fmt.Fprintf(h, "file:%s:%s:%d\n", f.Path, f.BlobHash, f.Size)
	}
	return hex.EncodeToString(h.Sum(nil))
}

func hashFileSHA256(path string) (string, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	sum := sha256.Sum256(data)
	return hex.EncodeToString(sum[:]), nil
}

func isValidSHA256Hex(hash string) bool {
	if len(hash) != 64 {
		return false
	}
	for i := 0; i < len(hash); i++ {
		c := hash[i]
		if !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
			return false
		}
	}
	return true
}
