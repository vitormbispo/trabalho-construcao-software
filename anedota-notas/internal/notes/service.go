package notes

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"anedota-notas/internal/versioning"
)

var (
	ErrInvalidPath     = errors.New("caminho inválido")
	ErrNoteNotFound    = errors.New("nota não encontrada")
	ErrFolderNotFound  = errors.New("pasta não encontrada")
	ErrAlreadyExists   = errors.New("já existe um ficheiro ou pasta com esse nome")
)

// TreeNode representa uma pasta ou página de anotação na árvore do workspace.
type TreeNode struct {
	Name     string      `json:"name"`
	Path     string      `json:"path"`
	Type     string      `json:"type"` // "folder" ou "note"
	Size     int64       `json:"size,omitempty"`
	ModTime  time.Time   `json:"modTime,omitempty"`
	Children []*TreeNode `json:"children,omitempty"`
}

// WorkspaceTree contém a árvore hierárquica e a lista plana de pastas existentes.
type WorkspaceTree struct {
	Nodes   []*TreeNode `json:"nodes"`
	Folders []string    `json:"folders"`
}

// Note representa o conteúdo e metadados de uma página de anotação.
type Note struct {
	Path    string    `json:"path"`
	Name    string    `json:"name"`
	Content string    `json:"content"`
	Size    int64     `json:"size"`
	ModTime time.Time `json:"modTime"`
}

// Service encapsula as operações de organização/edição de notas e o versionamento via ANEDOTA.
type Service struct {
	rootDir string
	repo    versioning.Repository
}

// NewService inicializa o diretório de notas e o repositório .anedota associado.
func NewService(rootDir string, repo versioning.Repository) (*Service, error) {
	if strings.TrimSpace(rootDir) == "" {
		return nil, errors.New("diretório raiz de notas não informado")
	}
	absRoot, err := filepath.Abs(rootDir)
	if err != nil {
		return nil, err
	}
	if err := os.MkdirAll(absRoot, 0o755); err != nil {
		return nil, fmt.Errorf("erro ao criar diretório de notas: %w", err)
	}
	if repo == nil {
		r, err := versioning.NewRepository(absRoot)
		if err != nil {
			return nil, err
		}
		repo = r
	}
	if err := repo.Init(); err != nil {
		return nil, fmt.Errorf("erro ao inicializar repositório ANEDOTA: %w", err)
	}
	return &Service{
		rootDir: absRoot,
		repo:    repo,
	}, nil
}

func (s *Service) RootDir() string {
	return s.rootDir
}

// ListTree constrói a árvore de pastas e ficheiros de anotação.
func (s *Service) ListTree() (WorkspaceTree, error) {
	folders := []string{}
	nodes, err := s.readDirTree("", &folders)
	if err != nil {
		return WorkspaceTree{}, err
	}
	sort.Strings(folders)
	return WorkspaceTree{
		Nodes:   nodes,
		Folders: folders,
	}, nil
}

func (s *Service) readDirTree(relDir string, folders *[]string) ([]*TreeNode, error) {
	absDir := s.rootDir
	if relDir != "" {
		absDir = filepath.Join(s.rootDir, filepath.FromSlash(relDir))
	}

	entries, err := os.ReadDir(absDir)
	if err != nil {
		return nil, err
	}

	var result []*TreeNode
	for _, entry := range entries {
		name := entry.Name()
		if name == ".anedota" || strings.HasPrefix(name, ".") {
			continue
		}

		childRel := name
		if relDir != "" {
			childRel = relDir + "/" + name
		}

		info, err := entry.Info()
		if err != nil {
			return nil, err
		}

		if entry.IsDir() {
			*folders = append(*folders, childRel)
			children, err := s.readDirTree(childRel, folders)
			if err != nil {
				return nil, err
			}
			result = append(result, &TreeNode{
				Name:     name,
				Path:     childRel,
				Type:     "folder",
				ModTime:  info.ModTime().UTC(),
				Children: children,
			})
		} else {
			result = append(result, &TreeNode{
				Name:    name,
				Path:    childRel,
				Type:    "note",
				Size:    info.Size(),
				ModTime: info.ModTime().UTC(),
			})
		}
	}

	sort.Slice(result, func(i, j int) bool {
		if result[i].Type != result[j].Type {
			return result[i].Type == "folder"
		}
		return strings.ToLower(result[i].Name) < strings.ToLower(result[j].Name)
	})

	if result == nil {
		result = []*TreeNode{}
	}
	return result, nil
}

// CreateFolder cria uma pasta (inclusive aninhada) para organizar notas.
func (s *Service) CreateFolder(relPath string) (string, error) {
	clean, abs, err := s.resolveSafePath(relPath, false)
	if err != nil {
		return "", err
	}
	if info, err := os.Stat(abs); err == nil {
		if !info.IsDir() {
			return "", ErrAlreadyExists
		}
		return clean, nil
	}
	if err := os.MkdirAll(abs, 0o755); err != nil {
		return "", fmt.Errorf("erro ao criar pasta: %w", err)
	}
	return clean, nil
}

// DeleteFolder remove uma pasta e todas as notas dentro dela.
func (s *Service) DeleteFolder(relPath string) error {
	_, abs, err := s.resolveSafePath(relPath, false)
	if err != nil {
		return err
	}
	info, err := os.Stat(abs)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return ErrFolderNotFound
		}
		return err
	}
	if !info.IsDir() {
		return ErrFolderNotFound
	}
	return os.RemoveAll(abs)
}

