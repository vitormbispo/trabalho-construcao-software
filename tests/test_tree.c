#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#endif

#include <anedota/storage.h>
#include <anedota/tree.h>
#include <assert.h>
#include <pal/pal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_test_bytes(const char *path, const void *data, size_t size)
{
    pal_file_t *file = pal_open(path, "wb");
    assert(file != NULL);
    if (size > 0)
    {
        assert(pal_write(file, data, size) == (int)size);
    }
    assert(pal_close(file) == 0);
}

static void write_test_file(const char *path, const char *contents)
{
    write_test_bytes(path, contents, strlen(contents));
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
        assert(snprintf(child, sizeof(child), "%s/%s", path, name) < (int)sizeof(child));
        if (type == PAL_DIR_ENTRY_DIRECTORY)
        {
            remove_tree_recursive(child);
        }
        else
        {
            assert(unlink(child) == 0);
        }
    }
    assert(status == 0);
    assert(pal_dir_close(dir) == 0);
    assert(rmdir(path) == 0);
}

static void check_u32_be(const unsigned char *bytes, uint32_t expected)
{
    assert(bytes[0] == (unsigned char)(expected >> 24));
    assert(bytes[1] == (unsigned char)(expected >> 16));
    assert(bytes[2] == (unsigned char)(expected >> 8));
    assert(bytes[3] == (unsigned char)expected);
}

