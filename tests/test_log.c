#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#endif

#include <anedota/commit.h>
#include <anedota/log.h>
#include <anedota/ref.h>
#include <anedota/storage.h>
#include <anedota/tree.h>
#include <assert.h>
#include <pal/pal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void join_path(char *out, size_t out_size,
                      const char *base, const char *relative)
{
    assert(snprintf(out, out_size, "%s/%s", base, relative) < (int)out_size);
}

static void write_bytes(const char *path, const void *data, size_t size)
{
    pal_file_t *file = pal_open(path, "wb");
    assert(file != NULL);
    if (size > 0)
    {
        assert(pal_write(file, data, size) == (int)size);
    }
    assert(pal_close(file) == 0);
}

static void remove_tree_recursive(const char *path)
{
    pal_directory_t *dir = pal_dir_open(path);
    if (!dir)
    {
        unlink(path);
        return;
    }

    const char *name = NULL;
    pal_dir_entry_type_t type;
    int status;
    while ((status = pal_dir_next(dir, &name, &type)) == 1)
    {
        char child[1024];
        join_path(child, sizeof(child), path, name);
        if (type == PAL_DIR_ENTRY_DIRECTORY)
            remove_tree_recursive(child);
        else
            assert(unlink(child) == 0);
    }
    assert(status == 0);
    assert(pal_dir_close(dir) == 0);
    assert(rmdir(path) == 0);
}

static void test_log_empty_storage(void)
{
    char workspace[] = "/tmp/anedota-log-empty-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    assert(anedota_storage_init(root) == 0);

    anedota_log_t *log = NULL;
    assert(anedota_log_read(root, &log) == 0);
    assert(log != NULL);
    assert(log->count == 0);
    free(log);

    remove_tree_recursive(workspace);
    printf("[PASS] test_log_empty_storage\n");
}

static void test_log_single_commit(void)
{
    char workspace[] = "/tmp/anedota-log-single-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    assert(anedota_storage_init(root) == 0);

    char tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, NULL, 0, tree_hash) == 0);

    anedota_commit_t c1_in = {
        tree_hash, NULL, "Vitor", "2026-11-24T19:00:00Z", "Commit unico"
    };
    char c1_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_commit_write(root, &c1_in, c1_hash) == 0);

    anedota_log_t *log = NULL;
    assert(anedota_log_read(root, &log) == 0);
    assert(log != NULL);
    assert(log->count == 1);
    assert(strcmp(log->entries[0].hash, c1_hash) == 0);
    assert(strcmp(log->entries[0].tree_hash, tree_hash) == 0);
    assert(log->entries[0].parent_hash == NULL);
    assert(strcmp(log->entries[0].author, "Vitor") == 0);
    assert(strcmp(log->entries[0].timestamp, "2026-11-24T19:00:00Z") == 0);
    assert(strcmp(log->entries[0].message, "Commit unico") == 0);
    free(log);

    remove_tree_recursive(workspace);
    printf("[PASS] test_log_single_commit\n");
}

