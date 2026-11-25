const state = {
  currentPath: null,
  savedContent: "",
  dirty: false,
  collapsedFolders: new Set(),
  headCommitId: "",
  repoDirty: false,
};

const els = {
  driverBadge: document.getElementById("driver-badge"),
  treeContainer: document.getElementById("tree-container"),
  emptyState: document.getElementById("empty-state"),
  editorWrapper: document.getElementById("editor-wrapper"),
  currentNotePath: document.getElementById("current-note-path"),
  unsavedPill: document.getElementById("unsaved-pill"),
  noteEditor: document.getElementById("note-editor"),
  editorStats: document.getElementById("editor-stats"),
  editorSavedAt: document.getElementById("editor-saved-at"),
  repoDirtyPill: document.getElementById("repo-dirty-pill"),
  commitMessage: document.getElementById("commit-message"),
  changesList: document.getElementById("changes-list"),
  commitsList: document.getElementById("commits-list"),
  toast: document.getElementById("toast"),
};

let toastTimer = null;
function showToast(message, isError = false) {
  els.toast.textContent = message;
  els.toast.classList.toggle("error", isError);
  els.toast.classList.remove("hidden");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => els.toast.classList.add("hidden"), 3000);
}

async function api(url, options = {}) {
  const res = await fetch(url, {
    headers: { "Content-Type": "application/json", ...(options.headers || {}) },
    ...options,
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) {
    throw new Error(data.error || `Erro HTTP ${res.status}`);
  }
  return data;
}

function setEditorDirty(dirty) {
  state.dirty = dirty;
  els.unsavedPill.classList.toggle("hidden", !dirty);
}

function updateStats() {
  const text = els.noteEditor.value;
  const lines = text.length === 0 ? 1 : text.split("\n").length;
  els.editorStats.textContent = `${text.length} caracteres · ${lines} ${lines === 1 ? "linha" : "linhas"}`;
}

function closeEditor() {
  state.currentPath = null;
  state.savedContent = "";
  setEditorDirty(false);
  els.editorWrapper.classList.add("hidden");
  els.emptyState.classList.remove("hidden");
}

async function openNote(path) {
  if (state.dirty && state.currentPath !== path) {
    if (!confirm("Você tem alterações não salvas na nota atual. Descartar e abrir outra nota?")) {
      return;
    }
  }
  try {
    const note = await api(`/api/note?path=${encodeURIComponent(path)}`);
    state.currentPath = note.path;
    state.savedContent = note.content;
    els.currentNotePath.textContent = note.path;
    els.noteEditor.value = note.content;
    els.editorSavedAt.textContent = `Modificado em ${new Date(note.modTime).toLocaleString("pt-BR")}`;
    setEditorDirty(false);
    updateStats();
    els.emptyState.classList.add("hidden");
    els.editorWrapper.classList.remove("hidden");
    await refreshTree();
  } catch (err) {
    showToast(err.message, true);
  }
}

async function saveCurrentNote(targetPath = state.currentPath) {
  if (!targetPath) return;
  try {
    const note = await api("/api/note", {
      method: "PUT",
      body: JSON.stringify({ path: targetPath, content: els.noteEditor.value }),
    });
    state.currentPath = note.path;
    state.savedContent = note.content;
    els.currentNotePath.textContent = note.path;
    els.editorSavedAt.textContent = `Salvo em ${new Date(note.modTime).toLocaleString("pt-BR")}`;
    setEditorDirty(false);
    showToast(`Nota salva em ${note.path}`);
    await refreshAll();
  } catch (err) {
    showToast(err.message, true);
  }
}

async function createNotePrompt(folderPrefix = "") {
  const suggestion = folderPrefix ? `${folderPrefix}/nova-nota.md` : "nova-nota.md";
  const path = prompt("Caminho da nova nota (ex: estudos/aula1.md):", suggestion);
  if (!path || !path.trim()) return;
  try {
    const note = await api("/api/note", {
      method: "PUT",
      body: JSON.stringify({ path: path.trim(), content: "" }),
    });
    showToast(`Nota criada: ${note.path}`);
    await refreshAll();
    await openNote(note.path);
    els.noteEditor.focus();
  } catch (err) {
    showToast(err.message, true);
  }
}

async function createFolderPrompt(folderPrefix = "") {
  const suggestion = folderPrefix ? `${folderPrefix}/subpasta` : "estudos";
  const path = prompt("Nome/caminho da nova pasta:", suggestion);
  if (!path || !path.trim()) return;
  try {
    const res = await api("/api/folders", {
      method: "POST",
      body: JSON.stringify({ path: path.trim() }),
    });
    showToast(`Pasta criada: ${res.path}`);
    await refreshAll();
  } catch (err) {
    showToast(err.message, true);
  }
}

async function deleteNote(path) {
  if (!confirm(`Excluir a nota "${path}"?`)) return;
  try {
    await api(`/api/note?path=${encodeURIComponent(path)}`, { method: "DELETE" });
    if (state.currentPath === path) {
      closeEditor();
    }
    showToast(`Nota excluída: ${path}`);
    await refreshAll();
  } catch (err) {
    showToast(err.message, true);
  }
}

async function deleteFolder(path) {
  if (!confirm(`Excluir a pasta "${path}" e todas as notas dentro dela?`)) return;
  try {
    await api(`/api/folders?path=${encodeURIComponent(path)}`, { method: "DELETE" });
    if (state.currentPath && (state.currentPath === path || state.currentPath.startsWith(path + "/"))) {
      closeEditor();
    }
    showToast(`Pasta excluída: ${path}`);
    await refreshAll();
  } catch (err) {
    showToast(err.message, true);
  }
}

async function renameCurrentNote() {
  if (!state.currentPath) return;
  if (state.dirty) {
    await saveCurrentNote();
  }
  const newPath = prompt("Novo caminho da nota:", state.currentPath);
  if (!newPath || !newPath.trim() || newPath.trim() === state.currentPath) return;
  try {
    const note = await api("/api/note/rename", {
      method: "POST",
      body: JSON.stringify({ oldPath: state.currentPath, newPath: newPath.trim() }),
    });
    state.currentPath = note.path;
    els.currentNotePath.textContent = note.path;
    showToast(`Renomeado para ${note.path}`);
    await refreshAll();
  } catch (err) {
    showToast(err.message, true);
  }
}

async function saveAsCurrentNote() {
  if (!state.currentPath) return;
  const newPath = prompt("Salvar cópia em outro ficheiro/pasta:", state.currentPath);
  if (!newPath || !newPath.trim()) return;
  await saveCurrentNote(newPath.trim());
}

function renderTreeNodes(nodes, container) {
  for (const node of nodes) {
    if (node.type === "folder") {
      const collapsed = state.collapsedFolders.has(node.path);
      const row = document.createElement("div");
      row.className = "tree-row";

      const label = document.createElement("div");
      label.className = "tree-label";
      label.textContent = `${collapsed ? "▸" : "▾"} 📁 ${node.name}`;
      label.addEventListener("click", () => {
        if (collapsed) state.collapsedFolders.delete(node.path);
        else state.collapsedFolders.add(node.path);
        refreshTree();
      });

      const actions = document.createElement("div");
      actions.className = "tree-row-actions";

      const btnAddNote = document.createElement("button");
      btnAddNote.className = "icon-btn";
      btnAddNote.title = "Nova nota nesta pasta";
      btnAddNote.textContent = "+";
      btnAddNote.addEventListener("click", (e) => {
        e.stopPropagation();
        createNotePrompt(node.path);
      });

      const btnDel = document.createElement("button");
      btnDel.className = "icon-btn danger";
      btnDel.title = "Excluir pasta";
      btnDel.textContent = "×";
      btnDel.addEventListener("click", (e) => {
        e.stopPropagation();
        deleteFolder(node.path);
      });

      actions.append(btnAddNote, btnDel);
      row.append(label, actions);
      container.appendChild(row);

      if (!collapsed && node.children && node.children.length > 0) {
        const childrenEl = document.createElement("div");
        childrenEl.className = "tree-children";
        renderTreeNodes(node.children, childrenEl);
        container.appendChild(childrenEl);
      }
    } else {
      const row = document.createElement("div");
      row.className = "tree-row" + (state.currentPath === node.path ? " active" : "");

      const label = document.createElement("div");
      label.className = "tree-label";
      label.textContent = `📄 ${node.name}`;
      row.addEventListener("click", () => openNote(node.path));

      const actions = document.createElement("div");
      actions.className = "tree-row-actions";
      const btnDel = document.createElement("button");
      btnDel.className = "icon-btn danger";
      btnDel.title = "Excluir nota";
      btnDel.textContent = "×";
      btnDel.addEventListener("click", (e) => {
        e.stopPropagation();
        deleteNote(node.path);
      });
      actions.append(btnDel);

      row.append(label, actions);
      container.appendChild(row);
    }
  }
}

async function refreshTree() {
  const tree = await api("/api/tree");
  els.treeContainer.innerHTML = "";
  if (!tree.nodes || tree.nodes.length === 0) {
    els.treeContainer.innerHTML = `<div class="muted-empty">Nenhuma pasta ou nota ainda.</div>`;
    return;
  }
  renderTreeNodes(tree.nodes, els.treeContainer);
}

async function refreshVersioning() {
  const [status, commitsRes] = await Promise.all([
    api("/api/versioning/status"),
    api("/api/versioning/commits"),
  ]);

  state.headCommitId = status.headCommitId || "";
  state.repoDirty = status.dirty;

  els.driverBadge.textContent = status.driver;
  els.driverBadge.title = status.driver;

  if (status.dirty) {
    els.repoDirtyPill.textContent = `${status.changes.length} pendente(s)`;
    els.repoDirtyPill.className = "pill warning";
  } else {
    els.repoDirtyPill.textContent = "Limpo";
    els.repoDirtyPill.className = "pill";
  }

  els.changesList.innerHTML = "";
  if (!status.changes || status.changes.length === 0) {
    els.changesList.innerHTML = `<li class="muted-empty">Nenhuma alteração desde o último commit.</li>`;
  } else {
    const labels = { added: "novo", modified: "editado", deleted: "removido" };
    for (const ch of status.changes) {
      const li = document.createElement("li");
      li.className = "change-item";
      li.innerHTML = `<span class="change-tag ${ch.kind}">${labels[ch.kind] || ch.kind}</span><span>${ch.isDir ? "📁 " : "📄 "}${escapeHtml(ch.path)}</span>`;
      els.changesList.appendChild(li);
    }
  }

  els.commitsList.innerHTML = "";
  const commits = commitsRes.commits || [];
  if (commits.length === 0) {
    els.commitsList.innerHTML = `<div class="muted-empty">Nenhum commit criado ainda.</div>`;
    return;
  }

  for (const c of commits) {
    const isHead = c.id === state.headCommitId;
    const card = document.createElement("div");
    card.className = "commit-card" + (isHead ? " head" : "");

    const filesHtml = (c.files || []).length
      ? `<ul>${c.files.map((f) => `<li>${escapeHtml(f.path)} <span class="commit-hash">(${f.blobHash.slice(0, 8)})</span></li>`).join("")}</ul>`
      : `<div class="muted-empty">Snapshot vazio</div>`;

    card.innerHTML = `
      <div class="commit-top">
        <span class="commit-hash" title="${c.id}">#${c.id.slice(0, 10)}</span>
        ${isHead ? `<span class="pill head">HEAD</span>` : ""}
      </div>
      <div class="commit-msg">${escapeHtml(c.message)}</div>
      <div class="commit-meta">
        <span>${new Date(c.createdAt).toLocaleString("pt-BR")}</span>
        <span>${(c.files || []).length} nota(s) · ${(c.folders || []).length} pasta(s)</span>
      </div>
      <details class="commit-files">
        <summary>Ver arquivos do snapshot</summary>
        ${filesHtml}
      </details>
      <div class="commit-actions">
        <span></span>
        <button class="btn small" data-restore="${c.id}">Restaurar versão</button>
      </div>
    `;

    card.querySelector("[data-restore]").addEventListener("click", () => restoreCommit(c));
    els.commitsList.appendChild(card);
  }
}

async function createCommit() {
  if (state.dirty) {
    const saveFirst = confirm("A nota aberta tem alterações não salvas. Deseja salvá-la antes de criar o commit?");
    if (saveFirst) {
      await saveCurrentNote();
    }
  }
  const message = els.commitMessage.value.trim();
  if (!message) {
    showToast("Digite uma mensagem para o commit.", true);
    els.commitMessage.focus();
    return;
  }
  try {
    const commit = await api("/api/versioning/commits", {
      method: "POST",
      body: JSON.stringify({ message }),
    });
    els.commitMessage.value = "";
    showToast(`Commit #${commit.id.slice(0, 8)} criado!`);
    await refreshVersioning();
  } catch (err) {
    showToast(err.message, true);
  }
}

async function restoreCommit(commit) {
  const warn = state.repoDirty || state.dirty
    ? "\n\nATENÇÃO: existem alterações não commitadas que serão substituídas."
    : "";
  if (!confirm(`Restaurar todas as notas para o commit #${commit.id.slice(0, 8)} ("${commit.message}")?${warn}`)) {
    return;
  }
  try {
    await api("/api/versioning/restore", {
      method: "POST",
      body: JSON.stringify({ commitId: commit.id }),
    });
    const prevPath = state.currentPath;
    setEditorDirty(false);
    await refreshAll();

    if (prevPath) {
      try {
        await openNote(prevPath);
      } catch {
        closeEditor();
      }
    }
    showToast(`Workspace restaurado para #${commit.id.slice(0, 8)}`);
  } catch (err) {
    showToast(err.message, true);
  }
}

async function refreshAll() {
  await Promise.all([refreshTree(), refreshVersioning()]);
}

function escapeHtml(str) {
  return String(str)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

// Event bindings
document.getElementById("btn-new-note").addEventListener("click", () => createNotePrompt(""));
document.getElementById("btn-empty-new-note").addEventListener("click", () => createNotePrompt(""));
document.getElementById("btn-new-folder").addEventListener("click", () => createFolderPrompt(""));
document.getElementById("btn-empty-new-folder").addEventListener("click", () => createFolderPrompt(""));
document.getElementById("btn-save").addEventListener("click", () => saveCurrentNote());
document.getElementById("btn-save-as").addEventListener("click", saveAsCurrentNote);
document.getElementById("btn-rename").addEventListener("click", renameCurrentNote);
document.getElementById("btn-delete-note").addEventListener("click", () => {
  if (state.currentPath) deleteNote(state.currentPath);
});
document.getElementById("btn-commit").addEventListener("click", createCommit);
els.commitMessage.addEventListener("keydown", (e) => {
  if (e.key === "Enter") createCommit();
});

els.noteEditor.addEventListener("input", () => {
  setEditorDirty(els.noteEditor.value !== state.savedContent);
  updateStats();
});

window.addEventListener("keydown", (e) => {
  if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === "s") {
    if (state.currentPath) {
      e.preventDefault();
      saveCurrentNote();
    }
  }
});

refreshAll().catch((err) => showToast(err.message, true));