static void test_tree_round_trip_and_determinism(void)
{
    char workspace[] = "/tmp/anedota-tree-mem-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    char blob_a_path[512];
    char blob_b_path[512];
    assert(snprintf(root, sizeof(root), "%s/.anedota", workspace) < (int)sizeof(root));
    assert(snprintf(blob_a_path, sizeof(blob_a_path), "%s/blob_a.txt", workspace) < (int)sizeof(blob_a_path));
    assert(snprintf(blob_b_path, sizeof(blob_b_path), "%s/blob_b.txt", workspace) < (int)sizeof(blob_b_path));
    assert(anedota_storage_init(root) == 0);

    char empty_tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, NULL, 0, empty_tree_hash) == 0);

    write_test_file(blob_a_path, "tree child blob");
    char blob_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_blob_write_file(root, blob_a_path, blob_hash) == 0);

    write_test_file(blob_b_path, "nested blob content");
    char nested_blob_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_blob_write_file(root, blob_b_path, nested_blob_hash) == 0);

    anedota_tree_entry_t entries[] = {
        { "subtree", ANEDOTA_TREE_ENTRY_TREE, empty_tree_hash },
        { "alpha.txt", ANEDOTA_TREE_ENTRY_BLOB, blob_hash }
    };
    char tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, entries, 2, tree_hash) == 0);

    anedota_tree_entry_t reversed[] = { entries[1], entries[0] };
    char reversed_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, reversed, 2, reversed_hash) == 0);
    assert(strcmp(tree_hash, reversed_hash) == 0);

    anedota_tree_t *tree = NULL;
    assert(anedota_tree_read(root, tree_hash, &tree) == 0);
    assert(tree != NULL);
    assert(tree->count == 2);
    assert(strcmp(tree->entries[0].name, "alpha.txt") == 0);
    assert(tree->entries[0].type == ANEDOTA_TREE_ENTRY_BLOB);
    assert(strcmp(tree->entries[0].hash, blob_hash) == 0);
    assert(strcmp(tree->entries[1].name, "subtree") == 0);
    assert(tree->entries[1].type == ANEDOTA_TREE_ENTRY_TREE);
    assert(strcmp(tree->entries[1].hash, empty_tree_hash) == 0);
    free(tree);

    /* Multi-level in-memory Tree: root -> child_tree -> nested blob */
    anedota_tree_entry_t child_entries[] = {
        { "nested.txt", ANEDOTA_TREE_ENTRY_BLOB, nested_blob_hash }
    };
    char child_tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, child_entries, 1, child_tree_hash) == 0);

    anedota_tree_entry_t parent_entries[] = {
        { "dir", ANEDOTA_TREE_ENTRY_TREE, child_tree_hash },
        { "alpha.txt", ANEDOTA_TREE_ENTRY_BLOB, blob_hash }
    };
    char parent_tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, parent_entries, 2, parent_tree_hash) == 0);

    assert(anedota_tree_read(root, parent_tree_hash, &tree) == 0);
    assert(tree != NULL);
    assert(tree->count == 2);
    assert(strcmp(tree->entries[0].name, "alpha.txt") == 0);
    assert(strcmp(tree->entries[1].name, "dir") == 0);
    assert(tree->entries[1].type == ANEDOTA_TREE_ENTRY_TREE);
    assert(strcmp(tree->entries[1].hash, child_tree_hash) == 0);
    free(tree);

    assert(anedota_tree_read(root, child_tree_hash, &tree) == 0);
    assert(tree != NULL);
    assert(tree->count == 1);
    assert(strcmp(tree->entries[0].name, "nested.txt") == 0);
    assert(tree->entries[0].type == ANEDOTA_TREE_ENTRY_BLOB);
    assert(strcmp(tree->entries[0].hash, nested_blob_hash) == 0);
    free(tree);

    assert(anedota_tree_read(root, empty_tree_hash, &tree) == 0);
    assert(tree != NULL);
    assert(tree->count == 0);
    free(tree);

    char *payload = NULL;
    size_t payload_size = 0;
    assert(anedota_blob_read(root, tree_hash, &payload, &payload_size) == 0);
    assert(payload_size == 13 + 1 + 4 + 64 + 9 + 1 + 4 + 64 + 7);
    assert(memcmp(payload, "ANEDOTAT", 8) == 0);
    assert(payload[8] == 1);
    check_u32_be((const unsigned char *)payload + 9, 2);
    size_t offset = 13;
    assert((unsigned char)payload[offset++] == ANEDOTA_TREE_ENTRY_BLOB);
    check_u32_be((const unsigned char *)payload + offset, 9);
    offset += 4;
    assert(memcmp(payload + offset, blob_hash, 64) == 0);
    offset += 64;
    assert(memcmp(payload + offset, "alpha.txt", 9) == 0);
    offset += 9;
    assert((unsigned char)payload[offset++] == ANEDOTA_TREE_ENTRY_TREE);
    check_u32_be((const unsigned char *)payload + offset, 7);
    offset += 4;
    assert(memcmp(payload + offset, empty_tree_hash, 64) == 0);
    offset += 64;
    assert(memcmp(payload + offset, "subtree", 7) == 0);
    free(payload);

    remove_tree_recursive(workspace);
    printf("[PASS] test_tree_round_trip_and_determinism\n");
}

