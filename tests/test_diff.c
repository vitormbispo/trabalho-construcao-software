#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#endif

#include <anedota/commit.h>
#include <anedota/diff.h>
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

static void test_diff_added_removed_modified(void)
{
    char workspace[] = "/tmp/anedota-diff-changes-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    assert(anedota_storage_init(root) == 0);

    char b_igual[ANEDOTA_SHA256_HEX_SIZE];
    char b_mod_v1[ANEDOTA_SHA256_HEX_SIZE];
    char b_mod_v2[ANEDOTA_SHA256_HEX_SIZE];
    char b_rem[ANEDOTA_SHA256_HEX_SIZE];
    char b_add[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_blob_write(root, "igual", 5, b_igual) == 0);
    assert(anedota_blob_write(root, "versao 1", 8, b_mod_v1) == 0);
    assert(anedota_blob_write(root, "versao 2", 8, b_mod_v2) == 0);
    assert(anedota_blob_write(root, "sera removido", 13, b_rem) == 0);
    assert(anedota_blob_write(root, "foi adicionado", 14, b_add) == 0);

    /* Tree v1: igual.txt, remover.txt, sub/mod.txt */
    anedota_tree_entry_t sub1_e[] = {
        { "mod.txt", ANEDOTA_TREE_ENTRY_BLOB, b_mod_v1 }
    };
    char sub1[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, sub1_e, 1, sub1) == 0);

    anedota_tree_entry_t root1_e[] = {
        { "igual.txt", ANEDOTA_TREE_ENTRY_BLOB, b_igual },
        { "remover.txt", ANEDOTA_TREE_ENTRY_BLOB, b_rem },
        { "sub", ANEDOTA_TREE_ENTRY_TREE, sub1 }
    };
    char tree1[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, root1_e, 3, tree1) == 0);

    anedota_commit_t c1_in = { tree1, NULL, "Vitor", "2026-11-24T19:10:00Z", "V1" };
    char c1[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_commit_write(root, &c1_in, c1) == 0);

    /* Tree v2: igual.txt, novo.txt, sub/mod.txt (modificado), sub/extra.txt, vazia/ */
    char empty_tree[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, NULL, 0, empty_tree) == 0);

    anedota_tree_entry_t sub2_e[] = {
        { "extra.txt", ANEDOTA_TREE_ENTRY_BLOB, b_add },
        { "mod.txt", ANEDOTA_TREE_ENTRY_BLOB, b_mod_v2 }
    };
    char sub2[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, sub2_e, 2, sub2) == 0);

    anedota_tree_entry_t root2_e[] = {
        { "igual.txt", ANEDOTA_TREE_ENTRY_BLOB, b_igual },
        { "novo.txt", ANEDOTA_TREE_ENTRY_BLOB, b_add },
        { "sub", ANEDOTA_TREE_ENTRY_TREE, sub2 },
        { "vazia", ANEDOTA_TREE_ENTRY_TREE, empty_tree }
    };
    char tree2[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, root2_e, 4, tree2) == 0);

    anedota_commit_t c2_in = { tree2, c1, "Vitor", "2026-11-24T19:11:00Z", "V2" };
    char c2[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_commit_write(root, &c2_in, c2) == 0);

    anedota_diff_t *diff = NULL;
    assert(anedota_diff_versions(root, c1, c2, &diff) == ANEDOTA_DIFF_OK);
    assert(diff != NULL);
    assert(diff->count == 5);

    /* Ordem lexicográfica por caminho:
     * 0: novo.txt (ADDED)
     * 1: remover.txt (REMOVED)
     * 2: sub/extra.txt (ADDED)
     * 3: sub/mod.txt (MODIFIED)
     * 4: vazia (ADDED, TREE)
     */
    assert(strcmp(diff->entries[0].path, "novo.txt") == 0);
    assert(diff->entries[0].kind == ANEDOTA_DIFF_ADDED);
    assert(diff->entries[0].entry_type == ANEDOTA_TREE_ENTRY_BLOB);
    assert(diff->entries[0].old_hash == NULL);
    assert(strcmp(diff->entries[0].new_hash, b_add) == 0);

    assert(strcmp(diff->entries[1].path, "remover.txt") == 0);
    assert(diff->entries[1].kind == ANEDOTA_DIFF_REMOVED);
    assert(diff->entries[1].entry_type == ANEDOTA_TREE_ENTRY_BLOB);
    assert(strcmp(diff->entries[1].old_hash, b_rem) == 0);
    assert(diff->entries[1].new_hash == NULL);

    assert(strcmp(diff->entries[2].path, "sub/extra.txt") == 0);
    assert(diff->entries[2].kind == ANEDOTA_DIFF_ADDED);
    assert(diff->entries[2].old_hash == NULL);
    assert(strcmp(diff->entries[2].new_hash, b_add) == 0);

    assert(strcmp(diff->entries[3].path, "sub/mod.txt") == 0);
    assert(diff->entries[3].kind == ANEDOTA_DIFF_MODIFIED);
    assert(strcmp(diff->entries[3].old_hash, b_mod_v1) == 0);
    assert(strcmp(diff->entries[3].new_hash, b_mod_v2) == 0);

    assert(strcmp(diff->entries[4].path, "vazia") == 0);
    assert(diff->entries[4].kind == ANEDOTA_DIFF_ADDED);
    assert(diff->entries[4].entry_type == ANEDOTA_TREE_ENTRY_TREE);
    assert(diff->entries[4].old_hash == NULL);
    assert(strcmp(diff->entries[4].new_hash, empty_tree) == 0);
    free(diff);

    /* Comparação direta por hash de Tree deve produzir o mesmo resultado */
    anedota_diff_t *tree_diff = NULL;
    assert(anedota_diff_versions(root, tree1, tree2, &tree_diff) == ANEDOTA_DIFF_OK);
    assert(tree_diff != NULL);
    assert(tree_diff->count == 5);
    free(tree_diff);

    remove_tree_recursive(workspace);
    printf("[PASS] test_diff_added_removed_modified\n");
}

