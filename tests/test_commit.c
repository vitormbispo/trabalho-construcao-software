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

static void write_text_file(const char *base, const char *relative,
                            const char *text)
{
    char path[512];
    join_path(path, sizeof(path), base, relative);
    write_bytes(path, text, strlen(text));
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

static void assert_commit_fields(const anedota_commit_t *commit,
                                 const char *expected_tree,
                                 const char *expected_parent,
                                 const char *expected_author,
                                 const char *expected_timestamp,
                                 const char *expected_message)
{
    assert(commit != NULL);
    assert(strcmp(commit->tree_hash, expected_tree) == 0);
    if (expected_parent == NULL)
    {
        assert(commit->parent_hash == NULL);
    }
    else
    {
        assert(commit->parent_hash != NULL);
        assert(strcmp(commit->parent_hash, expected_parent) == 0);
    }
    assert(strcmp(commit->author, expected_author) == 0);
    assert(strcmp(commit->timestamp, expected_timestamp) == 0);
    assert(strcmp(commit->message, expected_message) == 0);
}

static void assert_head_equals(const char *root, const char *expected_hash)
{
    char head[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_ref_read_head(root, head) == 1);
    assert(strcmp(head, expected_hash) == 0);
}

static void assert_commit_read_fails(const char *root, const char *hash)
{
    anedota_commit_t *out = (anedota_commit_t *)1;
    assert(anedota_commit_read(root, hash, &out) == -1);
    assert(out == (anedota_commit_t *)1);
}

static void assert_corrupt_commit_rejected(const char *root,
                                           const char *tmp_path,
                                           const void *payload,
                                           size_t size)
{
    char hash[ANEDOTA_SHA256_HEX_SIZE];
    write_bytes(tmp_path, payload, size);
    assert(anedota_blob_write_file(root, tmp_path, hash) == 0);
    assert_commit_read_fails(root, hash);
}

static void verify_serialized_root_commit(const char *root,
                                          const char *commit_hash,
                                          const char *tree_hash,
                                          const char *author,
                                          const char *timestamp,
                                          const char *message)
{
    char *payload = NULL;
    size_t size = 0;
    assert(anedota_blob_read(root, commit_hash, &payload, &size) == 0);

    const size_t author_len = strlen(author);
    const size_t ts_len = strlen(timestamp);
    const size_t msg_len = strlen(message);
    assert(size == 8 + 1 + 64 + 1 + 4 + author_len + 4 + ts_len + 4 + msg_len);
    assert(memcmp(payload, "ANEDOTAC", 8) == 0);
    assert(payload[8] == 1);
    assert(memcmp(payload + 9, tree_hash, 64) == 0);
    assert(payload[73] == 0);

    size_t offset = 74;
    check_u32_be((const unsigned char *)payload + offset, (uint32_t)author_len);
    offset += 4;
    assert(memcmp(payload + offset, author, author_len) == 0);
    offset += author_len;

    check_u32_be((const unsigned char *)payload + offset, (uint32_t)ts_len);
    offset += 4;
    assert(memcmp(payload + offset, timestamp, ts_len) == 0);
    offset += ts_len;

    check_u32_be((const unsigned char *)payload + offset, (uint32_t)msg_len);
    offset += 4;
    assert(memcmp(payload + offset, message, msg_len) == 0);
    free(payload);
}

static void test_first_commit_without_parent_and_format(void)
{
    char workspace[] = "/tmp/anedota-commit-root-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    assert(anedota_storage_init(root) == 0);

    char head[ANEDOTA_SHA256_HEX_SIZE] = "unset";
    assert(anedota_ref_read_head(root, head) == 0);
    assert(head[0] == '\0');

    char tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, NULL, 0, tree_hash) == 0);

    anedota_commit_t input = {
        tree_hash, NULL, "Vitor", "2026-11-24T16:30:00Z",
        "Commit inicial\ncom multiplas linhas"
    };
    char hash_a[ANEDOTA_SHA256_HEX_SIZE];
    char hash_b[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_commit_write(root, &input, hash_a) == 0);
    assert(anedota_commit_write(root, &input, hash_b) == 0);
    assert(strcmp(hash_a, hash_b) == 0);
    assert_head_equals(root, hash_a);

    anedota_commit_t *loaded = NULL;
    assert(anedota_commit_read(root, hash_a, &loaded) == 0);
    assert_commit_fields(loaded, tree_hash, NULL, "Vitor",
                         "2026-11-24T16:30:00Z",
                         "Commit inicial\ncom multiplas linhas");
    free(loaded);

    verify_serialized_root_commit(root, hash_a, tree_hash, "Vitor",
                                  "2026-11-24T16:30:00Z",
                                  "Commit inicial\ncom multiplas linhas");

    remove_tree_recursive(workspace);
    printf("[PASS] test_first_commit_without_parent_and_format\n");
}