static void test_tree_invalid_inputs(void)
{
    char workspace[] = "/tmp/anedota-tree-inv-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    char uninit_root[512];
    char blob_path[512];
    char bad_payload_path[512];
    assert(snprintf(root, sizeof(root), "%s/.anedota", workspace) < (int)sizeof(root));
    assert(snprintf(uninit_root, sizeof(uninit_root), "%s/uninitialized", workspace) < (int)sizeof(uninit_root));
    assert(snprintf(blob_path, sizeof(blob_path), "%s/blob.txt", workspace) < (int)sizeof(blob_path));
    assert(snprintf(bad_payload_path, sizeof(bad_payload_path), "%s/bad.bin", workspace) < (int)sizeof(bad_payload_path));
    assert(anedota_storage_init(root) == 0);

    write_test_file(blob_path, "valid child");
    char child_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_blob_write_file(root, blob_path, child_hash) == 0);

    char unchanged[ANEDOTA_SHA256_HEX_SIZE] = "unchanged";
    anedota_tree_entry_t entry = {
        "valid", ANEDOTA_TREE_ENTRY_BLOB, child_hash
    };
    assert(anedota_tree_write(NULL, &entry, 1, unchanged) == -1);
    assert(anedota_tree_write("", &entry, 1, unchanged) == -1);
    assert(anedota_tree_write(uninit_root, &entry, 1, unchanged) == -1);
    assert(anedota_tree_write(root, NULL, 1, unchanged) == -1);
    assert(anedota_tree_write(root, &entry, 1, NULL) == -1);

    anedota_tree_entry_t invalid_hash = {
        "valid", ANEDOTA_TREE_ENTRY_BLOB, "bad"
    };
    assert(anedota_tree_write(root, &invalid_hash, 1, unchanged) == -1);

    anedota_tree_entry_t missing_child = {
        "valid", ANEDOTA_TREE_ENTRY_BLOB,
        "0000000000000000000000000000000000000000000000000000000000000000"
    };
    assert(anedota_tree_write(root, &missing_child, 1, unchanged) == -1);

    anedota_tree_entry_t duplicate_names[] = {
        { "same", ANEDOTA_TREE_ENTRY_BLOB, child_hash },
        { "same", ANEDOTA_TREE_ENTRY_TREE, child_hash }
    };
    assert(anedota_tree_write(root, duplicate_names, 2, unchanged) == -1);

    const char *bad_names[] = { "", ".", "..", "a/b", "../escape" };
    for (size_t i = 0; i < sizeof(bad_names) / sizeof(bad_names[0]); ++i)
    {
        anedota_tree_entry_t invalid_name = {
            bad_names[i], ANEDOTA_TREE_ENTRY_BLOB, child_hash
        };
        assert(anedota_tree_write(root, &invalid_name, 1, unchanged) == -1);
    }

    anedota_tree_entry_t invalid_type = {
        "valid", (anedota_tree_entry_type_t)3, child_hash
    };
    assert(anedota_tree_write(root, &invalid_type, 1, unchanged) == -1);
    assert(strcmp(unchanged, "unchanged") == 0);

    anedota_tree_t *tree = (anedota_tree_t *)1;
    assert(anedota_tree_read(NULL, child_hash, &tree) == -1);
    assert(anedota_tree_read("", child_hash, &tree) == -1);
    assert(anedota_tree_read(uninit_root, child_hash, &tree) == -1);
    assert(anedota_tree_read(root, NULL, &tree) == -1);
    assert(anedota_tree_read(root, child_hash, NULL) == -1);
    assert(anedota_tree_read(root,
        "0000000000000000000000000000000000000000000000000000000000000000",
        &tree) == -1);
    assert(tree == (anedota_tree_t *)1);

    /* Non-tree blob */
    write_test_file(bad_payload_path, "not a tree");
    char invalid_tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_blob_write_file(root, bad_payload_path, invalid_tree_hash) == 0);
    assert(anedota_tree_read(root, invalid_tree_hash, &tree) == -1);
    assert(tree == (anedota_tree_t *)1);

    /* Wrong version byte */
    const unsigned char wrong_version[] = {
        'A', 'N', 'E', 'D', 'O', 'T', 'A', 'T', 2, 0, 0, 0, 0
    };
    write_test_bytes(bad_payload_path, wrong_version, sizeof(wrong_version));
    assert(anedota_blob_write_file(root, bad_payload_path, invalid_tree_hash) == 0);
    assert(anedota_tree_read(root, invalid_tree_hash, &tree) == -1);
    assert(tree == (anedota_tree_t *)1);

    /* Trailing garbage bytes after valid header */
    const unsigned char trailing_bytes[] = {
        'A', 'N', 'E', 'D', 'O', 'T', 'A', 'T', 1, 0, 0, 0, 0, 0xFF
    };
    write_test_bytes(bad_payload_path, trailing_bytes, sizeof(trailing_bytes));
    assert(anedota_blob_write_file(root, bad_payload_path, invalid_tree_hash) == 0);
    assert(anedota_tree_read(root, invalid_tree_hash, &tree) == -1);
    assert(tree == (anedota_tree_t *)1);

    /* Unsorted entries payload */
    unsigned char unsorted[13 + 2 * (1 + 4 + 64 + 1)];
    size_t pos = 0;
    memcpy(unsorted + pos, "ANEDOTAT", 8);
    pos += 8;
    unsorted[pos++] = 1;
    unsorted[pos++] = 0;
    unsorted[pos++] = 0;
    unsorted[pos++] = 0;
    unsorted[pos++] = 2;
    unsorted[pos++] = ANEDOTA_TREE_ENTRY_BLOB;
    unsorted[pos++] = 0;
    unsorted[pos++] = 0;
    unsorted[pos++] = 0;
    unsorted[pos++] = 1;
    memcpy(unsorted + pos, child_hash, 64);
    pos += 64;
    unsorted[pos++] = 'b';
    unsorted[pos++] = ANEDOTA_TREE_ENTRY_BLOB;
    unsorted[pos++] = 0;
    unsorted[pos++] = 0;
    unsorted[pos++] = 0;
    unsorted[pos++] = 1;
    memcpy(unsorted + pos, child_hash, 64);
    pos += 64;
    unsorted[pos++] = 'a';
    write_test_bytes(bad_payload_path, unsorted, pos);
    assert(anedota_blob_write_file(root, bad_payload_path, invalid_tree_hash) == 0);
    assert(anedota_tree_read(root, invalid_tree_hash, &tree) == -1);
    assert(tree == (anedota_tree_t *)1);

    remove_tree_recursive(workspace);
    printf("[PASS] test_tree_invalid_inputs\n");
}

