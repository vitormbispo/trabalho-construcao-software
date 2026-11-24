#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#endif

#include <anedota/commit.h>
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

static void write_text(const char *path, const char *text)
{
    pal_file_t *file = pal_open(path, "wb");
    assert(file != NULL);
    const size_t length = strlen(text);
    if (length > 0)
    {
        assert(pal_write(file, text, length) == (int)length);
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

static void test_head_empty_and_auto_updated_on_commit(void)
{
    char workspace[] = "/tmp/anedota-ref-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    char head_path[512];
    char branch_path[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    join_path(head_path, sizeof(head_path), root, "HEAD");
    join_path(branch_path, sizeof(branch_path), root, "referencias/branches/main");
    assert(anedota_storage_init(root) == 0);

    char current[ANEDOTA_SHA256_HEX_SIZE] = "initial";
    assert(anedota_ref_read_head(root, current) == 0);
    assert(current[0] == '\0');

    char tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, NULL, 0, tree_hash) == 0);

    anedota_commit_t first = {
        tree_hash, NULL, "Vitor", "2026-11-24T16:40:00Z", "Primeiro"
    };
    char first_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_commit_write(root, &first, first_hash) == 0);
    assert(pal_file_exists(head_path) == 1);
    assert(pal_file_exists(branch_path) == 1);
    assert(anedota_ref_read_head(root, current) == 1);
    assert(strcmp(current, first_hash) == 0);

    anedota_commit_t second = {
        tree_hash, first_hash, "Vitor", "2026-11-24T16:41:00Z", "Segundo"
    };
    char second_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_commit_write(root, &second, second_hash) == 0);
    assert(anedota_ref_read_head(root, current) == 1);
    assert(strcmp(current, second_hash) == 0);

    assert(anedota_ref_update_head(root, first_hash) == 0);
    assert(anedota_ref_read_head(root, current) == 1);
    assert(strcmp(current, first_hash) == 0);

    remove_tree_recursive(workspace);
    printf("[PASS] test_head_empty_and_auto_updated_on_commit\n");
}

static void test_head_invalid_and_corrupt_states(void)
{
    char workspace[] = "/tmp/anedota-ref-inv-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    char uninit[512];
    char head_path[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    join_path(uninit, sizeof(uninit), workspace, "sem_repo");
    join_path(head_path, sizeof(head_path), root, "HEAD");
    assert(anedota_storage_init(root) == 0);

    char tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, NULL, 0, tree_hash) == 0);

    char unchanged[ANEDOTA_SHA256_HEX_SIZE] = "unchanged";
    const char *missing =
        "0000000000000000000000000000000000000000000000000000000000000000";

    assert(anedota_ref_read_head(NULL, unchanged) == -1);
    assert(anedota_ref_read_head("", unchanged) == -1);
    assert(anedota_ref_read_head(uninit, unchanged) == -1);
    assert(anedota_ref_read_head(root, NULL) == -1);
    assert(strcmp(unchanged, "unchanged") == 0);

    assert(anedota_ref_update_head(NULL, missing) == -1);
    assert(anedota_ref_update_head(uninit, missing) == -1);
    assert(anedota_ref_update_head(root, NULL) == -1);
    assert(anedota_ref_update_head(root, "invalido") == -1);
    assert(anedota_ref_update_head(root, missing) == -1);
    assert(anedota_ref_update_head(root, tree_hash) == -1);

    write_text(head_path, "ref: ../escape\n");
    assert(anedota_ref_read_head(root, unchanged) == -1);

    write_text(head_path, "conteudo invalido\n");
    assert(anedota_ref_read_head(root, unchanged) == -1);
    assert(strcmp(unchanged, "unchanged") == 0);

    remove_tree_recursive(workspace);
    printf("[PASS] test_head_invalid_and_corrupt_states\n");
}

int main(void)
{
    printf("Executando testes de referencias...\n");
    test_head_empty_and_auto_updated_on_commit();
    test_head_invalid_and_corrupt_states();
    printf("Testes de referencias concluídos com sucesso\n");
    return 0;
}
