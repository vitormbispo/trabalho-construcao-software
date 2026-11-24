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

static void join_path(char *out, size_t out_size,
                      const char *base, const char *relative)
{
    assert(snprintf(out, out_size, "%s/%s", base, relative) < (int)out_size);
}

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

static void create_dir(const char *base, const char *relative)
{
    char path[512];
    join_path(path, sizeof(path), base, relative);
    assert(pal_dir_create(path) == 0);
}

static void create_text_file(const char *base, const char *relative,
                             const char *contents)
{
    char path[512];
    join_path(path, sizeof(path), base, relative);
    write_test_bytes(path, contents, strlen(contents));
}

static void create_binary_file(const char *base, const char *relative,
                               const void *data, size_t size)
{
    char path[512];
    join_path(path, sizeof(path), base, relative);
    write_test_bytes(path, data, size);
}

static void store_text_blob(const char *root, const char *workspace,
                            const char *name, const char *contents,
                            char hash_out[ANEDOTA_SHA256_HEX_SIZE])
{
    char path[512];
    join_path(path, sizeof(path), workspace, name);
    write_test_bytes(path, contents, strlen(contents));
    assert(anedota_blob_write_file(root, path, hash_out) == 0);
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

static void check_u32_be(const unsigned char *bytes, uint32_t expected)
{
    assert(bytes[0] == (unsigned char)(expected >> 24));
    assert(bytes[1] == (unsigned char)(expected >> 16));
    assert(bytes[2] == (unsigned char)(expected >> 8));
    assert(bytes[3] == (unsigned char)expected);
}

static void assert_entry(const anedota_tree_entry_t *entry, const char *name,
                         anedota_tree_entry_type_t type, const char *hash)
{
    assert(strcmp(entry->name, name) == 0);
    assert(entry->type == type);
    if (hash)
    {
        assert(strcmp(entry->hash, hash) == 0);
    }
}

static void assert_blob_bytes(const char *root, const char *hash,
                              const void *expected, size_t expected_size)
{
    char *data = NULL;
    size_t size = 0;
    assert(anedota_blob_read(root, hash, &data, &size) == 0);
    assert(size == expected_size);
    assert(memcmp(data, expected, expected_size) == 0);
    free(data);
}

static void assert_blob_text(const char *root, const char *hash,
                             const char *expected)
{
    assert_blob_bytes(root, hash, expected, strlen(expected));
}

static void assert_tree_read_fails(const char *root, const char *hash)
{
    anedota_tree_t *tree = (anedota_tree_t *)1;
    assert(anedota_tree_read(root, hash, &tree) == -1);
    assert(tree == (anedota_tree_t *)1);
}

static void assert_corrupt_tree_rejected(const char *root, const char *tmp_path,
                                         const void *payload, size_t size)
{
    char hash[ANEDOTA_SHA256_HEX_SIZE];
    write_test_bytes(tmp_path, payload, size);
    assert(anedota_blob_write_file(root, tmp_path, hash) == 0);
    assert_tree_read_fails(root, hash);
}

static void verify_serialized_two_entry_tree(const char *root,
                                             const char *tree_hash,
                                             const char *blob_hash,
                                             const char *empty_tree_hash)
{
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
}

static void test_tree_round_trip_and_determinism(void)
{
    char workspace[] = "/tmp/anedota-tree-mem-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    assert(anedota_storage_init(root) == 0);

    char empty_hash[ANEDOTA_SHA256_HEX_SIZE];
    char blob_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, NULL, 0, empty_hash) == 0);
    store_text_blob(root, workspace, "blob_a.txt", "tree child blob", blob_hash);

    anedota_tree_entry_t entries[] = {
        { "subtree", ANEDOTA_TREE_ENTRY_TREE, empty_hash },
        { "alpha.txt", ANEDOTA_TREE_ENTRY_BLOB, blob_hash }
    };
    anedota_tree_entry_t reversed[] = { entries[1], entries[0] };
    char tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    char reversed_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, entries, 2, tree_hash) == 0);
    assert(anedota_tree_write(root, reversed, 2, reversed_hash) == 0);
    assert(strcmp(tree_hash, reversed_hash) == 0);

    anedota_tree_t *tree = NULL;
    assert(anedota_tree_read(root, tree_hash, &tree) == 0);
    assert(tree != NULL && tree->count == 2);
    assert_entry(&tree->entries[0], "alpha.txt", ANEDOTA_TREE_ENTRY_BLOB, blob_hash);
    assert_entry(&tree->entries[1], "subtree", ANEDOTA_TREE_ENTRY_TREE, empty_hash);
    free(tree);

    assert(anedota_tree_read(root, empty_hash, &tree) == 0);
    assert(tree != NULL && tree->count == 0);
    free(tree);

    verify_serialized_two_entry_tree(root, tree_hash, blob_hash, empty_hash);
    remove_tree_recursive(workspace);
    printf("[PASS] test_tree_round_trip_and_determinism\n");
}

