//go:build anedota

package versioning

/*
#cgo CFLAGS: -I${SRCDIR}/../../../include -std=c99
#cgo LDFLAGS: ${SRCDIR}/../../../build/libanedota.a
#include <stdlib.h>
#include <anedota/anedota.h>

static const anedota_tree_entry_t *cgo_tree_entry_at(const anedota_tree_t *tree, size_t index)
{
    return &tree->entries[index];
}

static const anedota_log_entry_t *cgo_log_entry_at(const anedota_log_t *log, size_t index)
{
    return &log->entries[index];
}

static const anedota_diff_entry_t *cgo_diff_entry_at(const anedota_diff_t *diff, size_t index)
{
    return &diff->entries[index];
}

static anedota_tree_entry_t *cgo_alloc_tree_entries(size_t count)
{
    if (count == 0) return NULL;
    return (anedota_tree_entry_t *)calloc(count, sizeof(anedota_tree_entry_t));
}

static void cgo_set_tree_entry(anedota_tree_entry_t *entries, size_t index,
                               const char *name, int type, const char *hash)
{
    entries[index].name = name;
    entries[index].type = (anedota_tree_entry_type_t)type;
    entries[index].hash = hash;
}
*/
import "C"

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
	"unsafe"
)

// AnedotaCGORepository implementa Repository utilizando exclusivamente a biblioteca
// estática compilada libanedota.a (sem invocar a CLI e sem manifesto JSON temporário).
type AnedotaCGORepository struct {
	mu           sync.Mutex
	workspaceDir string
	anedotaRoot  string
	driver       *anedotaCGODriver
}

// NewRepository cria o repositório nativo vinculado diretamente à libanedota.a.
func NewRepository(workspaceDir string) (Repository, error) {
	if strings.TrimSpace(workspaceDir) == "" {
		return nil, errors.New("diretório de trabalho inválido")
	}
	absDir, err := filepath.Abs(workspaceDir)
	if err != nil {
		return nil, err
	}
	return &AnedotaCGORepository{
		workspaceDir: absDir,
		anedotaRoot:  filepath.Join(absDir, anedotaDirName),
		driver:       &anedotaCGODriver{},
	}, nil
}

func (r *AnedotaCGORepository) DriverName() string {
	return "libanedota.a (API C nativa: storage, blob, tree, commit, ref, log, diff)"
}

func (r *AnedotaCGORepository) Init() error {
	r.mu.Lock()
	defer r.mu.Unlock()

	if err := os.MkdirAll(r.workspaceDir, 0o755); err != nil {
		return fmt.Errorf("erro ao preparar diretório de notas: %w", err)
	}
	return r.driver.Init(r.anedotaRoot)
}

func (r *AnedotaCGORepository) ensureInitializedLocked() error {
	if info, err := os.Stat(filepath.Join(r.anedotaRoot, "objetos")); err == nil && info.IsDir() {
		return nil
	}
	return r.driver.Init(r.anedotaRoot)
}

func (r *AnedotaCGORepository) readHeadLocked() (string, bool, error) {
	cRoot := C.CString(r.anedotaRoot)
	defer C.free(unsafe.Pointer(cRoot))

	var hashBuf [C.ANEDOTA_SHA256_HEX_SIZE]C.char
	rc := C.anedota_ref_read_head(cRoot, &hashBuf[0])
	if rc < 0 {
		return "", false, fmt.Errorf("anedota_ref_read_head falhou (código %d)", int(rc))
	}
	if rc == 0 {
		return "", false, nil
	}
	return C.GoString(&hashBuf[0]), true, nil
}

func (r *AnedotaCGORepository) updateHeadLocked(commitHash string) error {
	cRoot := C.CString(r.anedotaRoot)
	defer C.free(unsafe.Pointer(cRoot))

	cHash := C.CString(commitHash)
	defer C.free(unsafe.Pointer(cHash))

	if rc := C.anedota_ref_update_head(cRoot, cHash); rc != 0 {
		return fmt.Errorf("anedota_ref_update_head falhou para %q (código %d)", commitHash, int(rc))
	}
	return nil
}

type nativeTreeEntryInput struct {
	name      string
	entryType C.int
	hash      string
}

