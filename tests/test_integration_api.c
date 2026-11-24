#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#endif

#include <anedota/anedota.h>
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

/* Simula um software hospedeiro usando APENAS libanedota.a sobre dados
 * genéricos em memória (sem executar a CLI e sem arquivos de origem). */
static void test_host_generic_memory_full_workflow(void)
{
    char workspace[] = "/tmp/anedota-api-int-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    join_path(root, sizeof(root), workspace, "dados_hospedeiro");

    /* 1. Inicializar armazenamento */
    assert(anedota_storage_init(root) == 0);

    /* Histórico inicialmente vazio */
    anedota_log_t *empty_log = NULL;
    assert(anedota_log_read(root, &empty_log) == 0);
    assert(empty_log->count == 0);
    free(empty_log);

    /* 2. Armazenar fluxos de bytes genéricos (vazio, texto UTF-8 e binário com NUL) */
    const unsigned char bin_v1[] = { 0x00, 0x01, 0xFE, 0xFF, 0x00, 0x42 };
    const char doc_v1[] = "Documento genérico v1";
    char h_empty[ANEDOTA_SHA256_HEX_SIZE];
    char h_bin_v1[ANEDOTA_SHA256_HEX_SIZE];
    char h_doc_v1[ANEDOTA_SHA256_HEX_SIZE];

    assert(anedota_blob_write(root, NULL, 0, h_empty) == 0);
    assert(strcmp(h_empty,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    assert(anedota_blob_write(root, bin_v1, sizeof(bin_v1), h_bin_v1) == 0);
    assert(anedota_blob_write(root, doc_v1, strlen(doc_v1), h_doc_v1) == 0);

    /* 3. Recuperar e verificar fidelidade bit a bit */
    char *read_data = NULL;
    size_t read_size = 0;
    assert(anedota_blob_read(root, h_bin_v1, &read_data, &read_size) == 0);
    assert(read_size == sizeof(bin_v1));
    assert(memcmp(read_data, bin_v1, sizeof(bin_v1)) == 0);
    free(read_data);

    /* 4. Criar Tree e Commit v1 */
    anedota_tree_entry_t t1_entries[] = {
        { "doc.txt", ANEDOTA_TREE_ENTRY_BLOB, h_doc_v1 },
        { "payload.bin", ANEDOTA_TREE_ENTRY_BLOB, h_bin_v1 },
        { "vazio.dat", ANEDOTA_TREE_ENTRY_BLOB, h_empty }
    };
    char tree_v1[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, t1_entries, 3, tree_v1) == 0);

    anedota_commit_t c1_in = {
        tree_v1, NULL, "HostApp", "2026-11-24T19:20:00Z", "Snapshot v1"
    };
    char c1[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_commit_write(root, &c1_in, c1) == 0);

    /* 5. Evoluir estado em memória e criar Commit v2 */
    const char doc_v2[] = "Documento genérico v2 atualizado";
    const char novo_v2[] = "{\"chave\":true}";
    char h_doc_v2[ANEDOTA_SHA256_HEX_SIZE];
    char h_novo_v2[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_blob_write(root, doc_v2, strlen(doc_v2), h_doc_v2) == 0);
    assert(anedota_blob_write(root, novo_v2, strlen(novo_v2), h_novo_v2) == 0);

    anedota_tree_entry_t sub_entries[] = {
        { "meta.json", ANEDOTA_TREE_ENTRY_BLOB, h_novo_v2 }
    };
    char sub_tree[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, sub_entries, 1, sub_tree) == 0);

    anedota_tree_entry_t t2_entries[] = {
        { "doc.txt", ANEDOTA_TREE_ENTRY_BLOB, h_doc_v2 },
        { "meta", ANEDOTA_TREE_ENTRY_TREE, sub_tree },
        { "vazio.dat", ANEDOTA_TREE_ENTRY_BLOB, h_empty }
    };
    char tree_v2[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_tree_write(root, t2_entries, 3, tree_v2) == 0);

    char head_before_c2[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_ref_read_head(root, head_before_c2) == 1);
    assert(strcmp(head_before_c2, c1) == 0);

    anedota_commit_t c2_in = {
        tree_v2, head_before_c2, "HostApp", "2026-11-24T19:25:00Z", "Snapshot v2"
    };
    char c2[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_commit_write(root, &c2_in, c2) == 0);

    /* 6. Consultar histórico completo */
    anedota_log_t *log = NULL;
    assert(anedota_log_read(root, &log) == 0);
    assert(log->count == 2);
    assert(strcmp(log->entries[0].hash, c2) == 0);
    assert(strcmp(log->entries[0].message, "Snapshot v2") == 0);
    assert(strcmp(log->entries[1].hash, c1) == 0);
    assert(strcmp(log->entries[1].message, "Snapshot v1") == 0);
    free(log);

    /* 7. Comparar versões c1 -> c2 e c2 -> c2 */
    anedota_diff_t *diff = NULL;
    assert(anedota_diff_versions(root, c1, c2, &diff) == ANEDOTA_DIFF_OK);
    assert(diff->count == 3);
    assert(strcmp(diff->entries[0].path, "doc.txt") == 0);
    assert(diff->entries[0].kind == ANEDOTA_DIFF_MODIFIED);
    assert(strcmp(diff->entries[1].path, "meta/meta.json") == 0);
    assert(diff->entries[1].kind == ANEDOTA_DIFF_ADDED);
    assert(strcmp(diff->entries[2].path, "payload.bin") == 0);
    assert(diff->entries[2].kind == ANEDOTA_DIFF_REMOVED);
    free(diff);

    anedota_diff_t *same = NULL;
    assert(anedota_diff_versions(root, c2, c2, &same) == ANEDOTA_DIFF_OK);
    assert(same->count == 0);
    free(same);

    /* 8. Recuperar dados da versão histórica c1 via Tree + Blob */
    anedota_commit_t *loaded_c1 = NULL;
    assert(anedota_commit_read(root, c1, &loaded_c1) == 0);
    anedota_tree_t *loaded_t1 = NULL;
    assert(anedota_tree_read(root, loaded_c1->tree_hash, &loaded_t1) == 0);
    assert(loaded_t1->count == 3);
    assert(strcmp(loaded_t1->entries[0].name, "doc.txt") == 0);

    char *old_doc = NULL;
    size_t old_doc_size = 0;
    assert(anedota_blob_read(root, loaded_t1->entries[0].hash,
                             &old_doc, &old_doc_size) == 0);
    assert(old_doc_size == strlen(doc_v1));
    assert(strcmp(old_doc, doc_v1) == 0);

    free(old_doc);
    free(loaded_t1);
    free(loaded_c1);

    remove_tree_recursive(workspace);
    printf("[PASS] test_host_generic_memory_full_workflow\n");
}