static void test_tree_nested_in_memory(void)
{
    char workspace[] = "/tmp/anedota-tree-nest-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    assert(anedota_storage_init(root) == 0);

    char blob_a[ANEDOTA_SHA256_HEX_SIZE];
    char blob_b[ANEDOTA_SHA256_HEX_SIZE];
    store_text_blob(root, workspace, "a.txt", "root content", blob_a);
    store_text_blob(root, workspace, "b.txt", "nested content", blob_b);

    anedota_tree_entry_t child_entries[] = {
        { "nested.txt", ANEDOTA_TREE_ENTRY_BLOB, blob_b }
    };
    char child_tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, child_entries, 1, child_tree_hash) == 0);

    anedota_tree_entry_t parent_entries[] = {
        { "dir", ANEDOTA_TREE_ENTRY_TREE, child_tree_hash },
        { "alpha.txt", ANEDOTA_TREE_ENTRY_BLOB, blob_a }
    };
    char parent_tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, parent_entries, 2, parent_tree_hash) == 0);

    anedota_tree_t *parent = NULL;
    anedota_tree_t *child = NULL;
    assert(anedota_tree_read(root, parent_tree_hash, &parent) == 0);
    assert(parent != NULL && parent->count == 2);
    assert_entry(&parent->entries[0], "alpha.txt", ANEDOTA_TREE_ENTRY_BLOB, blob_a);
    assert_entry(&parent->entries[1], "dir", ANEDOTA_TREE_ENTRY_TREE, child_tree_hash);

    assert(anedota_tree_read(root, parent->entries[1].hash, &child) == 0);
    assert(child != NULL && child->count == 1);
    assert_entry(&child->entries[0], "nested.txt", ANEDOTA_TREE_ENTRY_BLOB, blob_b);
    assert_blob_text(root, child->entries[0].hash, "nested content");

    free(child);
    free(parent);
    remove_tree_recursive(workspace);
    printf("[PASS] test_tree_nested_in_memory\n");
}

static size_t build_unsorted_tree_payload(unsigned char *out,
                                          const char *child_hash)
{
    size_t pos = 0;
    memcpy(out + pos, "ANEDOTAT", 8);
    pos += 8;
    out[pos++] = 1;
    out[pos++] = 0;
    out[pos++] = 0;
    out[pos++] = 0;
    out[pos++] = 2;

    const char names[2] = { 'b', 'a' };
    for (size_t i = 0; i < 2; ++i)
    {
        out[pos++] = ANEDOTA_TREE_ENTRY_BLOB;
        out[pos++] = 0;
        out[pos++] = 0;
        out[pos++] = 0;
        out[pos++] = 1;
        memcpy(out + pos, child_hash, 64);
        pos += 64;
        out[pos++] = (unsigned char)names[i];
    }
    return pos;
}