func (r *AnedotaCGORepository) writeTreeEntriesLocked(entries []nativeTreeEntryInput) (string, error) {
	cRoot := C.CString(r.anedotaRoot)
	defer C.free(unsafe.Pointer(cRoot))

	var cEntries *C.anedota_tree_entry_t
	var allocs []unsafe.Pointer
	if len(entries) > 0 {
		cEntries = C.cgo_alloc_tree_entries(C.size_t(len(entries)))
		if cEntries == nil {
			return "", errors.New("falha ao alocar entradas da tree")
		}
		defer C.free(unsafe.Pointer(cEntries))

		allocs = make([]unsafe.Pointer, 0, len(entries)*2)
		for i, e := range entries {
			cName := C.CString(e.name)
			cHash := C.CString(e.hash)
			allocs = append(allocs, unsafe.Pointer(cName), unsafe.Pointer(cHash))
			C.cgo_set_tree_entry(cEntries, C.size_t(i), cName, e.entryType, cHash)
		}
		defer func() {
			for _, p := range allocs {
				C.free(p)
			}
		}()
	}

	var hashBuf [C.ANEDOTA_SHA256_HEX_SIZE]C.char
	if rc := C.anedota_tree_write(cRoot, cEntries, C.size_t(len(entries)), &hashBuf[0]); rc != 0 {
		return "", fmt.Errorf("anedota_tree_write falhou (código %d)", int(rc))
	}
	return C.GoString(&hashBuf[0]), nil
}

// writeDirectoryTreeLocked grava recursivamente o diretório de notas usando
// anedota_blob_write_file e anedota_tree_write, ignorando .anedota e dotfiles.
func (r *AnedotaCGORepository) writeDirectoryTreeLocked(absDir string) (string, error) {
	dirEntries, err := os.ReadDir(absDir)
	if err != nil {
		return "", err
	}

	inputs := make([]nativeTreeEntryInput, 0, len(dirEntries))
	for _, entry := range dirEntries {
		name := entry.Name()
		if name == anedotaDirName || name == ".DS_Store" || strings.HasPrefix(name, ".") {
			continue
		}
		childPath := filepath.Join(absDir, name)
		if entry.IsDir() {
			subTreeHash, err := r.writeDirectoryTreeLocked(childPath)
			if err != nil {
				return "", err
			}
			inputs = append(inputs, nativeTreeEntryInput{
				name:      name,
				entryType: C.ANEDOTA_TREE_ENTRY_TREE,
				hash:      subTreeHash,
			})
		} else {
			blobHash, err := r.driver.WriteBlobFile(r.anedotaRoot, childPath)
			if err != nil {
				return "", err
			}
			inputs = append(inputs, nativeTreeEntryInput{
				name:      name,
				entryType: C.ANEDOTA_TREE_ENTRY_BLOB,
				hash:      blobHash,
			})
		}
	}

	return r.writeTreeEntriesLocked(inputs)
}

func (r *AnedotaCGORepository) walkTreeLocked(treeHash, prefix string, folders *[]string, files *[]FileEntry) error {
	cRoot := C.CString(r.anedotaRoot)
	defer C.free(unsafe.Pointer(cRoot))

	cHash := C.CString(treeHash)
	defer C.free(unsafe.Pointer(cHash))

	var cTree *C.anedota_tree_t
	if rc := C.anedota_tree_read(cRoot, cHash, &cTree); rc != 0 {
		return fmt.Errorf("anedota_tree_read falhou para %q (código %d)", treeHash, int(rc))
	}
	defer C.free(unsafe.Pointer(cTree))

	count := int(cTree.count)
	for i := 0; i < count; i++ {
		entry := C.cgo_tree_entry_at(cTree, C.size_t(i))
		name := C.GoString(entry.name)
		hash := C.GoString(entry.hash)
		relPath := name
		if prefix != "" {
			relPath = prefix + "/" + name
		}

		if entry._type == C.ANEDOTA_TREE_ENTRY_TREE {
			*folders = append(*folders, relPath)
			if err := r.walkTreeLocked(hash, relPath, folders, files); err != nil {
				return err
			}
		} else {
			var size int64
			if info, err := os.Stat(filepath.Join(r.anedotaRoot, "objetos", hash)); err == nil {
				size = info.Size()
			}
			*files = append(*files, FileEntry{
				Path:     relPath,
				BlobHash: hash,
				Size:     size,
			})
		}
	}
	return nil
}