static void test_sequential_commits_chain_and_head_updates(void)
{
    char workspace[] = "/tmp/anedota-commit-chain-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    assert(anedota_storage_init(root) == 0);

    char tree_1[ANEDOTA_SHA256_HEX_SIZE];
    char tree_2[ANEDOTA_SHA256_HEX_SIZE];
    char tree_3[ANEDOTA_SHA256_HEX_SIZE];
    char commit_1[ANEDOTA_SHA256_HEX_SIZE];
    char commit_2[ANEDOTA_SHA256_HEX_SIZE];
    char commit_3[ANEDOTA_SHA256_HEX_SIZE];

    write_text_file(workspace, "app.txt", "versao 1");
    assert(anedota_tree_write_directory(root, workspace, tree_1) == 0);
    anedota_commit_t input_1 = {
        tree_1, NULL, "Vitor", "2026-11-24T16:30:00Z", "Versao 1"
    };
    assert(anedota_commit_write(root, &input_1, commit_1) == 0);
    assert_head_equals(root, commit_1);

    write_text_file(workspace, "app.txt", "versao 2");
    assert(anedota_tree_write_directory(root, workspace, tree_2) == 0);
    anedota_commit_t input_2 = {
        tree_2, commit_1, "Bruno", "2026-11-24T16:35:00Z", "Versao 2"
    };
    assert(anedota_commit_write(root, &input_2, commit_2) == 0);
    assert(strcmp(commit_1, commit_2) != 0);
    assert_head_equals(root, commit_2);

    write_text_file(workspace, "extra.txt", "novo arquivo");
    assert(anedota_tree_write_directory(root, workspace, tree_3) == 0);
    anedota_commit_t input_3 = {
        tree_3, commit_2, "Julio", "2026-11-24T16:40:00Z", "Versao 3"
    };
    assert(anedota_commit_write(root, &input_3, commit_3) == 0);
    assert(strcmp(commit_2, commit_3) != 0);
    assert_head_equals(root, commit_3);

    anedota_commit_t *c3 = NULL;
    anedota_commit_t *c2 = NULL;
    anedota_commit_t *c1 = NULL;
    assert(anedota_commit_read(root, commit_3, &c3) == 0);
    assert_commit_fields(c3, tree_3, commit_2, "Julio",
                         "2026-11-24T16:40:00Z", "Versao 3");
    assert(anedota_commit_read(root, c3->parent_hash, &c2) == 0);
    assert_commit_fields(c2, tree_2, commit_1, "Bruno",
                         "2026-11-24T16:35:00Z", "Versao 2");
    assert(anedota_commit_read(root, c2->parent_hash, &c1) == 0);
    assert_commit_fields(c1, tree_1, NULL, "Vitor",
                         "2026-11-24T16:30:00Z", "Versao 1");

    free(c1);
    free(c2);
    free(c3);
    remove_tree_recursive(workspace);
    printf("[PASS] test_sequential_commits_chain_and_head_updates\n");
}

static void test_commit_rejects_invalid_tree_parent_and_fields(void)
{
    char workspace[] = "/tmp/anedota-commit-inv-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    char uninit[512];
    char blob_file[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    join_path(uninit, sizeof(uninit), workspace, "sem_repo");
    join_path(blob_file, sizeof(blob_file), workspace, "plain.txt");
    assert(anedota_storage_init(root) == 0);

    char tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    char blob_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, NULL, 0, tree_hash) == 0);
    write_bytes(blob_file, "apenas um blob", 14);
    assert(anedota_blob_write_file(root, blob_file, blob_hash) == 0);

    anedota_commit_t initial = {
        tree_hash, NULL, "Vitor", "2026-11-24T16:30:00Z", "Base"
    };
    char initial_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_commit_write(root, &initial, initial_hash) == 0);

    const char *missing =
        "0000000000000000000000000000000000000000000000000000000000000000";
    char unchanged[ANEDOTA_SHA256_HEX_SIZE] = "unchanged";

    anedota_commit_t invalid_cases[] = {
        { NULL, NULL, "Vitor", "2026-11-24T16:30:00Z", "msg" },
        { "hash-curto", NULL, "Vitor", "2026-11-24T16:30:00Z", "msg" },
        { missing, NULL, "Vitor", "2026-11-24T16:30:00Z", "msg" },
        { blob_hash, NULL, "Vitor", "2026-11-24T16:30:00Z", "msg" },
        { tree_hash, "pai-curto", "Vitor", "2026-11-24T16:30:00Z", "msg" },
        { tree_hash, missing, "Vitor", "2026-11-24T16:30:00Z", "msg" },
        { tree_hash, tree_hash, "Vitor", "2026-11-24T16:30:00Z", "msg" },
        { tree_hash, blob_hash, "Vitor", "2026-11-24T16:30:00Z", "msg" },
        { tree_hash, NULL, NULL, "2026-11-24T16:30:00Z", "msg" },
        { tree_hash, NULL, "", "2026-11-24T16:30:00Z", "msg" },
        { tree_hash, NULL, "Vitor", NULL, "msg" },
        { tree_hash, NULL, "Vitor", "", "msg" },
        { tree_hash, NULL, "Vitor", "2026-11-24T16:30:00Z", NULL },
        { tree_hash, NULL, "Vitor", "2026-11-24T16:30:00Z", "" }
    };

    assert(anedota_commit_write(NULL, &initial, unchanged) == -1);
    assert(anedota_commit_write("", &initial, unchanged) == -1);
    assert(anedota_commit_write(uninit, &initial, unchanged) == -1);
    assert(anedota_commit_write(root, NULL, unchanged) == -1);
    assert(anedota_commit_write(root, &initial, NULL) == -1);

    for (size_t i = 0; i < sizeof(invalid_cases) / sizeof(invalid_cases[0]); ++i)
    {
        assert(anedota_commit_write(root, &invalid_cases[i], unchanged) == -1);
    }
    assert(strcmp(unchanged, "unchanged") == 0);
    assert_head_equals(root, initial_hash);

    remove_tree_recursive(workspace);
    printf("[PASS] test_commit_rejects_invalid_tree_parent_and_fields\n");
}