static void test_tree_invalid_inputs(void)
{
    char workspace[] = "/tmp/anedota-tree-inv-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    char uninit_root[512];
    char bad_payload_path[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    join_path(uninit_root, sizeof(uninit_root), workspace, "uninitialized");
    join_path(bad_payload_path, sizeof(bad_payload_path), workspace, "bad.bin");
    assert(anedota_storage_init(root) == 0);

    char child_hash[ANEDOTA_SHA256_HEX_SIZE];
    store_text_blob(root, workspace, "blob.txt", "valid child", child_hash);

    char unchanged[ANEDOTA_SHA256_HEX_SIZE] = "unchanged";
    anedota_tree_entry_t entry = { "valid", ANEDOTA_TREE_ENTRY_BLOB, child_hash };
    assert(anedota_tree_write(NULL, &entry, 1, unchanged) == -1);
    assert(anedota_tree_write("", &entry, 1, unchanged) == -1);
    assert(anedota_tree_write(uninit_root, &entry, 1, unchanged) == -1);
    assert(anedota_tree_write(root, NULL, 1, unchanged) == -1);
    assert(anedota_tree_write(root, &entry, 1, NULL) == -1);

    anedota_tree_entry_t invalid_hash = { "valid", ANEDOTA_TREE_ENTRY_BLOB, "bad" };
    anedota_tree_entry_t missing_child = {
        "valid", ANEDOTA_TREE_ENTRY_BLOB,
        "0000000000000000000000000000000000000000000000000000000000000000"
    };
    anedota_tree_entry_t duplicates[] = {
        { "same", ANEDOTA_TREE_ENTRY_BLOB, child_hash },
        { "same", ANEDOTA_TREE_ENTRY_TREE, child_hash }
    };
    anedota_tree_entry_t invalid_type = {
        "valid", (anedota_tree_entry_type_t)3, child_hash
    };
    assert(anedota_tree_write(root, &invalid_hash, 1, unchanged) == -1);
    assert(anedota_tree_write(root, &missing_child, 1, unchanged) == -1);
    assert(anedota_tree_write(root, duplicates, 2, unchanged) == -1);
    assert(anedota_tree_write(root, &invalid_type, 1, unchanged) == -1);

    const char *bad_names[] = { "", ".", "..", "a/b", "../escape" };
    for (size_t i = 0; i < sizeof(bad_names) / sizeof(bad_names[0]); ++i)
    {
        anedota_tree_entry_t bad_entry = {
            bad_names[i], ANEDOTA_TREE_ENTRY_BLOB, child_hash
        };
        assert(anedota_tree_write(root, &bad_entry, 1, unchanged) == -1);
    }
    assert(strcmp(unchanged, "unchanged") == 0);

    assert_tree_read_fails(NULL, child_hash);
    assert_tree_read_fails("", child_hash);
    assert_tree_read_fails(uninit_root, child_hash);
    assert_tree_read_fails(root, NULL);
    assert(anedota_tree_read(root, child_hash, NULL) == -1);
    assert_tree_read_fails(
        root, "0000000000000000000000000000000000000000000000000000000000000000");

    const unsigned char wrong_version[] = {
        'A', 'N', 'E', 'D', 'O', 'T', 'A', 'T', 2, 0, 0, 0, 0
    };
    const unsigned char trailing_bytes[] = {
        'A', 'N', 'E', 'D', 'O', 'T', 'A', 'T', 1, 0, 0, 0, 0, 0xFF
    };
    unsigned char unsorted[13 + 2 * (1 + 4 + 64 + 1)];
    const size_t unsorted_size = build_unsorted_tree_payload(unsorted, child_hash);

    assert_corrupt_tree_rejected(root, bad_payload_path, "not a tree", 10);
    assert_corrupt_tree_rejected(root, bad_payload_path, wrong_version,
                                 sizeof(wrong_version));
    assert_corrupt_tree_rejected(root, bad_payload_path, trailing_bytes,
                                 sizeof(trailing_bytes));
    assert_corrupt_tree_rejected(root, bad_payload_path, unsorted, unsorted_size);

    remove_tree_recursive(workspace);
    printf("[PASS] test_tree_invalid_inputs\n");
}

static void test_tree_write_directory_recursive(void)
{
    char workspace[] = "/tmp/anedota-tree-dir-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    char snapshot_dir[512];
    join_path(root, sizeof(root), workspace, "storage");
    join_path(snapshot_dir, sizeof(snapshot_dir), workspace, "project");
    assert(anedota_storage_init(root) == 0);
    assert(pal_dir_create(snapshot_dir) == 0);

    const unsigned char binary_data[] = { 0x00, 0x01, 0xFF, 'b', 'i', 'n', 0x00 };
    create_binary_file(snapshot_dir, "a.bin", binary_data, sizeof(binary_data));
    create_text_file(snapshot_dir, "z.txt", "arquivo na raiz");
    create_dir(snapshot_dir, "empty_dir");
    create_dir(snapshot_dir, "sub");
    create_text_file(snapshot_dir, "sub/inside.txt", "conteudo interno");
    create_dir(snapshot_dir, "sub/deep");
    create_text_file(snapshot_dir, "sub/deep/leaf.txt", "folha profunda");

    char root_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write_directory(root, snapshot_dir, root_hash) == 0);

    anedota_tree_t *root_tree = NULL;
    anedota_tree_t *empty_tree = NULL;
    anedota_tree_t *sub_tree = NULL;
    anedota_tree_t *deep_tree = NULL;

    assert(anedota_tree_read(root, root_hash, &root_tree) == 0);
    assert(root_tree != NULL && root_tree->count == 4);
    assert_entry(&root_tree->entries[0], "a.bin", ANEDOTA_TREE_ENTRY_BLOB, NULL);
    assert_entry(&root_tree->entries[1], "empty_dir", ANEDOTA_TREE_ENTRY_TREE, NULL);
    assert_entry(&root_tree->entries[2], "sub", ANEDOTA_TREE_ENTRY_TREE, NULL);
    assert_entry(&root_tree->entries[3], "z.txt", ANEDOTA_TREE_ENTRY_BLOB, NULL);

    assert_blob_bytes(root, root_tree->entries[0].hash,
                      binary_data, sizeof(binary_data));
    assert_blob_text(root, root_tree->entries[3].hash, "arquivo na raiz");

    assert(anedota_tree_read(root, root_tree->entries[1].hash, &empty_tree) == 0);
    assert(empty_tree != NULL && empty_tree->count == 0);

    assert(anedota_tree_read(root, root_tree->entries[2].hash, &sub_tree) == 0);
    assert(sub_tree != NULL && sub_tree->count == 2);
    assert_entry(&sub_tree->entries[0], "deep", ANEDOTA_TREE_ENTRY_TREE, NULL);
    assert_entry(&sub_tree->entries[1], "inside.txt", ANEDOTA_TREE_ENTRY_BLOB, NULL);
    assert_blob_text(root, sub_tree->entries[1].hash, "conteudo interno");

    assert(anedota_tree_read(root, sub_tree->entries[0].hash, &deep_tree) == 0);
    assert(deep_tree != NULL && deep_tree->count == 1);
    assert_entry(&deep_tree->entries[0], "leaf.txt", ANEDOTA_TREE_ENTRY_BLOB, NULL);
    assert_blob_text(root, deep_tree->entries[0].hash, "folha profunda");

    free(deep_tree);
    free(sub_tree);
    free(empty_tree);
    free(root_tree);
    remove_tree_recursive(workspace);
    printf("[PASS] test_tree_write_directory_recursive\n");
}