func (r *AnedotaCGORepository) readCommitLocked(commitHash string) (Commit, error) {
	cRoot := C.CString(r.anedotaRoot)
	defer C.free(unsafe.Pointer(cRoot))

	cHash := C.CString(commitHash)
	defer C.free(unsafe.Pointer(cHash))

	var cCommit *C.anedota_commit_t
	if rc := C.anedota_commit_read(cRoot, cHash, &cCommit); rc != 0 {
		return Commit{}, ErrCommitNotFound
	}
	defer C.free(unsafe.Pointer(cCommit))

	treeHash := C.GoString(cCommit.tree_hash)
	parentID := ""
	if cCommit.parent_hash != nil {
		parentID = C.GoString(cCommit.parent_hash)
	}
	tsStr := C.GoString(cCommit.timestamp)
	createdAt, _ := time.Parse(time.RFC3339Nano, tsStr)
	message := C.GoString(cCommit.message)

	folders := []string{}
	files := []FileEntry{}
	if err := r.walkTreeLocked(treeHash, "", &folders, &files); err != nil {
		return Commit{}, err
	}
	sort.Strings(folders)
	sort.Slice(files, func(i, j int) bool { return files[i].Path < files[j].Path })

	return Commit{
		ID:        commitHash,
		ParentID:  parentID,
		Message:   message,
		CreatedAt: createdAt,
		Folders:   folders,
		Files:     files,
	}, nil
}

func (r *AnedotaCGORepository) resolveCommitIDLocked(id string) (string, error) {
	id = strings.TrimSpace(id)
	if id == "" {
		return "", ErrCommitNotFound
	}
	if isValidSHA256Hex(id) {
		if _, err := r.readCommitLocked(id); err == nil {
			return id, nil
		}
		return "", ErrCommitNotFound
	}

	objectsDir := filepath.Join(r.anedotaRoot, "objetos")
	entries, err := os.ReadDir(objectsDir)
	if err != nil {
		return "", ErrCommitNotFound
	}

	var matched string
	matches := 0
	for _, entry := range entries {
		name := entry.Name()
		if !isValidSHA256Hex(name) || !strings.HasPrefix(name, id) {
			continue
		}
		if _, err := r.readCommitLocked(name); err == nil {
			matched = name
			matches++
		}
	}
	if matches == 1 {
		return matched, nil
	}
	return "", ErrCommitNotFound
}

func (r *AnedotaCGORepository) Commit(message string) (Commit, error) {
	msg := strings.TrimSpace(message)
	if msg == "" {
		return Commit{}, ErrEmptyCommitMessage
	}

	r.mu.Lock()
	defer r.mu.Unlock()

	if err := r.ensureInitializedLocked(); err != nil {
		return Commit{}, err
	}

	parentHash, hasParent, err := r.readHeadLocked()
	if err != nil {
		return Commit{}, err
	}

	treeHash, err := r.writeDirectoryTreeLocked(r.workspaceDir)
	if err != nil {
		return Commit{}, err
	}

	now := time.Now().UTC()
	ts := now.Format(time.RFC3339Nano)

	cRoot := C.CString(r.anedotaRoot)
	defer C.free(unsafe.Pointer(cRoot))
	cTree := C.CString(treeHash)
	defer C.free(unsafe.Pointer(cTree))
	cAuthor := C.CString("anedota-notas")
	defer C.free(unsafe.Pointer(cAuthor))
	cTimestamp := C.CString(ts)
	defer C.free(unsafe.Pointer(cTimestamp))
	cMessage := C.CString(msg)
	defer C.free(unsafe.Pointer(cMessage))

	var cParent *C.char
	if hasParent {
		cParent = C.CString(parentHash)
		defer C.free(unsafe.Pointer(cParent))
	}

	cCommit := C.anedota_commit_t{
		tree_hash:   cTree,
		parent_hash: cParent,
		author:      cAuthor,
		timestamp:   cTimestamp,
		message:     cMessage,
	}

	var hashBuf [C.ANEDOTA_SHA256_HEX_SIZE]C.char
	if rc := C.anedota_commit_write(cRoot, &cCommit, &hashBuf[0]); rc != 0 {
		return Commit{}, fmt.Errorf("anedota_commit_write falhou (código %d)", int(rc))
	}

	commitHash := C.GoString(&hashBuf[0])
	return r.readCommitLocked(commitHash)
}

