#include <anedota/storage.h>
#include <anedota/tree.h>
#include <assert.h>
#include <pal/pal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *root = "test_anedota_tree";
static char generated_hashes[8][ANEDOTA_SHA256_HEX_SIZE];
static size_t generated_hash_count;

static void record_hash(const char *hash)
{
    assert(generated_hash_count < sizeof(generated_hashes) / sizeof(generated_hashes[0]));
    memcpy(generated_hashes[generated_hash_count++], hash, ANEDOTA_SHA256_HEX_SIZE);
}

static void cleanup_tree_test(void)
{
    char path[512];
    for (size_t i = 0; i < generated_hash_count; ++i)
    {
        assert(snprintf(path, sizeof(path), "%s/objetos/%s",
                        root, generated_hashes[i]) < (int)sizeof(path));
        unlink(path);
    }
    generated_hash_count = 0;
    unlink("test_tree_blob.txt");
    unlink("test_tree_invalid.txt");
    rmdir("test_anedota_tree/referencias/branches");
    rmdir("test_anedota_tree/referencias");
    rmdir("test_anedota_tree/objetos");
    rmdir("test_anedota_tree");
}

static void write_test_file(const char *path, const char *contents)
{
    pal_file_t *file = pal_open(path, "wb");
    assert(file != NULL);
    const size_t size = strlen(contents);
    assert(pal_write(file, contents, size) == (int)size);
    assert(pal_close(file) == 0);
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
    cleanup_tree_test();
    assert(anedota_storage_init(root) == 0);

    char empty_tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, NULL, 0, empty_tree_hash) == 0);
    record_hash(empty_tree_hash);

    write_test_file("test_tree_blob.txt", "tree child blob");
    char blob_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_blob_write_file(root, "test_tree_blob.txt", blob_hash) == 0);
    record_hash(blob_hash);

    anedota_tree_entry_t entries[] = {
        { "subtree", ANEDOTA_TREE_ENTRY_TREE, empty_tree_hash },
        { "alpha.txt", ANEDOTA_TREE_ENTRY_BLOB, blob_hash }
    };
    char tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, entries, 2, tree_hash) == 0);
    record_hash(tree_hash);

    anedota_tree_entry_t reversed[] = { entries[1], entries[0] };
    char reversed_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, reversed, 2, reversed_hash) == 0);
    record_hash(reversed_hash);
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

    cleanup_tree_test();
    printf("[PASS] test_tree_round_trip_and_determinism\n");
}

static void test_tree_invalid_inputs(void)
{
    cleanup_tree_test();
    assert(anedota_storage_init(root) == 0);

    write_test_file("test_tree_blob.txt", "valid child");
    char child_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_blob_write_file(root, "test_tree_blob.txt", child_hash) == 0);
    record_hash(child_hash);

    char unchanged[ANEDOTA_SHA256_HEX_SIZE] = "unchanged";
    anedota_tree_entry_t entry = {
        "valid", ANEDOTA_TREE_ENTRY_BLOB, child_hash
    };
    assert(anedota_tree_write(NULL, &entry, 1, unchanged) == -1);
    assert(anedota_tree_write("", &entry, 1, unchanged) == -1);
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

    anedota_tree_entry_t invalid_name = {
        "../escape", ANEDOTA_TREE_ENTRY_BLOB, child_hash
    };
    assert(anedota_tree_write(root, &invalid_name, 1, unchanged) == -1);

    anedota_tree_entry_t invalid_type = {
        "valid", (anedota_tree_entry_type_t)3, child_hash
    };
    assert(anedota_tree_write(root, &invalid_type, 1, unchanged) == -1);
    assert(strcmp(unchanged, "unchanged") == 0);

    anedota_tree_t *tree = (anedota_tree_t *)1;
    assert(anedota_tree_read(root, NULL, &tree) == -1);
    assert(anedota_tree_read(root,
        "0000000000000000000000000000000000000000000000000000000000000000",
        &tree) == -1);
    assert(tree == (anedota_tree_t *)1);

    write_test_file("test_tree_invalid.txt", "not a tree");
    char invalid_tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_blob_write_file(root, "test_tree_invalid.txt",
                                   invalid_tree_hash) == 0);
    record_hash(invalid_tree_hash);
    assert(anedota_tree_read(root, invalid_tree_hash, &tree) == -1);
    assert(tree == (anedota_tree_t *)1);

    cleanup_tree_test();
    printf("[PASS] test_tree_invalid_inputs\n");
}

int main(void)
{
    printf("Executando testes de Tree...\n");
    test_tree_round_trip_and_determinism();
    test_tree_invalid_inputs();
    printf("Testes de Tree concluídos com sucesso\n");
    return 0;
}