static void test_tree_write_directory_recursive(void)
{
    char workspace[] = "/tmp/anedota-tree-dir-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    char snapshot_dir[512];
    char path[512];
    assert(snprintf(root, sizeof(root), "%s/storage", workspace) < (int)sizeof(root));
    assert(snprintf(snapshot_dir, sizeof(snapshot_dir), "%s/project", workspace) < (int)sizeof(snapshot_dir));
    assert(anedota_storage_init(root) == 0);
    assert(pal_dir_create(snapshot_dir) == 0);

    const unsigned char binary_data[] = { 0x00, 0x01, 0xFF, 'b', 'i', 'n', 0x00 };
    assert(snprintf(path, sizeof(path), "%s/a.bin", snapshot_dir) < (int)sizeof(path));
    write_test_bytes(path, binary_data, sizeof(binary_data));

    assert(snprintf(path, sizeof(path), "%s/z.txt", snapshot_dir) < (int)sizeof(path));
    write_test_file(path, "arquivo na raiz");

    assert(snprintf(path, sizeof(path), "%s/empty_dir", snapshot_dir) < (int)sizeof(path));
    assert(pal_dir_create(path) == 0);

    assert(snprintf(path, sizeof(path), "%s/sub", snapshot_dir) < (int)sizeof(path));
    assert(pal_dir_create(path) == 0);

    assert(snprintf(path, sizeof(path), "%s/sub/inside.txt", snapshot_dir) < (int)sizeof(path));
    write_test_file(path, "conteudo interno");

    assert(snprintf(path, sizeof(path), "%s/sub/deep", snapshot_dir) < (int)sizeof(path));
    assert(pal_dir_create(path) == 0);

    assert(snprintf(path, sizeof(path), "%s/sub/deep/leaf.txt", snapshot_dir) < (int)sizeof(path));
    write_test_file(path, "folha profunda");

    char root_tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write_directory(root, snapshot_dir, root_tree_hash) == 0);

    anedota_tree_t *root_tree = NULL;
    assert(anedota_tree_read(root, root_tree_hash, &root_tree) == 0);
    assert(root_tree != NULL);
    assert(root_tree->count == 4);

    assert(strcmp(root_tree->entries[0].name, "a.bin") == 0);
    assert(root_tree->entries[0].type == ANEDOTA_TREE_ENTRY_BLOB);
    char *blob_data = NULL;
    size_t blob_size = 0;
    assert(anedota_blob_read(root, root_tree->entries[0].hash, &blob_data, &blob_size) == 0);
    assert(blob_size == sizeof(binary_data));
    assert(memcmp(blob_data, binary_data, sizeof(binary_data)) == 0);
    free(blob_data);

    assert(strcmp(root_tree->entries[1].name, "empty_dir") == 0);
    assert(root_tree->entries[1].type == ANEDOTA_TREE_ENTRY_TREE);
    anedota_tree_t *empty_tree = NULL;
    assert(anedota_tree_read(root, root_tree->entries[1].hash, &empty_tree) == 0);
    assert(empty_tree != NULL);
    assert(empty_tree->count == 0);
    free(empty_tree);

    assert(strcmp(root_tree->entries[2].name, "sub") == 0);
    assert(root_tree->entries[2].type == ANEDOTA_TREE_ENTRY_TREE);
    anedota_tree_t *sub_tree = NULL;
    assert(anedota_tree_read(root, root_tree->entries[2].hash, &sub_tree) == 0);
    assert(sub_tree != NULL);
    assert(sub_tree->count == 2);
    assert(strcmp(sub_tree->entries[0].name, "deep") == 0);
    assert(sub_tree->entries[0].type == ANEDOTA_TREE_ENTRY_TREE);
    assert(strcmp(sub_tree->entries[1].name, "inside.txt") == 0);
    assert(sub_tree->entries[1].type == ANEDOTA_TREE_ENTRY_BLOB);

    assert(anedota_blob_read(root, sub_tree->entries[1].hash, &blob_data, &blob_size) == 0);
    assert(blob_size == strlen("conteudo interno"));
    assert(memcmp(blob_data, "conteudo interno", blob_size) == 0);
    free(blob_data);

    anedota_tree_t *deep_tree = NULL;
    assert(anedota_tree_read(root, sub_tree->entries[0].hash, &deep_tree) == 0);
    assert(deep_tree != NULL);
    assert(deep_tree->count == 1);
    assert(strcmp(deep_tree->entries[0].name, "leaf.txt") == 0);
    assert(deep_tree->entries[0].type == ANEDOTA_TREE_ENTRY_BLOB);
    assert(anedota_blob_read(root, deep_tree->entries[0].hash, &blob_data, &blob_size) == 0);
    assert(blob_size == strlen("folha profunda"));
    assert(memcmp(blob_data, "folha profunda", blob_size) == 0);
    free(blob_data);
    free(deep_tree);
    free(sub_tree);

    assert(strcmp(root_tree->entries[3].name, "z.txt") == 0);
    assert(root_tree->entries[3].type == ANEDOTA_TREE_ENTRY_BLOB);
    assert(anedota_blob_read(root, root_tree->entries[3].hash, &blob_data, &blob_size) == 0);
    assert(blob_size == strlen("arquivo na raiz"));
    assert(memcmp(blob_data, "arquivo na raiz", blob_size) == 0);
    free(blob_data);
    free(root_tree);

    remove_tree_recursive(workspace);
    printf("[PASS] test_tree_write_directory_recursive\n");
}