static void test_in_memory_hash_and_blob_edge_cases(void)
{
    char workspace[] = "/tmp/anedota-api-mem-XXXXXX";
    assert(mkdtemp(workspace) != NULL);

    char root[512];
    join_path(root, sizeof(root), workspace, ".anedota");
    assert(anedota_storage_init(root) == 0);

    char hash_abc[ANEDOTA_SHA256_HEX_SIZE];
    char hash_empty[ANEDOTA_SHA256_HEX_SIZE];
    char unchanged[ANEDOTA_SHA256_HEX_SIZE] = "unchanged";

    assert(anedota_sha256_bytes("abc", 3, hash_abc) == 0);
    assert(strcmp(hash_abc,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    assert(anedota_sha256_bytes(NULL, 0, hash_empty) == 0);
    assert(strcmp(hash_empty,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    assert(anedota_sha256_bytes(NULL, 5, unchanged) == -1);
    assert(anedota_sha256_bytes("abc", 3, NULL) == -1);

    char blob_a[ANEDOTA_SHA256_HEX_SIZE];
    char blob_b[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_blob_write(root, "abc", 3, blob_a) == 0);
    assert(anedota_blob_write(root, "abc", 3, blob_b) == 0);
    assert(strcmp(blob_a, hash_abc) == 0);
    assert(strcmp(blob_b, hash_abc) == 0);

    assert(anedota_blob_write(NULL, "abc", 3, unchanged) == -1);
    assert(anedota_blob_write("", "abc", 3, unchanged) == -1);
    assert(anedota_blob_write(root, NULL, 3, unchanged) == -1);
    assert(anedota_blob_write(root, "abc", 3, NULL) == -1);
    assert(strcmp(unchanged, "unchanged") == 0);

    remove_tree_recursive(workspace);
    printf("[PASS] test_in_memory_hash_and_blob_edge_cases\n");
}

int main(void)
{
    printf("Executando testes de integracao da biblioteca libanedota...\n");
    test_host_generic_memory_full_workflow();
    test_in_memory_hash_and_blob_edge_cases();
    printf("Testes de integracao da biblioteca concluídos com sucesso\n");
    return 0;
}