// ReadNote lê o conteúdo de uma página de anotação existente.
func (s *Service) ReadNote(relPath string) (Note, error) {
	clean, abs, err := s.resolveSafePath(relPath, false)
	if err != nil {
		return Note{}, err
	}
	info, err := os.Stat(abs)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return Note{}, ErrNoteNotFound
		}
		return Note{}, err
	}
	if info.IsDir() {
		return Note{}, ErrNoteNotFound
	}
	data, err := os.ReadFile(abs)
	if err != nil {
		return Note{}, err
	}
	return Note{
		Path:    clean,
		Name:    filepath.Base(clean),
		Content: string(data),
		Size:    info.Size(),
		ModTime: info.ModTime().UTC(),
	}, nil
}

// SaveNote cria ou atualiza uma página de anotação em qualquer pasta/ficheiro.
// Se o caminho não tiver extensão, adiciona ".md" automaticamente.
func (s *Service) SaveNote(relPath, content string) (Note, error) {
	clean, abs, err := s.resolveSafePath(relPath, true)
	if err != nil {
		return Note{}, err
	}
	if info, err := os.Stat(abs); err == nil && info.IsDir() {
		return Note{}, ErrAlreadyExists
	}
	if err := os.MkdirAll(filepath.Dir(abs), 0o755); err != nil {
		return Note{}, fmt.Errorf("erro ao criar pasta da nota: %w", err)
	}
	if err := os.WriteFile(abs, []byte(content), 0o644); err != nil {
		return Note{}, fmt.Errorf("erro ao salvar nota: %w", err)
	}
	return s.ReadNote(clean)
}

// RenameNote renomeia ou move uma página de anotação para outro ficheiro/pasta.
func (s *Service) RenameNote(oldRelPath, newRelPath string) (Note, error) {
	_, oldAbs, err := s.resolveSafePath(oldRelPath, false)
	if err != nil {
		return Note{}, err
	}
	newClean, newAbs, err := s.resolveSafePath(newRelPath, true)
	if err != nil {
		return Note{}, err
	}

	info, err := os.Stat(oldAbs)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return Note{}, ErrNoteNotFound
		}
		return Note{}, err
	}
	if info.IsDir() {
		return Note{}, ErrNoteNotFound
	}
	if oldAbs != newAbs {
		if _, err := os.Stat(newAbs); err == nil {
			return Note{}, ErrAlreadyExists
		}
		if err := os.MkdirAll(filepath.Dir(newAbs), 0o755); err != nil {
			return Note{}, err
		}
		if err := os.Rename(oldAbs, newAbs); err != nil {
			return Note{}, err
		}
	}
	return s.ReadNote(newClean)
}

// DeleteNote exclui uma página de anotação.
func (s *Service) DeleteNote(relPath string) error {
	_, abs, err := s.resolveSafePath(relPath, false)
	if err != nil {
		return err
	}
	info, err := os.Stat(abs)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return ErrNoteNotFound
		}
		return err
	}
	if info.IsDir() {
		return ErrNoteNotFound
	}
	return os.Remove(abs)
}

// --- Operações de Versionamento (delegadas à camada ANEDOTA) ---

func (s *Service) Status() (versioning.WorkspaceStatus, error) {
	return s.repo.Status()
}

func (s *Service) Commit(message string) (versioning.Commit, error) {
	return s.repo.Commit(message)
}

func (s *Service) ListCommits() ([]versioning.Commit, error) {
	return s.repo.ListCommits()
}

func (s *Service) GetCommit(id string) (versioning.Commit, error) {
	return s.repo.GetCommit(id)
}

func (s *Service) DiffCommits(fromID, toID string) ([]versioning.Change, error) {
	return s.repo.Diff(fromID, toID)
}

func (s *Service) RestoreCommit(id string) error {
	return s.repo.Restore(id)
}

// resolveSafePath valida o caminho relativo para evitar path traversal e acesso a .anedota/.
func (s *Service) resolveSafePath(raw string, defaultExt bool) (cleanSlash string, absPath string, err error) {
	trimmed := strings.TrimSpace(strings.ReplaceAll(raw, "\\", "/"))
	trimmed = strings.TrimPrefix(trimmed, "/")
	if trimmed == "" {
		return "", "", ErrInvalidPath
	}

	cleaned := filepath.ToSlash(filepath.Clean(trimmed))
	if cleaned == "." || cleaned == ".." || strings.HasPrefix(cleaned, "../") || strings.Contains(cleaned, "/../") {
		return "", "", ErrInvalidPath
	}

	parts := strings.Split(cleaned, "/")
	for _, p := range parts {
		if p == "" || p == "." || p == ".." || p == ".anedota" || strings.HasPrefix(p, ".") {
			return "", "", ErrInvalidPath
		}
	}

	if defaultExt {
		base := parts[len(parts)-1]
		if filepath.Ext(base) == "" {
			cleaned += ".md"
		}
	}

	abs := filepath.Join(s.rootDir, filepath.FromSlash(cleaned))
	relCheck, err := filepath.Rel(s.rootDir, abs)
	if err != nil || relCheck == "." || strings.HasPrefix(relCheck, "..") {
		return "", "", ErrInvalidPath
	}

	return cleaned, abs, nil
}