static void test_log_multiple_commits_order(void)
{
    char workspace[] = "/tmp/anedota-log-multi-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    assert(anedota_storage_init(root) == 0);

    char blob1[ANEDOTA_SHA256_HEX_SIZE];
    char blob2[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_blob_write(root, "v1", 2, blob1) == 0);
    assert(anedota_blob_write(root, "v2", 2, blob2) == 0);

    anedota_tree_entry_t e1[] = { { "a.txt", ANEDOTA_TREE_ENTRY_BLOB, blob1 } };
    anedota_tree_entry_t e2[] = { { "a.txt", ANEDOTA_TREE_ENTRY_BLOB, blob2 } };
    char t1[ANEDOTA_SHA256_HEX_SIZE];
    char t2[ANEDOTA_SHA256_HEX_SIZE];
    char t3[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, e1, 1, t1) == 0);
    assert(anedota_tree_write(root, e2, 1, t2) == 0);
    assert(anedota_tree_write(root, NULL, 0, t3) == 0);

    char c1[ANEDOTA_SHA256_HEX_SIZE];
    char c2[ANEDOTA_SHA256_HEX_SIZE];
    char c3[ANEDOTA_SHA256_HEX_SIZE];

    anedota_commit_t in1 = { t1, NULL, "Vitor", "2026-11-24T19:01:00Z", "Primeiro" };
    assert(anedota_commit_write(root, &in1, c1) == 0);

    anedota_commit_t in2 = { t2, c1, "Bruno", "2026-11-24T19:02:00Z", "Segundo" };
    assert(anedota_commit_write(root, &in2, c2) == 0);

    anedota_commit_t in3 = { t3, c2, "Julio", "2026-11-24T19:03:00Z", "Terceiro" };
    assert(anedota_commit_write(root, &in3, c3) == 0);

    anedota_log_t *log = NULL;
    assert(anedota_log_read(root, &log) == 0);
    assert(log != NULL);
    assert(log->count == 3);

    /* Ordem esperada: mais recente -> mais antigo (c3, c2, c1) */
    assert(strcmp(log->entries[0].hash, c3) == 0);
    assert(strcmp(log->entries[0].tree_hash, t3) == 0);
    assert(strcmp(log->entries[0].parent_hash, c2) == 0);
    assert(strcmp(log->entries[0].author, "Julio") == 0);
    assert(strcmp(log->entries[0].message, "Terceiro") == 0);

    assert(strcmp(log->entries[1].hash, c2) == 0);
    assert(strcmp(log->entries[1].tree_hash, t2) == 0);
    assert(strcmp(log->entries[1].parent_hash, c1) == 0);
    assert(strcmp(log->entries[1].author, "Bruno") == 0);
    assert(strcmp(log->entries[1].message, "Segundo") == 0);

    assert(strcmp(log->entries[2].hash, c1) == 0);
    assert(strcmp(log->entries[2].tree_hash, t1) == 0);
    assert(log->entries[2].parent_hash == NULL);
    assert(strcmp(log->entries[2].author, "Vitor") == 0);
    assert(strcmp(log->entries[2].message, "Primeiro") == 0);
    free(log);

    /* Consulta a partir de commit intermediário */
    anedota_log_t *partial = NULL;
    assert(anedota_log_read_from(root, c2, &partial) == 0);
    assert(partial->count == 2);
    assert(strcmp(partial->entries[0].hash, c2) == 0);
    assert(strcmp(partial->entries[1].hash, c1) == 0);
    free(partial);

    remove_tree_recursive(workspace);
    printf("[PASS] test_log_multiple_commits_order\n");
}

static void test_log_error_cases(void)
{
    char workspace[] = "/tmp/anedota-log-err-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    char uninit[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    join_path(uninit, sizeof(uninit), workspace, "nao_existe");
    assert(anedota_storage_init(root) == 0);

    anedota_log_t *sentinel = (anedota_log_t *)1;
    assert(anedota_log_read(NULL, &sentinel) == -1);
    assert(anedota_log_read("", &sentinel) == -1);
    assert(anedota_log_read(root, NULL) == -1);
    assert(anedota_log_read(uninit, &sentinel) == -1);
    assert(anedota_log_read_from(root, "invalido", &sentinel) == -1);
    assert(anedota_log_read_from(root,
        "0000000000000000000000000000000000000000000000000000000000000000",
        &sentinel) == -1);
    assert(sentinel == (anedota_log_t *)1);

    /* Cria cadeia de 2 commits e apaga o commit pai para simular corrupção */
    char t1[ANEDOTA_SHA256_HEX_SIZE];
    char c1[ANEDOTA_SHA256_HEX_SIZE];
    char c2[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, NULL, 0, t1) == 0);
    anedota_commit_t in1 = { t1, NULL, "Vitor", "2026-11-24T19:01:00Z", "C1" };
    assert(anedota_commit_write(root, &in1, c1) == 0);
    anedota_commit_t in2 = { t1, c1, "Vitor", "2026-11-24T19:02:00Z", "C2" };
    assert(anedota_commit_write(root, &in2, c2) == 0);

    char c1_obj_path[512];
    char obj_rel[128];
    assert(snprintf(obj_rel, sizeof(obj_rel), "objetos/%s", c1) < (int)sizeof(obj_rel));
    join_path(c1_obj_path, sizeof(c1_obj_path), root, obj_rel);
    assert(pal_file_delete(c1_obj_path) == 0);

    assert(anedota_log_read(root, &sentinel) == -1);
    assert(sentinel == (anedota_log_t *)1);

    /* HEAD corrompido */
    char head_path[512];
    join_path(head_path, sizeof(head_path), root, "HEAD");
    write_bytes(head_path, "conteudo invalido\n", 18);
    assert(anedota_log_read(root, &sentinel) == -1);
    assert(sentinel == (anedota_log_t *)1);

    remove_tree_recursive(workspace);
    printf("[PASS] test_log_error_cases\n");
}

int main(void)
{
    printf("Executando testes de historico (log)...\n");
    test_log_empty_storage();
    test_log_single_commit();
    test_log_multiple_commits_order();
    test_log_error_cases();
    printf("Testes de historico concluídos com sucesso\n");
    return 0;
}
