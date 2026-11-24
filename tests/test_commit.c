#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#endif

#include <anedota/commit.h>
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

static void create_empty_tree(const char *root,
                              char hash_out[ANEDOTA_SHA256_HEX_SIZE])
{
    assert(anedota_tree_write(root, NULL, 0, hash_out) == 0);
}

static void test_commit_write_and_read_chain(void)
{
    char workspace[] = "/tmp/anedota-commit-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    assert(anedota_storage_init(root) == 0);

    char tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    create_empty_tree(root, tree_hash);

    anedota_commit_t first_input = {
        tree_hash, NULL, "Vitor", "2026-11-24T16:30:00Z", "Commit inicial"
    };
    char first_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_commit_write(root, &first_input, first_hash) == 0);

    anedota_commit_t *first_read = NULL;
    assert(anedota_commit_read(root, first_hash, &first_read) == 0);
    assert(first_read != NULL);
    assert(strcmp(first_read->tree_hash, tree_hash) == 0);
    assert(first_read->parent_hash == NULL);
    assert(strcmp(first_read->author, "Vitor") == 0);
    assert(strcmp(first_read->timestamp, "2026-11-24T16:30:00Z") == 0);
    assert(strcmp(first_read->message, "Commit inicial") == 0);
    free(first_read);

    anedota_commit_t second_input = {
        tree_hash, first_hash, "Vitor", "2026-11-24T16:35:00Z", "Segundo commit"
    };
    char second_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_commit_write(root, &second_input, second_hash) == 0);
    assert(strcmp(first_hash, second_hash) != 0);

    anedota_commit_t *second_read = NULL;
    assert(anedota_commit_read(root, second_hash, &second_read) == 0);
    assert(second_read != NULL);
    assert(strcmp(second_read->tree_hash, tree_hash) == 0);
    assert(second_read->parent_hash != NULL);
    assert(strcmp(second_read->parent_hash, first_hash) == 0);
    assert(strcmp(second_read->message, "Segundo commit") == 0);
    free(second_read);

    remove_tree_recursive(workspace);
    printf("[PASS] test_commit_write_and_read_chain\n");
}

static void test_commit_rejects_invalid_tree_and_inputs(void)
{
    char workspace[] = "/tmp/anedota-commit-inv-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    assert(anedota_storage_init(root) == 0);

    char tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    create_empty_tree(root, tree_hash);

    const char *missing =
        "0000000000000000000000000000000000000000000000000000000000000000";
    char unchanged[ANEDOTA_SHA256_HEX_SIZE] = "unchanged";

    anedota_commit_t bad_tree = {
        missing, NULL, "Vitor", "2026-11-24T16:30:00Z", "msg"
    };
    anedota_commit_t bad_parent = {
        tree_hash, missing, "Vitor", "2026-11-24T16:30:00Z", "msg"
    };
    anedota_commit_t empty_msg = {
        tree_hash, NULL, "Vitor", "2026-11-24T16:30:00Z", ""
    };

    assert(anedota_commit_write(root, &bad_tree, unchanged) == -1);
    assert(anedota_commit_write(root, &bad_parent, unchanged) == -1);
    assert(anedota_commit_write(root, &empty_msg, unchanged) == -1);
    assert(strcmp(unchanged, "unchanged") == 0);

    anedota_commit_t *out = (anedota_commit_t *)1;
    assert(anedota_commit_read(root, missing, &out) == -1);
    assert(anedota_commit_read(root, tree_hash, &out) == -1);
    assert(out == (anedota_commit_t *)1);

    remove_tree_recursive(workspace);
    printf("[PASS] test_commit_rejects_invalid_tree_and_inputs\n");
}

int main(void)
{
    printf("Executando testes de Commit...\n");
    test_commit_write_and_read_chain();
    test_commit_rejects_invalid_tree_and_inputs();
    printf("Testes de Commit concluídos com sucesso\n");
    return 0;
}
