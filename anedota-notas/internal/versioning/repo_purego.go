//go:build !anedota

package versioning

// NewRepository cria o repositório em Go puro (fallback quando não compilado com -tags anedota).
func NewRepository(workspaceDir string) (Repository, error) {
	return NewRepositoryWithDriver(workspaceDir, newDefaultBlobDriver())
}
