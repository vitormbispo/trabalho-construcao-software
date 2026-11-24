# Documentação da API Pública — `libanedota`

A biblioteca **ANEDOTA** (`libanedota.a`) fornece operações de versionamento local desacopladas da CLI, permitindo que qualquer software hospedeiro versione dados genéricos (fluxos de bytes em memória ou arquivos em disco) utilizando exclusivamente a camada de abstração de plataforma (PAL).

---

## Cabeçalho Público e Compilação

Inclua o cabeçalho consolidado:

```c
#include <anedota/anedota.h>
```

(Alternativamente, `#include <anedota.h>` redireciona para `<anedota/anedota.h>`.)

Esse cabeçalho expõe todos os módulos públicos sem nenhuma dependência da CLI (`commands.h` não faz parte da biblioteca):

- `anedota/hash.h` — Cálculo de hash SHA-256 (bytes em memória e arquivos PAL)
- `anedota/storage.h` — Inicialização do armazenamento, escrita e leitura de Blobs
- `anedota/tree.h` — Criação e leitura de objetos Tree (em memória e recursivo por diretório)
- `anedota/commit.h` — Criação e leitura de objetos Commit
- `anedota/ref.h` — Leitura e atualização da referência atual (`HEAD` / branches)
- `anedota/log.h` — Consulta ao histórico de commits
- `anedota/diff.h` — Comparação entre duas versões (Commits ou Trees)

### Compilando a biblioteca

```sh
make lib
```

Gera `build/libanedota.a` (apenas os módulos do núcleo + PAL, sem `main.o` nem `command_*.o`).

### Vinculando em um software hospedeiro (C)

```sh
clang -std=c99 -Iinclude meu_app.c build/libanedota.a -o meu_app
```

---

## Convenções Gerais

- **Identificadores (hashes):** strings hexadecimais minúsculas de 64 caracteres terminadas em `'\0'` (`ANEDOTA_SHA256_HEX_SIZE == 65`).
- **Retornos:** `0` indica sucesso; valores negativos (`-1` ou códigos `anedota_diff_status_t`) indicam erro. Em caso de erro, parâmetros de saída (`hash_out`, `*_out`) não são modificados.
- **Gestão de memória:** todas as funções de leitura que retornam ponteiros alocados (`anedota_blob_read`, `anedota_tree_read`, `anedota_commit_read`, `anedota_log_read`, `anedota_log_read_from`, `anedota_diff_versions`) devolvem uma **única alocação** que deve ser liberada pelo chamador com `free()`.
- **Agnosticismo de dados:** o núcleo trata Blobs como sequências arbitrárias de bytes (inclusive contendo bytes NUL `0x00`), sem interpretar o formato ou significado do conteúdo.

---

## Referência das Operações

### 1. Inicializar armazenamento (`anedota/storage.h`)

```c
int anedota_storage_init(const char *root_path);
```

Cria a estrutura de diretórios `<root_path>/objetos/` e `<root_path>/referencias/branches/`.
- **Idempotente:** se a estrutura já existir, preserva os dados existentes e cria apenas diretórios ausentes.
- **Retorno:** `0` em sucesso; `-1` se `root_path` for inválido ou ocorrer erro de E/S.

---

### 2. Hash SHA-256 (`anedota/hash.h`)

```c
int anedota_sha256_bytes(const void *data, size_t size,
                         char output[ANEDOTA_SHA256_HEX_SIZE]);

int anedota_sha256_file(pal_file_t *file,
                        char output[ANEDOTA_SHA256_HEX_SIZE]);
```

- `anedota_sha256_bytes`: calcula o SHA-256 de um buffer genérico em memória (`data` pode ser `NULL` quando `size == 0`).
- `anedota_sha256_file`: calcula o SHA-256 dos bytes restantes de um `pal_file_t` já aberto em modo binário (a posse do arquivo permanece com o chamador).
- **Retorno:** `0` em sucesso; `-1` em erro.

---

### 3. Armazenar e recuperar Blobs (`anedota/storage.h`)

```c
int anedota_blob_write(const char *root_path, const void *data, size_t size,
                       char hash_out[ANEDOTA_SHA256_HEX_SIZE]);

int anedota_blob_write_file(const char *root_path, const char *source_path,
                            char hash_out[ANEDOTA_SHA256_HEX_SIZE]);

int anedota_blob_read(const char *root_path, const char *hash,
                      char **data_out, size_t *size_out);
```

- `anedota_blob_write`: grava um fluxo de bytes genérico em memória em `<root_path>/objetos/<hash>`. Deduplica automaticamente se o objeto já existir.
- `anedota_blob_write_file`: grava o conteúdo bruto de um arquivo em disco como Blob.
- `anedota_blob_read`: lê o Blob identificado por `hash`, aloca `*data_out` com os bytes exatos (mais um `'\0'` extra ao final) e grava o tamanho exato em `*size_out`. O chamador deve chamar `free(*data_out)`.
- **Retorno:** `0` em sucesso; `-1` se o armazenamento não estiver inicializado, hash inválido/inexistente ou erro de E/S.

