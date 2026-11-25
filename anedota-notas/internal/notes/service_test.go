package notes

import (
	"errors"
	"testing"
)

func TestNotesCRUDAndVersioning(t *testing.T) {
	dir := t.TempDir()
	svc, err := NewService(dir, nil)
	if err != nil {
		t.Fatalf("NewService: %v", err)
	}

	// Cria pastas
	if _, err := svc.CreateFolder("estudos/go"); err != nil {
		t.Fatalf("CreateFolder: %v", err)
	}

	// Cria notas em ficheiros diferentes (com e sem extensão explícita)
	n1, err := svc.SaveNote("estudos/go/resumo", "# Resumo Go\nInterfaces são ótimas.")
	if err != nil {
		t.Fatalf("SaveNote 1: %v", err)
	}
	if n1.Path != "estudos/go/resumo.md" {
		t.Fatalf("esperava extensão .md automática, obteve %q", n1.Path)
	}

	n2, err := svc.SaveNote("lista.txt", "Comprar café")
	if err != nil {
		t.Fatalf("SaveNote 2: %v", err)
	}
	if n2.Path != "lista.txt" {
		t.Fatalf("esperava lista.txt, obteve %q", n2.Path)
	}

	// Bloqueia caminhos inválidos ou acesso a .anedota
	for _, bad := range []string{"../fora.md", ".anedota/objetos/abc", ".oculto.md", ""} {
		if _, err := svc.SaveNote(bad, "x"); !errors.Is(err, ErrInvalidPath) {
			t.Fatalf("esperava ErrInvalidPath para %q, obteve %v", bad, err)
		}
	}

	// Faz commit v1
	c1, err := svc.Commit("Notas iniciais")
	if err != nil {
		t.Fatalf("Commit 1: %v", err)
	}

	// Edita, renomeia e apaga
	if _, err := svc.SaveNote("estudos/go/resumo.md", "Conteúdo alterado"); err != nil {
		t.Fatalf("Edit: %v", err)
	}
	if _, err := svc.RenameNote("lista.txt", "pessoal/compras.txt"); err != nil {
		t.Fatalf("RenameNote: %v", err)
	}
	if err := svc.DeleteNote("estudos/go/resumo.md"); err != nil {
		t.Fatalf("DeleteNote: %v", err)
	}

	// Verifica que lista.txt não existe mais e pessoal/compras.txt existe
	if _, err := svc.ReadNote("lista.txt"); !errors.Is(err, ErrNoteNotFound) {
		t.Fatalf("lista.txt deveria ter sido movido")
	}
	moved, err := svc.ReadNote("pessoal/compras.txt")
	if err != nil || moved.Content != "Comprar café" {
		t.Fatalf("pessoal/compras.txt inválido: %+v err=%v", moved, err)
	}

	// Commit v2
	if _, err := svc.Commit("Reorganizou notas"); err != nil {
		t.Fatalf("Commit 2: %v", err)
	}

	// Restaura v1
	if err := svc.RestoreCommit(c1.ID); err != nil {
		t.Fatalf("RestoreCommit c1: %v", err)
	}

	restored1, err := svc.ReadNote("estudos/go/resumo.md")
	if err != nil || restored1.Content != "# Resumo Go\nInterfaces são ótimas." {
		t.Fatalf("nota 1 não restaurada: %+v err=%v", restored1, err)
	}
	restored2, err := svc.ReadNote("lista.txt")
	if err != nil || restored2.Content != "Comprar café" {
		t.Fatalf("nota 2 não restaurada: %+v err=%v", restored2, err)
	}
	if _, err := svc.ReadNote("pessoal/compras.txt"); !errors.Is(err, ErrNoteNotFound) {
		t.Fatalf("pessoal/compras.txt não deveria existir após restaurar c1")
	}
}