static void test_diff_equal_versions(void)
{
    char workspace[] = "/tmp/anedota-diff-equal-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    assert(anedota_storage_init(root) == 0);

    char blob[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_blob_write(root, "conteudo fixo", 13, blob) == 0);
    anedota_tree_entry_t e[] = { { "doc.txt", ANEDOTA_TREE_ENTRY_BLOB, blob } };
    char tree[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, e, 1, tree) == 0);

    anedota_commit_t c1_in = { tree, NULL, "Vitor", "2026-11-24T19:10:00Z", "C1" };
    char c1[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_commit_write(root, &c1_in, c1) == 0);

    anedota_commit_t c2_in = { tree, c1, "Vitor", "2026-11-24T19:11:00Z", "C2 igual" };
    char c2[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_commit_write(root, &c2_in, c2) == 0);

    anedota_diff_t *diff = NULL;
    assert(anedota_diff_versions(root, c1, c1, &diff) == ANEDOTA_DIFF_OK);
    assert(diff != NULL && diff->count == 0);
    free(diff);

    diff = NULL;
    assert(anedota_diff_versions(root, c1, c2, &diff) == ANEDOTA_DIFF_OK);
    assert(diff != NULL && diff->count == 0);
    free(diff);

    remove_tree_recursive(workspace);
    printf("[PASS] test_diff_equal_versions\n");
}

static void test_diff_type_change_blob_and_tree(void)
{
    char workspace[] = "/tmp/anedota-diff-type-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    assert(anedota_storage_init(root) == 0);

    char b1[ANEDOTA_SHA256_HEX_SIZE];
    char b2[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_blob_write(root, "era arquivo", 11, b1) == 0);
    assert(anedota_blob_write(root, "dentro da pasta", 15, b2) == 0);

    anedota_tree_entry_t t1_e[] = { { "item", ANEDOTA_TREE_ENTRY_BLOB, b1 } };
    char t1[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, t1_e, 1, t1) == 0);

    anedota_tree_entry_t sub_e[] = { { "filho.txt", ANEDOTA_TREE_ENTRY_BLOB, b2 } };
    char sub[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, sub_e, 1, sub) == 0);

    anedota_tree_entry_t t2_e[] = { { "item", ANEDOTA_TREE_ENTRY_TREE, sub } };
    char t2[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, t2_e, 1, t2) == 0);

    anedota_diff_t *diff = NULL;
    assert(anedota_diff_versions(root, t1, t2, &diff) == ANEDOTA_DIFF_OK);
    assert(diff != NULL && diff->count == 2);
    assert(strcmp(diff->entries[0].path, "item") == 0);
    assert(diff->entries[0].kind == ANEDOTA_DIFF_REMOVED);
    assert(strcmp(diff->entries[1].path, "item/filho.txt") == 0);
    assert(diff->entries[1].kind == ANEDOTA_DIFF_ADDED);
    free(diff);

    remove_tree_recursive(workspace);
    printf("[PASS] test_diff_type_change_blob_and_tree\n");
}