static void populate_sample_dir(const char *dir, int reverse_order)
{
    if (!reverse_order)
    {
        create_text_file(dir, "first.txt", "mesmo conteudo 1");
        create_dir(dir, "sub");
        create_text_file(dir, "sub/nested.txt", "mesmo conteudo 2");
        create_text_file(dir, "second.txt", "mesmo conteudo 3");
        return;
    }

    create_text_file(dir, "second.txt", "mesmo conteudo 3");
    create_dir(dir, "sub");
    create_text_file(dir, "sub/nested.txt", "mesmo conteudo 2");
    create_text_file(dir, "first.txt", "mesmo conteudo 1");
}

static void test_tree_write_directory_determinism_and_excludes_anedota(void)
{
    char workspace[] = "/tmp/anedota-tree-det-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char dir_a[512];
    char dir_b[512];
    char storage_a[512];
    char storage_b[512];
    join_path(dir_a, sizeof(dir_a), workspace, "dir_a");
    join_path(dir_b, sizeof(dir_b), workspace, "dir_b");
    join_path(storage_a, sizeof(storage_a), dir_a, ".anedota");
    join_path(storage_b, sizeof(storage_b), workspace, "storage_b");

    assert(pal_dir_create(dir_a) == 0);
    assert(anedota_storage_init(storage_a) == 0);
    create_text_file(storage_a, "extra_internal.txt", "ignorar");
    populate_sample_dir(dir_a, 0);
    create_dir(dir_a, "sub/.anedota");
    create_text_file(dir_a, "sub/.anedota/ignored.txt", "ignorar");

    assert(pal_dir_create(dir_b) == 0);
    assert(anedota_storage_init(storage_b) == 0);
    populate_sample_dir(dir_b, 1);

    char hash_a[ANEDOTA_SHA256_HEX_SIZE];
    char hash_b[ANEDOTA_SHA256_HEX_SIZE];
    char hash_repeat[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write_directory(storage_a, dir_a, hash_a) == 0);
    assert(anedota_tree_write_directory(storage_b, dir_b, hash_b) == 0);
    assert(strcmp(hash_a, hash_b) == 0);

    assert(anedota_tree_write_directory(storage_a, dir_a, hash_repeat) == 0);
    assert(strcmp(hash_a, hash_repeat) == 0);

    anedota_tree_t *tree = NULL;
    anedota_tree_t *sub_tree = NULL;
    assert(anedota_tree_read(storage_a, hash_a, &tree) == 0);
    assert(tree != NULL && tree->count == 3);
    assert_entry(&tree->entries[0], "first.txt", ANEDOTA_TREE_ENTRY_BLOB, NULL);
    assert_entry(&tree->entries[1], "second.txt", ANEDOTA_TREE_ENTRY_BLOB, NULL);
    assert_entry(&tree->entries[2], "sub", ANEDOTA_TREE_ENTRY_TREE, NULL);
    assert(anedota_tree_read(storage_a, tree->entries[2].hash, &sub_tree) == 0);
    assert(sub_tree != NULL && sub_tree->count == 1);
    assert_entry(&sub_tree->entries[0], "nested.txt", ANEDOTA_TREE_ENTRY_BLOB, NULL);
    free(sub_tree);
    free(tree);

    create_text_file(dir_b, "sub/nested.txt", "conteudo alterado");
    char hash_modified[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write_directory(storage_b, dir_b, hash_modified) == 0);
    assert(strcmp(hash_modified, hash_b) != 0);

    remove_tree_recursive(workspace);
    printf("[PASS] test_tree_write_directory_determinism_and_excludes_anedota\n");
}