---

### 4. Objetos Tree (`anedota/tree.h`)

```c
typedef enum
{
    ANEDOTA_TREE_ENTRY_BLOB = 1,
    ANEDOTA_TREE_ENTRY_TREE = 2
} anedota_tree_entry_type_t;

typedef struct
{
    const char *name;
    anedota_tree_entry_type_t type;
    const char *hash;
} anedota_tree_entry_t;

typedef struct
{
    size_t count;
    anedota_tree_entry_t entries[];
} anedota_tree_t;

int anedota_tree_write(const char *root_path,
                       const anedota_tree_entry_t *entries, size_t count,
                       char hash_out[ANEDOTA_SHA256_HEX_SIZE]);

int anedota_tree_read(const char *root_path, const char *hash,
                      anedota_tree_t **tree_out);

int anedota_tree_write_directory(const char *root_path, const char *dir_path,
                                 char hash_out[ANEDOTA_SHA256_HEX_SIZE]);
```

- `anedota_tree_write`: serializa entradas em memória de forma determinística (ordenadas lexicograficamente por `name`). Todos os objetos referenciados devem existir previamente em `objetos/`. Nomes duplicados, vazios, `.`, `..` ou contendo `/` são rejeitados.
- `anedota_tree_read`: lê e valida uma Tree, retornando uma única alocação em `*tree_out` (liberar com `free(*tree_out)`).
- `anedota_tree_write_directory`: percorre recursivamente `dir_path` via PAL, gravando arquivos como Blobs e subdiretórios como Trees, ignorando `.anedota/`.

---

### 5. Objetos Commit e Referências (`anedota/commit.h`, `anedota/ref.h`)

```c
typedef struct
{
    const char *tree_hash;
    const char *parent_hash; /* NULL para o primeiro commit */
    const char *author;
    const char *timestamp;
    const char *message;
} anedota_commit_t;

int anedota_commit_write(const char *root_path,
                         const anedota_commit_t *commit,
                         char hash_out[ANEDOTA_SHA256_HEX_SIZE]);

int anedota_commit_read(const char *root_path, const char *hash,
                        anedota_commit_t **commit_out);

int anedota_ref_read_head(const char *root_path,
                          char hash_out[ANEDOTA_SHA256_HEX_SIZE]);

int anedota_ref_update_head(const char *root_path, const char *commit_hash);
```

- `anedota_commit_write`: valida que `tree_hash` é uma Tree válida, que `parent_hash` (quando informado) é um Commit válido e que `author`, `timestamp` e `message` são não vazios; grava o Commit e atualiza `HEAD` automaticamente.
- `anedota_commit_read`: lê e valida o Commit em uma única alocação (`free(*commit_out)`).
- `anedota_ref_read_head`: retorna `0` se ainda não houver commits (`hash_out[0] = '\0'`), `1` se houver histórico (preenchendo `hash_out`) e `-1` em caso de erro.
- `anedota_ref_update_head`: atualiza a referência apontada por `HEAD` (branch simbólica ou detached HEAD) para `commit_hash`.

---

### 6. Consultar histórico (`anedota/log.h`)

```c
typedef struct
{
    char hash[ANEDOTA_SHA256_HEX_SIZE];
    const char *tree_hash;
    const char *parent_hash;
    const char *author;
    const char *timestamp;
    const char *message;
} anedota_log_entry_t;

typedef struct
{
    size_t count;
    anedota_log_entry_t entries[];
} anedota_log_t;

int anedota_log_read(const char *root_path, anedota_log_t **log_out);

int anedota_log_read_from(const char *root_path,
                          const char *start_commit_hash,
                          anedota_log_t **log_out);
```

- `anedota_log_read`: inicia no Commit apontado por `HEAD` e percorre a cadeia de `parent_hash` do mais recente ao mais antigo. Se o armazenamento ainda não possuir commits, retorna `0` com `(*log_out)->count == 0`.
- `anedota_log_read_from`: percorre o histórico a partir de um Commit específico.
- Detecta ciclos e commits/referências corrompidas retornando `-1`. Liberar com `free(*log_out)`.

---

### 7. Comparar versões (`anedota/diff.h`)

```c
typedef enum
{
    ANEDOTA_DIFF_OK = 0,
    ANEDOTA_DIFF_ERR_INVALID_ARG = -1,
    ANEDOTA_DIFF_ERR_UNINITIALIZED = -2,
    ANEDOTA_DIFF_ERR_VERSION_NOT_FOUND = -3,
    ANEDOTA_DIFF_ERR_INVALID_VERSION = -4,
    ANEDOTA_DIFF_ERR_IO = -5
} anedota_diff_status_t;

typedef enum
{
    ANEDOTA_DIFF_ADDED = 1,
    ANEDOTA_DIFF_REMOVED = 2,
    ANEDOTA_DIFF_MODIFIED = 3
} anedota_diff_kind_t;

typedef struct
{
    const char *path;
    anedota_diff_kind_t kind;
    anedota_tree_entry_type_t entry_type;
    const char *old_hash; /* NULL quando ADDED */
    const char *new_hash; /* NULL quando REMOVED */
} anedota_diff_entry_t;

typedef struct
{
    size_t count;
    anedota_diff_entry_t entries[];
} anedota_diff_t;

int anedota_diff_versions(const char *root_path,
                          const char *from_version,
                          const char *to_version,
                          anedota_diff_t **diff_out);

const char *anedota_diff_status_message(int status);
```

