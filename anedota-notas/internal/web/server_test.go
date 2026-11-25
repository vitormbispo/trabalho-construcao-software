package web

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	"anedota-notas/internal/notes"
	"anedota-notas/internal/versioning"
)

func TestHTTPFlowNotesAndVersioning(t *testing.T) {
	svc, err := notes.NewService(t.TempDir(), nil)
	if err != nil {
		t.Fatalf("NewService: %v", err)
	}
	srv, err := NewServer(svc)
	if err != nil {
		t.Fatalf("NewServer: %v", err)
	}

	do := func(method, url string, body any) *httptest.ResponseRecorder {
		t.Helper()
		var buf bytes.Buffer
		if body != nil {
			if err := json.NewEncoder(&buf).Encode(body); err != nil {
				t.Fatal(err)
			}
		}
		req := httptest.NewRequest(method, url, &buf)
		if body != nil {
			req.Header.Set("Content-Type", "application/json")
		}
		rec := httptest.NewRecorder()
		srv.ServeHTTP(rec, req)
		return rec
	}

	// 1. Cria pasta
	if rec := do(http.MethodPost, "/api/folders", map[string]string{"path": "diario"}); rec.Code != http.StatusCreated {
		t.Fatalf("POST /api/folders: status %d body=%s", rec.Code, rec.Body.String())
	}

	// 2. Salva nota
	if rec := do(http.MethodPut, "/api/note", map[string]string{
		"path":    "diario/dia1.md",
		"content": "Primeira anotação",
	}); rec.Code != http.StatusOK {
		t.Fatalf("PUT /api/note: status %d body=%s", rec.Code, rec.Body.String())
	}

	// 3. Cria commit
	recCommit := do(http.MethodPost, "/api/versioning/commits", map[string]string{"message": "Commit inicial"})
	if recCommit.Code != http.StatusCreated {
		t.Fatalf("POST /api/versioning/commits: status %d body=%s", recCommit.Code, recCommit.Body.String())
	}
	var c1 versioning.Commit
	if err := json.Unmarshal(recCommit.Body.Bytes(), &c1); err != nil {
		t.Fatal(err)
	}

	// 4. Altera a nota e cria segundo commit
	if rec := do(http.MethodPut, "/api/note", map[string]string{
		"path":    "diario/dia1.md",
		"content": "Conteúdo modificado depois",
	}); rec.Code != http.StatusOK {
		t.Fatalf("PUT /api/note edit: status %d", rec.Code)
	}

	recCommit2 := do(http.MethodPost, "/api/versioning/commits", map[string]string{"message": "Segundo commit"})
	if recCommit2.Code != http.StatusCreated {
		t.Fatalf("POST /api/versioning/commits 2: status %d body=%s", recCommit2.Code, recCommit2.Body.String())
	}
	var c2 versioning.Commit
	if err := json.Unmarshal(recCommit2.Body.Bytes(), &c2); err != nil {
		t.Fatal(err)
	}

	// 4.1 Consulta diff entre commits via API HTTP
	recDiff := do(http.MethodGet, "/api/versioning/diff?from="+c1.ID+"&to="+c2.ID, nil)
	if recDiff.Code != http.StatusOK {
		t.Fatalf("GET /api/versioning/diff: status %d body=%s", recDiff.Code, recDiff.Body.String())
	}
	var diffResp struct {
		Changes []versioning.Change `json:"changes"`
	}
	if err := json.Unmarshal(recDiff.Body.Bytes(), &diffResp); err != nil {
		t.Fatal(err)
	}
	if len(diffResp.Changes) != 1 || diffResp.Changes[0].Path != "diario/dia1.md" || diffResp.Changes[0].Kind != "modified" {
		t.Fatalf("diff HTTP inesperado: %+v", diffResp.Changes)
	}

	// 5. Restaura o commit 1
	if rec := do(http.MethodPost, "/api/versioning/restore", map[string]string{"commitId": c1.ID}); rec.Code != http.StatusOK {
		t.Fatalf("POST /api/versioning/restore: status %d body=%s", rec.Code, rec.Body.String())
	}

	// 6. Confirma conteúdo restaurado via API
	recRead := do(http.MethodGet, "/api/note?path=diario/dia1.md", nil)
	if recRead.Code != http.StatusOK {
		t.Fatalf("GET /api/note: status %d", recRead.Code)
	}
	var note notes.Note
	if err := json.Unmarshal(recRead.Body.Bytes(), &note); err != nil {
		t.Fatal(err)
	}
	if note.Content != "Primeira anotação" {
		t.Fatalf("conteúdo esperado após restore: 'Primeira anotação', obteve %q", note.Content)
	}
}