func (r *AnedotaCGORepository) ListCommits() ([]Commit, error) {
	r.mu.Lock()
	defer r.mu.Unlock()

	if info, err := os.Stat(filepath.Join(r.anedotaRoot, "objetos")); err != nil || !info.IsDir() {
		return []Commit{}, nil
	}

	cRoot := C.CString(r.anedotaRoot)
	defer C.free(unsafe.Pointer(cRoot))

	var cLog *C.anedota_log_t
	if rc := C.anedota_log_read(cRoot, &cLog); rc != 0 {
		return nil, fmt.Errorf("anedota_log_read falhou (código %d)", int(rc))
	}
	defer C.free(unsafe.Pointer(cLog))

	count := int(cLog.count)
	commits := make([]Commit, 0, count)
	for i := 0; i < count; i++ {
		entry := C.cgo_log_entry_at(cLog, C.size_t(i))
		hash := C.GoString(&entry.hash[0])
		treeHash := C.GoString(entry.tree_hash)
		parentID := ""
		if entry.parent_hash != nil {
			parentID = C.GoString(entry.parent_hash)
		}
		createdAt, _ := time.Parse(time.RFC3339Nano, C.GoString(entry.timestamp))

		folders := []string{}
		files := []FileEntry{}
		if err := r.walkTreeLocked(treeHash, "", &folders, &files); err != nil {
			return nil, err
		}
		sort.Strings(folders)
		sort.Slice(files, func(a, b int) bool { return files[a].Path < files[b].Path })

		commits = append(commits, Commit{
			ID:        hash,
			ParentID:  parentID,
			Message:   C.GoString(entry.message),
			CreatedAt: createdAt,
			Folders:   folders,
			Files:     files,
		})
	}
	return commits, nil
}

func (r *AnedotaCGORepository) GetCommit(id string) (Commit, error) {
	r.mu.Lock()
	defer r.mu.Unlock()

	resolved, err := r.resolveCommitIDLocked(id)
	if err != nil {
		return Commit{}, err
	}
	return r.readCommitLocked(resolved)
}

func (r *AnedotaCGORepository) Diff(fromID, toID string) ([]Change, error) {
	r.mu.Lock()
	defer r.mu.Unlock()

	if err := r.ensureInitializedLocked(); err != nil {
		return nil, err
	}

	resolvedFrom, err := r.resolveCommitIDLocked(fromID)
	if err != nil {
		return nil, err
	}
	resolvedTo, err := r.resolveCommitIDLocked(toID)
	if err != nil {
		return nil, err
	}

	cRoot := C.CString(r.anedotaRoot)
	defer C.free(unsafe.Pointer(cRoot))
	cFrom := C.CString(resolvedFrom)
	defer C.free(unsafe.Pointer(cFrom))
	cTo := C.CString(resolvedTo)
	defer C.free(unsafe.Pointer(cTo))

	var cDiff *C.anedota_diff_t
	status := C.anedota_diff_versions(cRoot, cFrom, cTo, &cDiff)
	if status != C.ANEDOTA_DIFF_OK {
		if status == C.ANEDOTA_DIFF_ERR_VERSION_NOT_FOUND {
			return nil, ErrCommitNotFound
		}
		return nil, fmt.Errorf("anedota_diff_versions falhou: %s", C.GoString(C.anedota_diff_status_message(status)))
	}
	defer C.free(unsafe.Pointer(cDiff))

	count := int(cDiff.count)
	changes := make([]Change, 0, count)
	for i := 0; i < count; i++ {
		entry := C.cgo_diff_entry_at(cDiff, C.size_t(i))
		var kind string
		switch entry.kind {
		case C.ANEDOTA_DIFF_ADDED:
			kind = "added"
		case C.ANEDOTA_DIFF_REMOVED:
			kind = "deleted"
		case C.ANEDOTA_DIFF_MODIFIED:
			kind = "modified"
		}
		changes = append(changes, Change{
			Path:  C.GoString(entry.path),
			Kind:  kind,
			IsDir: entry.entry_type == C.ANEDOTA_TREE_ENTRY_TREE,
		})
	}
	return changes, nil
}