static void test_tree_directory_only_anedota_matches_empty_tree(void)
{
    char workspace[] = "/tmp/anedota-tree-empty-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char storage[512];
    join_path(storage, sizeof(storage), workspace, ".anedota");
    assert(anedota_storage_init(storage) == 0);

    char dir_hash[ANEDOTA_SHA256_HEX_SIZE];
    char api_empty_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write_directory(storage, workspace, dir_hash) == 0);
    assert(anedota_tree_write(storage, NULL, 0, api_empty_hash) == 0);
    assert(strcmp(dir_hash, api_empty_hash) == 0);

    remove_tree_recursive(workspace);
    printf("[PASS] test_tree_directory_only_anedota_matches_empty_tree\n");
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
    join_path(root, sizeof(root), workspace, ".anedota");
    join_path(uninit_root, sizeof(uninit_root), workspace, "no_repo");
    join_path(file_path, sizeof(file_path), workspace, "regular.txt");
    join_path(missing_dir, sizeof(missing_dir), workspace, "missing");
    join_path(symlink_dir, sizeof(symlink_dir), workspace, "with_link");
    join_path(link_path, sizeof(link_path), symlink_dir, "link");

    assert(anedota_storage_init(root) == 0);
    create_text_file(workspace, "regular.txt", "regular file");
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
    test_tree_nested_in_memory();
    test_tree_invalid_inputs();
    test_tree_write_directory_recursive();
    test_tree_write_directory_determinism_and_excludes_anedota();
    test_tree_directory_only_anedota_matches_empty_tree();
    test_tree_write_directory_invalid_inputs();
    printf("Testes de Tree concluídos com sucesso\n");
    return 0;
}
