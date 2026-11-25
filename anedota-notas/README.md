# Anedota Notas

Aplicação web em Go de anotações pessoais construída como **software hospedeiro real** para integrar e exemplificar a biblioteca C **`libanedota.a`** (`..`), **sem executar a CLI `anedota`**.

## Funcionalidades

- **Escrita e edição básica** de páginas de anotação (`.md`, `.txt`, etc.) via interface web local.
- **Organização em pastas** (criação de pastas/subpastas e exclusão).
- **Gerenciamento de ficheiros**: criar novas notas, editar, salvar em diferentes ficheiros (`Salvar como...`), renomear/mover entre pastas e excluir.
- **Versionamento nativo via `libanedota.a`** (`-tags anedota`):
  - Inicialização automática do repositório `.anedota/` (`anedota_storage_init`);
  - Armazenamento e recuperação de Blobs SHA-256 (`anedota_blob_write_file`, `anedota_blob_read`);
  - Montagem e leitura de árvores de diretórios/notas (`anedota_tree_write`, `anedota_tree_read`);
  - Criação e leitura de Commits e atualização de `HEAD` (`anedota_commit_write`, `anedota_commit_read`, `anedota_ref_read_head`, `anedota_ref_update_head`);
  - Consulta de histórico completo (`anedota_log_read`);
  - Comparação entre versões (`anedota_diff_versions`, exposta também em `GET /api/versioning/diff?from=...&to=...`);
  - Restauração completa do estado das notas e pastas para qualquer commit anterior.

## Como executar

### 1. Modo integrado via CGO com `libanedota.a` (API C real do ANEDOTA)

Compila `../build/libanedota.a` e vincula diretamente pelo cabeçalho público `<anedota/anedota.h>`, sem invocar a CLI:

```bash
make run-cgo
# ou:
make -C .. lib
CGO_ENABLED=1 go run -tags anedota ./cmd/notas -addr 127.0.0.1:8080 -dir ./dados-notas
```

Acesse no navegador: `http://127.0.0.1:8080`

### 2. Modo fallback (Go puro)

Não exige compilação C; útil para desenvolvimento isolado da interface:

```bash
make run
```

## Testes

Rodar testes vinculando diretamente à **`libanedota.a`** via CGO:

```bash
make test-cgo
```

Rodar os testes no modo fallback em Go puro:

```bash
make test
```

## Arquitetura da integração com o ANEDOTA

Toda a integração está isolada no pacote `internal/versioning`:

- `internal/versioning/store.go`: define a interface `Repository` usada pelo serviço de notas (`Init`, `Commit`, `ListCommits`, `GetCommit`, `Diff`, `Restore`, `Status`).
- `internal/versioning/repo_anedota_cgo.go` (`-tags anedota`): implementação nativa que vincula `../build/libanedota.a` via CGO usando `<anedota/anedota.h>`, sem nenhum manifesto JSON temporário.
- `internal/versioning/blob_anedota_cgo.go` (`-tags anedota`): driver de Blobs sobre `libanedota.a`.
- `internal/versioning/repo_purego.go` e `internal/versioning/manifest.go` (`!anedota`): fallback em Go puro.