func (r *AnedotaCGORepository) Restore(commitID string) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	if err := r.ensureInitializedLocked(); err != nil {
		return err
	}

	resolved, err := r.resolveCommitIDLocked(commitID)
	if err != nil {
		return err
	}
	target, err := r.readCommitLocked(resolved)
	if err != nil {
		return err
	}

	contents := make(map[string][]byte, len(target.Files))
	for _, f := range target.Files {
		data, err := r.driver.ReadBlob(r.anedotaRoot, f.BlobHash)
		if err != nil {
			return fmt.Errorf("falha ao ler blob %s (%s): %w", f.BlobHash, f.Path, err)
		}
		contents[f.Path] = data
	}

	dirEntries, err := os.ReadDir(r.workspaceDir)
	if err != nil {
		return fmt.Errorf("erro ao listar workspace: %w", err)
	}
	for _, entry := range dirEntries {
		if entry.Name() == anedotaDirName {
			continue
		}
		if err := os.RemoveAll(filepath.Join(r.workspaceDir, entry.Name())); err != nil {
			return fmt.Errorf("erro ao limpar %q: %w", entry.Name(), err)
		}
	}

	for _, folder := range target.Folders {
		absFolder := filepath.Join(r.workspaceDir, filepath.FromSlash(folder))
		if err := os.MkdirAll(absFolder, 0o755); err != nil {
			return fmt.Errorf("erro ao recriar pasta %q: %w", folder, err)
		}
	}

	for _, f := range target.Files {
		absFile := filepath.Join(r.workspaceDir, filepath.FromSlash(f.Path))
		if err := os.MkdirAll(filepath.Dir(absFile), 0o755); err != nil {
			return fmt.Errorf("erro ao criar diretório pai de %q: %w", f.Path, err)
		}
		if err := os.WriteFile(absFile, contents[f.Path], 0o644); err != nil {
			return fmt.Errorf("erro ao restaurar ficheiro %q: %w", f.Path, err)
		}
	}

	return r.updateHeadLocked(target.ID)
}

func sha256BytesCGO(data []byte) (string, error) {
	var ptr unsafe.Pointer
	if len(data) > 0 {
		ptr = unsafe.Pointer(&data[0])
	}
	var hashBuf [C.ANEDOTA_SHA256_HEX_SIZE]C.char
	if rc := C.anedota_sha256_bytes(ptr, C.size_t(len(data)), &hashBuf[0]); rc != 0 {
		return "", fmt.Errorf("anedota_sha256_bytes falhou (código %d)", int(rc))
	}
	return C.GoString(&hashBuf[0]), nil
}

func (r *AnedotaCGORepository) Status() (WorkspaceStatus, error) {
	r.mu.Lock()
	defer r.mu.Unlock()

	status := WorkspaceStatus{
		Driver:  r.DriverName(),
		Changes: []Change{},
	}

	if info, err := os.Stat(filepath.Join(r.anedotaRoot, "objetos")); err == nil && info.IsDir() {
		status.Initialized = true
	}

	headFiles := make(map[string]string)
	headFolders := make(map[string]struct{})

	if status.Initialized {
		headHash, hasHead, err := r.readHeadLocked()
		if err != nil {
			return status, err
		}
		if hasHead {
			status.HeadCommitID = headHash
			cRoot := C.CString(r.anedotaRoot)
			var cLog *C.anedota_log_t
			if C.anedota_log_read(cRoot, &cLog) == 0 {
				status.TotalCommits = int(cLog.count)
				C.free(unsafe.Pointer(cLog))
			}
			C.free(unsafe.Pointer(cRoot))

			if headCommit, err := r.readCommitLocked(headHash); err == nil {
				for _, f := range headCommit.Files {
					headFiles[f.Path] = f.BlobHash
				}
				for _, dir := range headCommit.Folders {
					headFolders[dir] = struct{}{}
				}
			}
		}
	}

	tmpRepo := &AnedotaRepository{workspaceDir: r.workspaceDir}
	curFolders, curFiles, err := tmpRepo.scanWorkspaceLocked()
	if err != nil {
		return status, err
	}

	curFileHashes := make(map[string]string, len(curFiles))
	for _, relPath := range curFiles {
		absPath := filepath.Join(r.workspaceDir, filepath.FromSlash(relPath))
		data, err := os.ReadFile(absPath)
		if err != nil {
			return status, err
		}
		hash, err := sha256BytesCGO(data)
		if err != nil {
			return status, err
		}
		curFileHashes[relPath] = hash
	}

	curFolderSet := make(map[string]struct{}, len(curFolders))
	for _, f := range curFolders {
		curFolderSet[f] = struct{}{}
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
