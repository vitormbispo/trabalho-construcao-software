package versioning

import (
	"errors"
	"time"
)

var (
	ErrEmptyCommitMessage = errors.New("a mensagem do commit não pode ser vazia")
	ErrCommitNotFound     = errors.New("commit não encontrado")
	ErrNotInitialized     = errors.New("repositório .anedota não inicializado")
)

// FileEntry representa um ficheiro de anotação capturado em um snapshot.
type FileEntry struct {
	Path     string `json:"path"`
	BlobHash string `json:"blobHash"`
	Size     int64  `json:"size"`
}

// Commit representa um snapshot completo do diretório de notas.
// Alinhado ao MVP do ANEDOTA: cada commit registra o estado completo versionado.
type Commit struct {
	ID        string      `json:"id"`
	ParentID  string      `json:"parentId,omitempty"`
	Message   string      `json:"message"`
	CreatedAt time.Time   `json:"createdAt"`
	Folders   []string    `json:"folders"`
	Files     []FileEntry `json:"files"`
}

// Change descreve uma diferença entre o diretório de trabalho atual e o último commit.
type Change struct {
	Path  string `json:"path"`
	Kind  string `json:"kind"` // "added", "modified", "deleted"
	IsDir bool   `json:"isDir"`
}

// WorkspaceStatus resume o estado atual do versionamento das notas.
type WorkspaceStatus struct {
	Driver       string   `json:"driver"`
	Initialized  bool     `json:"initialized"`
	HeadCommitID string   `json:"headCommitId,omitempty"`
	TotalCommits int      `json:"totalCommits"`
	Dirty        bool     `json:"dirty"`
	Changes      []Change `json:"changes"`
}

// BlobDriver abstrai as operações de baixo nível já disponíveis na API C do ANEDOTA:
//   - anedota_storage_init
//   - anedota_blob_write_file
//   - anedota_blob_read
//
// Existem duas implementações:
//   - build padrão (!anedota): implementação em Go puro compatível com .anedota/
//   - build com -tags anedota: binding CGO direto para a biblioteca C do ANEDOTA
type BlobDriver interface {
	Name() string
	Init(anedotaRoot string) error
	WriteBlobFile(anedotaRoot, sourcePath string) (string, error)
	ReadBlob(anedotaRoot, hash string) ([]byte, error)
}

// Repository define o contrato completo de versionamento usado pela aplicação de notas.
// No build com -tags anedota, todas as operações usam diretamente a biblioteca C libanedota.a.
type Repository interface {
	DriverName() string
	Init() error
	Commit(message string) (Commit, error)
	ListCommits() ([]Commit, error)
	GetCommit(id string) (Commit, error)
	Diff(fromID, toID string) ([]Change, error)
	Restore(commitID string) error
	Status() (WorkspaceStatus, error)
}