static void test_diff_errors_and_missing_versions(void)
{
    char workspace[] = "/tmp/anedota-diff-err-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    char uninit[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    join_path(uninit, sizeof(uninit), workspace, "sem_repo");
    assert(anedota_storage_init(root) == 0);

    char blob[ANEDOTA_SHA256_HEX_SIZE];
    char tree[ANEDOTA_SHA256_HEX_SIZE];
    char c1[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_blob_write(root, "blob puro", 9, blob) == 0);
    assert(anedota_tree_write(root, NULL, 0, tree) == 0);
    anedota_commit_t c1_in = { tree, NULL, "Vitor", "2026-11-24T19:15:00Z", "Base" };
    assert(anedota_commit_write(root, &c1_in, c1) == 0);

    const char *missing =
        "0000000000000000000000000000000000000000000000000000000000000000";
    anedota_diff_t *sentinel = (anedota_diff_t *)1;

    assert(anedota_diff_versions(NULL, c1, c1, &sentinel) == ANEDOTA_DIFF_ERR_INVALID_ARG);
    assert(anedota_diff_versions("", c1, c1, &sentinel) == ANEDOTA_DIFF_ERR_INVALID_ARG);
    assert(anedota_diff_versions(root, NULL, c1, &sentinel) == ANEDOTA_DIFF_ERR_INVALID_ARG);
    assert(anedota_diff_versions(root, c1, NULL, &sentinel) == ANEDOTA_DIFF_ERR_INVALID_ARG);
    assert(anedota_diff_versions(root, c1, c1, NULL) == ANEDOTA_DIFF_ERR_INVALID_ARG);

    assert(anedota_diff_versions(uninit, c1, c1, &sentinel) == ANEDOTA_DIFF_ERR_UNINITIALIZED);

    assert(anedota_diff_versions(root, missing, c1, &sentinel) == ANEDOTA_DIFF_ERR_VERSION_NOT_FOUND);
    assert(anedota_diff_versions(root, c1, missing, &sentinel) == ANEDOTA_DIFF_ERR_VERSION_NOT_FOUND);
    assert(anedota_diff_versions(root, missing, missing, &sentinel) == ANEDOTA_DIFF_ERR_VERSION_NOT_FOUND);

    assert(anedota_diff_versions(root, "curto", c1, &sentinel) == ANEDOTA_DIFF_ERR_INVALID_VERSION);
    assert(anedota_diff_versions(root, c1, "", &sentinel) == ANEDOTA_DIFF_ERR_INVALID_VERSION);
    assert(anedota_diff_versions(root, blob, c1, &sentinel) == ANEDOTA_DIFF_ERR_INVALID_VERSION);
    assert(anedota_diff_versions(root, c1, blob, &sentinel) == ANEDOTA_DIFF_ERR_INVALID_VERSION);

    assert(sentinel == (anedota_diff_t *)1);

    assert(anedota_diff_status_message(ANEDOTA_DIFF_OK) != NULL);
    assert(anedota_diff_status_message(ANEDOTA_DIFF_ERR_VERSION_NOT_FOUND) != NULL);
    assert(anedota_diff_status_message(ANEDOTA_DIFF_ERR_INVALID_VERSION) != NULL);

    remove_tree_recursive(workspace);
    printf("[PASS] test_diff_errors_and_missing_versions\n");
}

int main(void)
{
    printf("Executando testes de comparacao (diff)...\n");
    test_diff_added_removed_modified();
    test_diff_equal_versions();
    test_diff_type_change_blob_and_tree();
    test_diff_errors_and_missing_versions();
    printf("Testes de comparacao concluídos com sucesso\n");
    return 0;
}