static void test_tree_write_directory_determinism_and_excludes_anedota(void)
{
    char workspace[] = "/tmp/anedota-tree-det-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char dir_a[512];
    char dir_b[512];
    char storage_a[512];
    char storage_b[512];
    char only_anedota_dir[512];
    char only_anedota_storage[512];
    char path[512];

    assert(snprintf(dir_a, sizeof(dir_a), "%s/dir_a", workspace) < (int)sizeof(dir_a));
    assert(snprintf(dir_b, sizeof(dir_b), "%s/dir_b", workspace) < (int)sizeof(dir_b));
    assert(snprintf(storage_a, sizeof(storage_a), "%s/.anedota", dir_a) < (int)sizeof(storage_a));
    assert(snprintf(storage_b, sizeof(storage_b), "%s/storage_b", workspace) < (int)sizeof(storage_b));
    assert(snprintf(only_anedota_dir, sizeof(only_anedota_dir), "%s/only_anedota", workspace) < (int)sizeof(only_anedota_dir));
    assert(snprintf(only_anedota_storage, sizeof(only_anedota_storage), "%s/.anedota", only_anedota_dir) < (int)sizeof(only_anedota_storage));

    assert(pal_dir_create(dir_a) == 0);
    assert(anedota_storage_init(storage_a) == 0);
    assert(snprintf(path, sizeof(path), "%s/extra_internal.txt", storage_a) < (int)sizeof(path));
    write_test_file(path, "nao deve entrar no snapshot");

    /* Populate dir_a in one order, including a nested .anedota/ directory */
    assert(snprintf(path, sizeof(path), "%s/first.txt", dir_a) < (int)sizeof(path));
    write_test_file(path, "mesmo conteudo 1");
    assert(snprintf(path, sizeof(path), "%s/sub", dir_a) < (int)sizeof(path));
    assert(pal_dir_create(path) == 0);
    assert(snprintf(path, sizeof(path), "%s/sub/nested.txt", dir_a) < (int)sizeof(path));
    write_test_file(path, "mesmo conteudo 2");
    assert(snprintf(path, sizeof(path), "%s/sub/.anedota", dir_a) < (int)sizeof(path));
    assert(pal_dir_create(path) == 0);
    assert(snprintf(path, sizeof(path), "%s/sub/.anedota/ignored.txt", dir_a) < (int)sizeof(path));
    write_test_file(path, "tambem deve ser ignorado");
    assert(snprintf(path, sizeof(path), "%s/second.txt", dir_a) < (int)sizeof(path));
    write_test_file(path, "mesmo conteudo 3");

    /* Populate dir_b in a different order without any .anedota/ directory */
    assert(pal_dir_create(dir_b) == 0);
    assert(anedota_storage_init(storage_b) == 0);
    assert(snprintf(path, sizeof(path), "%s/second.txt", dir_b) < (int)sizeof(path));
    write_test_file(path, "mesmo conteudo 3");
    assert(snprintf(path, sizeof(path), "%s/sub", dir_b) < (int)sizeof(path));
    assert(pal_dir_create(path) == 0);
    assert(snprintf(path, sizeof(path), "%s/sub/nested.txt", dir_b) < (int)sizeof(path));
    write_test_file(path, "mesmo conteudo 2");
    assert(snprintf(path, sizeof(path), "%s/first.txt", dir_b) < (int)sizeof(path));
    write_test_file(path, "mesmo conteudo 1");

    char hash_a[ANEDOTA_SHA256_HEX_SIZE];
    char hash_b[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write_directory(storage_a, dir_a, hash_a) == 0);
    assert(anedota_tree_write_directory(storage_b, dir_b, hash_b) == 0);
    assert(strcmp(hash_a, hash_b) == 0);

    /* Re-running after new objects exist in dir_a/.anedota/objetos produces same hash */
    char hash_a_repeat[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write_directory(storage_a, dir_a, hash_a_repeat) == 0);
    assert(strcmp(hash_a, hash_a_repeat) == 0);

    /* Verify .anedota is not listed in root or nested sub-Tree */
    anedota_tree_t *tree = NULL;
    assert(anedota_tree_read(storage_a, hash_a, &tree) == 0);
    assert(tree != NULL);
    assert(tree->count == 3);
    assert(strcmp(tree->entries[0].name, "first.txt") == 0);
    assert(strcmp(tree->entries[1].name, "second.txt") == 0);
    assert(strcmp(tree->entries[2].name, "sub") == 0);
    anedota_tree_t *sub_tree = NULL;
    assert(anedota_tree_read(storage_a, tree->entries[2].hash, &sub_tree) == 0);
    assert(sub_tree != NULL);
    assert(sub_tree->count == 1);
    assert(strcmp(sub_tree->entries[0].name, "nested.txt") == 0);
    free(sub_tree);
    free(tree);

    /* Changing a nested file must change the root Tree hash */
    assert(snprintf(path, sizeof(path), "%s/sub/nested.txt", dir_b) < (int)sizeof(path));
    write_test_file(path, "conteudo alterado");
    char hash_modified[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write_directory(storage_b, dir_b, hash_modified) == 0);
    assert(strcmp(hash_modified, hash_b) != 0);

    /* A directory containing only .anedota/ produces the empty Tree hash */
    assert(pal_dir_create(only_anedota_dir) == 0);
    assert(anedota_storage_init(only_anedota_storage) == 0);
    char only_anedota_hash[ANEDOTA_SHA256_HEX_SIZE];
    char empty_api_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write_directory(only_anedota_storage, only_anedota_dir,
                                        only_anedota_hash) == 0);
    assert(anedota_tree_write(only_anedota_storage, NULL, 0, empty_api_hash) == 0);
    assert(strcmp(only_anedota_hash, empty_api_hash) == 0);

    remove_tree_recursive(workspace);
    printf("[PASS] test_tree_write_directory_determinism_and_excludes_anedota\n");
}

