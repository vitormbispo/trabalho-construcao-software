package versioning

import (
	"os"
	"path/filepath"
	"testing"
)

func TestRepositoryCommitAndRestore(t *testing.T) {
	dir := t.TempDir()

	repo, err := NewRepository(dir)
	if err != nil {
		t.Fatalf("NewRepository: %v", err)
	}
	t.Logf("Testando com driver: %s", repo.DriverName())

	if err := repo.Init(); err != nil {
		t.Fatalf("Init: %v", err)
	}

	// Verifica estrutura padrão .anedota/
	for _, sub := range []string{".anedota/objetos", ".anedota/referencias/branches"} {
		if info, err := os.Stat(filepath.Join(dir, sub)); err != nil || !info.IsDir() {
			t.Fatalf("diretório esperado %q ausente", sub)
		}
	}

	// Cria pastas (incluindo uma pasta vazia) e notas iniciais.
	if err := os.MkdirAll(filepath.Join(dir, "faculdade"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(dir, "vazia"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "inicio.md"), []byte("# Olá\nVersão 1"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "faculdade", "notas.md"), []byte("Aula 1"), 0o644); err != nil {
		t.Fatal(err)
	}

	// Status deve indicar alterações pendentes.
	st, err := repo.Status()
	if err != nil {
		t.Fatalf("Status: %v", err)
	}
	if !st.Dirty {
		t.Fatalf("esperava workspace dirty antes do primeiro commit")
	}

	// Commit 1
	c1, err := repo.Commit("Primeiro snapshot")
	if err != nil {
		t.Fatalf("Commit 1: %v", err)
	}
	if len(c1.Files) != 2 {
		t.Fatalf("esperava 2 arquivos no commit 1, obteve %d", len(c1.Files))
	}
	if len(c1.Folders) != 2 {
		t.Fatalf("esperava 2 pastas no commit 1, obteve %d", len(c1.Folders))
	}

	// Verifica se os Blobs existem realmente em .anedota/objetos/<hash>
	for _, f := range c1.Files {
		blobPath := filepath.Join(dir, ".anedota", "objetos", f.BlobHash)
		if _, err := os.Stat(blobPath); err != nil {
			t.Fatalf("blob %q não encontrado em %q: %v", f.BlobHash, blobPath, err)
		}
	}

	// Agora altera, apaga e cria arquivos novos.
	if err := os.WriteFile(filepath.Join(dir, "inicio.md"), []byte("# Olá\nVersão 2 editada"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.Remove(filepath.Join(dir, "faculdade", "notas.md")); err != nil {
		t.Fatal(err)
	}
	if err := os.Remove(filepath.Join(dir, "vazia")); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "extra.md"), []byte("Arquivo extra"), 0o644); err != nil {
		t.Fatal(err)
	}

	// Commit 2
	c2, err := repo.Commit("Segunda versão")
	if err != nil {
		t.Fatalf("Commit 2: %v", err)
	}
	if c2.ParentID != c1.ID {
		t.Fatalf("ParentID incorreto: obteve %q, esperava %q", c2.ParentID, c1.ID)
	}

	// Histórico deve retornar mais recente primeiro.
	commits, err := repo.ListCommits()
	if err != nil {
		t.Fatalf("ListCommits: %v", err)
	}
	if len(commits) != 2 || commits[0].ID != c2.ID || commits[1].ID != c1.ID {
		t.Fatalf("ordem inesperada no histórico: %+v", commits)
	}

	// Restaura Commit 1
	if err := repo.Restore(c1.ID); err != nil {
		t.Fatalf("Restore c1: %v", err)
	}

	gotInicio, err := os.ReadFile(filepath.Join(dir, "inicio.md"))
	if err != nil || string(gotInicio) != "# Olá\nVersão 1" {
		t.Fatalf("inicio.md não foi restaurado corretamente: %q (err=%v)", string(gotInicio), err)
	}
	gotFac, err := os.ReadFile(filepath.Join(dir, "faculdade", "notas.md"))
	if err != nil || string(gotFac) != "Aula 1" {
		t.Fatalf("faculdade/notas.md não foi restaurado: %q (err=%v)", string(gotFac), err)
	}
	if info, err := os.Stat(filepath.Join(dir, "vazia")); err != nil || !info.IsDir() {
		t.Fatalf("pasta vazia não foi restaurada")
	}
	if _, err := os.Stat(filepath.Join(dir, "extra.md")); !os.IsNotExist(err) {
		t.Fatalf("extra.md deveria ter sido removido ao restaurar c1")
	}

	stAfter, err := repo.Status()
	if err != nil {
		t.Fatalf("Status após restore: %v", err)
	}
	if stAfter.Dirty {
		t.Fatalf("workspace deveria estar limpo logo após restaurar c1, changes=%+v", stAfter.Changes)
	}
	if stAfter.HeadCommitID != c1.ID {
		t.Fatalf("HEAD deveria apontar para c1 (%s), obteve %s", c1.ID, stAfter.HeadCommitID)
	}

	// Restaura Commit 2 para garantir ida e volta
	if err := repo.Restore(c2.ID[:12]); err != nil {
		t.Fatalf("Restore c2 por prefixo: %v", err)
	}
	gotExtra, err := os.ReadFile(filepath.Join(dir, "extra.md"))
	if err != nil || string(gotExtra) != "Arquivo extra" {
		t.Fatalf("extra.md não foi restaurado em c2: %q (err=%v)", string(gotExtra), err)
	}

	// Compara c1 -> c2 usando Diff (nativo via anedota_diff_versions em -tags anedota)
	changes, err := repo.Diff(c1.ID, c2.ID)
	if err != nil {
		t.Fatalf("Diff c1->c2: %v", err)
	}
	byPath := make(map[string]string, len(changes))
	for _, ch := range changes {
		byPath[ch.Path] = ch.Kind
	}
	if byPath["inicio.md"] != "modified" || byPath["extra.md"] != "added" || byPath["faculdade/notas.md"] != "deleted" {
		t.Fatalf("diff inesperado entre c1 e c2: %+v", changes)
	}

	// Versões iguais não devem ter diferenças
	same, err := repo.Diff(c1.ID, c1.ID)
	if err != nil {
		t.Fatalf("Diff c1->c1: %v", err)
	}
	if len(same) != 0 {
		t.Fatalf("esperava 0 diferenças para versões iguais, obteve %+v", same)
	}

	// Versão inexistente deve retornar erro
	if _, err := repo.Diff(c1.ID, "0000000000000000000000000000000000000000000000000000000000000000"); err == nil {
		t.Fatalf("esperava erro ao comparar com versão inexistente")
	}
}
