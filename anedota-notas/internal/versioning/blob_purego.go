//go:build !anedota

package versioning

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"os"
	"path/filepath"
)

type pureGoBlobDriver struct{}

func newDefaultBlobDriver() BlobDriver {
	return &pureGoBlobDriver{}
}

func (d *pureGoBlobDriver) Name() string {
	return "anedota-simulado (Go puro compatível com .anedota/)"
}

func (d *pureGoBlobDriver) Init(anedotaRoot string) error {
	dirs := []string{
		anedotaRoot,
		filepath.Join(anedotaRoot, "objetos"),
		filepath.Join(anedotaRoot, "referencias"),
		filepath.Join(anedotaRoot, "referencias", "branches"),
	}
	for _, dir := range dirs {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return fmt.Errorf("erro ao criar diretório %q: %w", dir, err)
		}
	}
	return nil
}

func (d *pureGoBlobDriver) WriteBlobFile(anedotaRoot, sourcePath string) (string, error) {
	objectsDir := filepath.Join(anedotaRoot, "objetos")
	if info, err := os.Stat(objectsDir); err != nil || !info.IsDir() {
		return "", ErrNotInitialized
	}

	data, err := os.ReadFile(sourcePath)
	if err != nil {
		return "", fmt.Errorf("erro ao ler ficheiro %q: %w", sourcePath, err)
	}

	sum := sha256.Sum256(data)
	hash := hex.EncodeToString(sum[:])

	blobPath := filepath.Join(objectsDir, hash)
	if _, err := os.Stat(blobPath); err == nil {
		return hash, nil
	}

	tmp, err := os.CreateTemp(objectsDir, ".blob-*")
	if err != nil {
		return "", err
	}
	tmpName := tmp.Name()
	if _, err := tmp.Write(data); err != nil {
		tmp.Close()
		os.Remove(tmpName)
		return "", err
	}
	if err := tmp.Close(); err != nil {
		os.Remove(tmpName)
		return "", err
	}
	if err := os.Rename(tmpName, blobPath); err != nil {
		os.Remove(tmpName)
		return "", err
	}

	return hash, nil
}

func (d *pureGoBlobDriver) ReadBlob(anedotaRoot, hash string) ([]byte, error) {
	if !isValidSHA256Hex(hash) {
		return nil, fmt.Errorf("hash SHA-256 inválido: %q", hash)
	}

	blobPath := filepath.Join(anedotaRoot, "objetos", hash)
	data, err := os.ReadFile(blobPath)
	if err != nil {
		return nil, fmt.Errorf("erro ao ler blob %q: %w", hash, err)
	}
	return data, nil
}