- Aceita tanto hashes de **Commit** quanto hashes de **Tree** em `from_version` e `to_version`.
- Percorre as Trees recursivamente via PAL e classifica cada item como `ANEDOTA_DIFF_ADDED`, `ANEDOTA_DIFF_REMOVED` ou `ANEDOTA_DIFF_MODIFIED` em ordem determinística de caminho.
- Versões iguais retornam `ANEDOTA_DIFF_OK` com `(*diff_out)->count == 0`.
- Versão ausente retorna `ANEDOTA_DIFF_ERR_VERSION_NOT_FOUND`; hash malformado ou objeto que não seja Commit/Tree retorna `ANEDOTA_DIFF_ERR_INVALID_VERSION`.
- Liberar com `free(*diff_out)`.

---

## Exemplo Mínimo de Integração (C)

O programa completo está disponível em [`examples/host_app.c`](../examples/host_app.c) e compila com `make examples`. Abaixo está um resumo mínimo mostrando todo o fluxo em memória sem chamar a CLI:

```c
#include <anedota/anedota.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    const char *repo = "/tmp/meu-app-anedota";

    /* 1. Inicializar */
    if (anedota_storage_init(repo) != 0) return 1;

    /* 2. Armazenar bytes genéricos em memória */
    const char *v1 = "estado inicial";
    const char *v2 = "estado atualizado";
    char b1[ANEDOTA_SHA256_HEX_SIZE], b2[ANEDOTA_SHA256_HEX_SIZE];
    if (anedota_blob_write(repo, v1, strlen(v1), b1) != 0) return 1;
    if (anedota_blob_write(repo, v2, strlen(v2), b2) != 0) return 1;

    /* 3. Recuperar dados */
    char *buf = NULL;
    size_t sz = 0;
    if (anedota_blob_read(repo, b1, &buf, &sz) != 0) return 1;
    printf("Recuperado (%zu bytes): %s\n", sz, buf);
    free(buf);

    /* 4. Criar Trees e Commits */
    anedota_tree_entry_t e1[] = { { "dados.txt", ANEDOTA_TREE_ENTRY_BLOB, b1 } };
    anedota_tree_entry_t e2[] = { { "dados.txt", ANEDOTA_TREE_ENTRY_BLOB, b2 } };
    char t1[ANEDOTA_SHA256_HEX_SIZE], t2[ANEDOTA_SHA256_HEX_SIZE];
    char c1[ANEDOTA_SHA256_HEX_SIZE], c2[ANEDOTA_SHA256_HEX_SIZE];

    anedota_tree_write(repo, e1, 1, t1);
    anedota_commit_t cm1 = { t1, NULL, "App", "2026-11-24T19:00:00Z", "Versao 1" };
    anedota_commit_write(repo, &cm1, c1);

    anedota_tree_write(repo, e2, 1, t2);
    anedota_commit_t cm2 = { t2, c1, "App", "2026-11-24T19:05:00Z", "Versao 2" };
    anedota_commit_write(repo, &cm2, c2);

    /* 5. Consultar histórico */
    anedota_log_t *log = NULL;
    if (anedota_log_read(repo, &log) == 0)
    {
        for (size_t i = 0; i < log->count; ++i)
            printf("%.12s - %s\n", log->entries[i].hash, log->entries[i].message);
        free(log);
    }

    /* 6. Comparar versões */
    anedota_diff_t *diff = NULL;
    if (anedota_diff_versions(repo, c1, c2, &diff) == ANEDOTA_DIFF_OK)
    {
        for (size_t i = 0; i < diff->count; ++i)
            printf("diff kind=%d path=%s\n", diff->entries[i].kind, diff->entries[i].path);
        free(diff);
    }
    return 0;
}
```

---

## Exemplo Real de Software Hospedeiro (`../anedota-notas`)

A aplicação web de anotações pessoais **`anedota-notas`** (localizada no diretório irmão `../anedota-notas`, escrita em Go) demonstra a integração real da biblioteca `libanedota.a` via **CGO** (build tag `-tags anedota`), **sem executar a CLI `anedota`**:

- Vincula diretamente `../trabalho-construcao-software/build/libanedota.a` e inclui `<anedota/anedota.h>`.
- Utiliza `anedota_storage_init`, `anedota_blob_write_file`, `anedota_blob_read`, `anedota_tree_write`, `anedota_tree_read`, `anedota_commit_write`, `anedota_commit_read`, `anedota_ref_read_head`, `anedota_ref_update_head`, `anedota_log_read`, `anedota_diff_versions` e `anedota_sha256_bytes`.
- Para compilar e validar os testes de integração no software hospedeiro:

```sh
cd ../anedota-notas
make test-cgo
make run-cgo
```