static size_t build_raw_commit(unsigned char *out, const char *tree_hash,
                               unsigned char version, unsigned char has_parent,
                               int append_trailing_byte)
{
    size_t pos = 0;
    memcpy(out + pos, "ANEDOTAC", 8);
    pos += 8;
    out[pos++] = version;
    memcpy(out + pos, tree_hash, 64);
    pos += 64;
    out[pos++] = has_parent;
    for (int i = 0; i < 3; ++i)
    {
        out[pos++] = 0;
        out[pos++] = 0;
        out[pos++] = 0;
        out[pos++] = 1;
        out[pos++] = 'x';
    }
    if (append_trailing_byte) out[pos++] = 0xFF;
    return pos;
}

static void test_commit_read_rejects_corrupt_payloads(void)
{
    char workspace[] = "/tmp/anedota-commit-corrupt-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    char uninit[512];
    char tmp_file[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    join_path(uninit, sizeof(uninit), workspace, "sem_repo");
    join_path(tmp_file, sizeof(tmp_file), workspace, "payload.bin");
    assert(anedota_storage_init(root) == 0);

    char tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, NULL, 0, tree_hash) == 0);

    const char *missing =
        "0000000000000000000000000000000000000000000000000000000000000000";
    assert_commit_read_fails(NULL, tree_hash);
    assert_commit_read_fails("", tree_hash);
    assert_commit_read_fails(uninit, tree_hash);
    assert_commit_read_fails(root, NULL);
    assert(anedota_commit_read(root, tree_hash, NULL) == -1);
    assert_commit_read_fails(root, missing);
    assert_commit_read_fails(root, tree_hash);

    unsigned char raw[128];
    size_t len = build_raw_commit(raw, tree_hash, 2, 0, 0);
    assert_corrupt_commit_rejected(root, tmp_file, raw, len);

    len = build_raw_commit(raw, tree_hash, 1, 2, 0);
    assert_corrupt_commit_rejected(root, tmp_file, raw, len);

    len = build_raw_commit(raw, tree_hash, 1, 0, 1);
    assert_corrupt_commit_rejected(root, tmp_file, raw, len);

    len = build_raw_commit(raw, tree_hash, 1, 0, 0);
    assert_corrupt_commit_rejected(root, tmp_file, raw, len - 2);

    raw[78] = '\0';
    assert_corrupt_commit_rejected(root, tmp_file, raw, len);

    assert_corrupt_commit_rejected(root, tmp_file, "nao e commit", 12);

    remove_tree_recursive(workspace);
    printf("[PASS] test_commit_read_rejects_corrupt_payloads\n");
}

static void test_detached_head_read_and_update(void)
{
    char workspace[] = "/tmp/anedota-commit-dethead-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    char head_path[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    join_path(head_path, sizeof(head_path), root, "HEAD");
    assert(anedota_storage_init(root) == 0);

    char tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, NULL, 0, tree_hash) == 0);

    anedota_commit_t c1_in = {
        tree_hash, NULL, "Vitor", "2026-11-24T16:30:00Z", "C1"
    };
    char c1_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_commit_write(root, &c1_in, c1_hash) == 0);

    char detached_payload[ANEDOTA_SHA256_HEX_SIZE + 1];
    assert(snprintf(detached_payload, sizeof(detached_payload),
                    "%s\n", c1_hash) == ANEDOTA_SHA256_HEX_SIZE);
    write_bytes(head_path, detached_payload, ANEDOTA_SHA256_HEX_SIZE);
    assert_head_equals(root, c1_hash);

    anedota_commit_t c2_in = {
        tree_hash, c1_hash, "Vitor", "2026-11-24T16:35:00Z", "C2"
    };
    char c2_hash[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_commit_write(root, &c2_in, c2_hash) == 0);
    assert_head_equals(root, c2_hash);

    remove_tree_recursive(workspace);
    printf("[PASS] test_detached_head_read_and_update\n");
}

int main(void)
{
    printf("Executando testes de Commit...\n");
    test_first_commit_without_parent_and_format();
    test_sequential_commits_chain_and_head_updates();
    test_commit_rejects_invalid_tree_parent_and_fields();
    test_commit_read_rejects_corrupt_payloads();
    test_detached_head_read_and_update();
    printf("Testes de Commit concluídos com sucesso\n");
    return 0;
}