static void test_tree_write_directory_invalid_inputs(void)
{
    char workspace[] = "/tmp/anedota-tree-dirinv-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    char uninit_root[512];
    char file_path[512];
    char missing_dir[512];
    char symlink_dir[512];
    char link_path[512];
    assert(snprintf(root, sizeof(root), "%s/.anedota", workspace) < (int)sizeof(root));
    assert(snprintf(uninit_root, sizeof(uninit_root), "%s/no_repo", workspace) < (int)sizeof(uninit_root));
    assert(snprintf(file_path, sizeof(file_path), "%s/regular.txt", workspace) < (int)sizeof(file_path));
    assert(snprintf(missing_dir, sizeof(missing_dir), "%s/missing", workspace) < (int)sizeof(missing_dir));
    assert(snprintf(symlink_dir, sizeof(symlink_dir), "%s/with_link", workspace) < (int)sizeof(symlink_dir));
    assert(snprintf(link_path, sizeof(link_path), "%s/link", symlink_dir) < (int)sizeof(link_path));

    assert(anedota_storage_init(root) == 0);
    write_test_file(file_path, "regular file");
    assert(pal_dir_create(symlink_dir) == 0);
    assert(symlink(file_path, link_path) == 0);

    char unchanged[ANEDOTA_SHA256_HEX_SIZE] = "unchanged";
    assert(anedota_tree_write_directory(NULL, workspace, unchanged) == -1);
    assert(anedota_tree_write_directory("", workspace, unchanged) == -1);
    assert(anedota_tree_write_directory(root, NULL, unchanged) == -1);
    assert(anedota_tree_write_directory(root, "", unchanged) == -1);
    assert(anedota_tree_write_directory(root, workspace, NULL) == -1);
    assert(anedota_tree_write_directory(uninit_root, workspace, unchanged) == -1);
    assert(anedota_tree_write_directory(root, missing_dir, unchanged) == -1);
    assert(anedota_tree_write_directory(root, file_path, unchanged) == -1);
    assert(anedota_tree_write_directory(root, symlink_dir, unchanged) == -1);
    assert(strcmp(unchanged, "unchanged") == 0);

    remove_tree_recursive(workspace);
    printf("[PASS] test_tree_write_directory_invalid_inputs\n");
}

int main(void)
{
    printf("Executando testes de Tree...\n");
    test_tree_round_trip_and_determinism();
    test_tree_invalid_inputs();
    test_tree_write_directory_recursive();
    test_tree_write_directory_determinism_and_excludes_anedota();
    test_tree_write_directory_invalid_inputs();
    printf("Testes de Tree concluídos com sucesso\n");
    return 0;
}
