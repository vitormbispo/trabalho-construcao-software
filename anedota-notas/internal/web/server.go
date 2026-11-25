package web

import (
	"embed"
	"encoding/json"
	"errors"
	"io/fs"
	"net/http"

	"anedota-notas/internal/notes"
	"anedota-notas/internal/versioning"
)

//go:embed static/*
var staticFiles embed.FS

// Server expõe a API HTTP e a interface web da aplicação de anotações.
type Server struct {
	svc *notes.Service
	mux *http.ServeMux
}

func NewServer(svc *notes.Service) (*Server, error) {
	s := &Server{
		svc: svc,
		mux: http.NewServeMux(),
	}

	s.mux.HandleFunc("GET /api/tree", s.handleGetTree)
	s.mux.HandleFunc("POST /api/folders", s.handleCreateFolder)
	s.mux.HandleFunc("DELETE /api/folders", s.handleDeleteFolder)

	s.mux.HandleFunc("GET /api/note", s.handleReadNote)
	s.mux.HandleFunc("PUT /api/note", s.handleSaveNote)
	s.mux.HandleFunc("POST /api/note/rename", s.handleRenameNote)
	s.mux.HandleFunc("DELETE /api/note", s.handleDeleteNote)

	s.mux.HandleFunc("GET /api/versioning/status", s.handleStatus)
	s.mux.HandleFunc("GET /api/versioning/commits", s.handleListCommits)
	s.mux.HandleFunc("POST /api/versioning/commits", s.handleCreateCommit)
	s.mux.HandleFunc("GET /api/versioning/diff", s.handleDiffCommits)
	s.mux.HandleFunc("POST /api/versioning/restore", s.handleRestoreCommit)

	sub, err := fs.Sub(staticFiles, "static")
	if err != nil {
		return nil, err
	}
	s.mux.Handle("/", http.FileServer(http.FS(sub)))

	return s, nil
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	s.mux.ServeHTTP(w, r)
}

func (s *Server) handleGetTree(w http.ResponseWriter, r *http.Request) {
	tree, err := s.svc.ListTree()
	if err != nil {
		writeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, tree)
}

func (s *Server) handleCreateFolder(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Path string `json:"path"`
	}
	if err := decodeJSON(r, &req); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "JSON inválido"})
		return
	}
	created, err := s.svc.CreateFolder(req.Path)
	if err != nil {
		writeError(w, err)
		return
	}
	writeJSON(w, http.StatusCreated, map[string]string{"path": created})
}

func (s *Server) handleDeleteFolder(w http.ResponseWriter, r *http.Request) {
	path := r.URL.Query().Get("path")
	if err := s.svc.DeleteFolder(path); err != nil {
		writeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]string{"deleted": path})
}

func (s *Server) handleReadNote(w http.ResponseWriter, r *http.Request) {
	path := r.URL.Query().Get("path")
	note, err := s.svc.ReadNote(path)
	if err != nil {
		writeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, note)
}

func (s *Server) handleSaveNote(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Path    string `json:"path"`
		Content string `json:"content"`
	}
	if err := decodeJSON(r, &req); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "JSON inválido"})
		return
	}
	note, err := s.svc.SaveNote(req.Path, req.Content)
	if err != nil {
		writeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, note)
}

func (s *Server) handleRenameNote(w http.ResponseWriter, r *http.Request) {
	var req struct {
		OldPath string `json:"oldPath"`
		NewPath string `json:"newPath"`
	}
	if err := decodeJSON(r, &req); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "JSON inválido"})
		return
	}
	note, err := s.svc.RenameNote(req.OldPath, req.NewPath)
	if err != nil {
		writeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, note)
}

func (s *Server) handleDeleteNote(w http.ResponseWriter, r *http.Request) {
	path := r.URL.Query().Get("path")
	if err := s.svc.DeleteNote(path); err != nil {
		writeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]string{"deleted": path})
}

func (s *Server) handleStatus(w http.ResponseWriter, r *http.Request) {
	status, err := s.svc.Status()
	if err != nil {
		writeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, status)
}

func (s *Server) handleListCommits(w http.ResponseWriter, r *http.Request) {
	commits, err := s.svc.ListCommits()
	if err != nil {
		writeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"commits": commits})
}

func (s *Server) handleCreateCommit(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Message string `json:"message"`
	}
	if err := decodeJSON(r, &req); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "JSON inválido"})
		return
	}
	commit, err := s.svc.Commit(req.Message)
	if err != nil {
		writeError(w, err)
		return
	}
	writeJSON(w, http.StatusCreated, commit)
}

func (s *Server) handleDiffCommits(w http.ResponseWriter, r *http.Request) {
	fromID := r.URL.Query().Get("from")
	toID := r.URL.Query().Get("to")
	changes, err := s.svc.DiffCommits(fromID, toID)
	if err != nil {
		writeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"changes": changes})
}

func (s *Server) handleRestoreCommit(w http.ResponseWriter, r *http.Request) {
	var req struct {
		CommitID string `json:"commitId"`
	}
	if err := decodeJSON(r, &req); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "JSON inválido"})
		return
	}
	if err := s.svc.RestoreCommit(req.CommitID); err != nil {
		writeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]string{"restored": req.CommitID})
}

func decodeJSON(r *http.Request, dst any) error {
	defer r.Body.Close()
	dec := json.NewDecoder(http.MaxBytesReader(nil, r.Body, 4<<20))
	return dec.Decode(dst)
}

func writeJSON(w http.ResponseWriter, status int, payload any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(payload)
}

func writeError(w http.ResponseWriter, err error) {
	code := http.StatusInternalServerError
	switch {
	case errors.Is(err, notes.ErrInvalidPath),
		errors.Is(err, versioning.ErrEmptyCommitMessage):
		code = http.StatusBadRequest
	case errors.Is(err, notes.ErrNoteNotFound),
		errors.Is(err, notes.ErrFolderNotFound),
		errors.Is(err, versioning.ErrCommitNotFound):
		code = http.StatusNotFound
	case errors.Is(err, notes.ErrAlreadyExists):
		code = http.StatusConflict
	}
	writeJSON(w, code, map[string]string{"error": err.Error()})
}
