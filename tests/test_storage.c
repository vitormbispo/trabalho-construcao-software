#include <anedota/storage.h>
#include <assert.h>
#include <pal/pal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

static int is_directory(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void write_test_file(const char *path, const void *data, size_t size)
{
    struct pal_file *file = pal_open(path, "wb");
    assert(file != NULL);
    if (size > 0)
        assert(pal_write(file, (const char *)data, size) == (int)size);
    assert(pal_close(file) == 0);
}

static void cleanup_storage(void)
{
    unlink("test_anedota_storage/objetos/sentinel");
    unlink("test_anedota_storage/objetos/ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    unlink("test_anedota_storage/objetos/e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    unlink("test_anedota_storage/objetos/9e8a7798d05322487ac8cfe4b88e15f6dc4a19d718b37b8675476f691468f534");
    unlink("test_blob_source_a.txt");
    unlink("test_blob_source_b.bin");
    unlink("test_blob_empty.txt");
    pal_dir_delete("test_anedota_storage/referencias/branches");
    pal_dir_delete("test_anedota_storage/referencias");
    pal_dir_delete("test_anedota_storage/objetos");
    pal_dir_delete("test_anedota_storage");
}

static void test_storage_init(void)
{
    cleanup_storage();

    assert(anedota_storage_init(NULL) == -1);
    assert(anedota_storage_init("") == -1);
    assert(anedota_storage_init("test_anedota_storage") == 0);

    assert(is_directory("test_anedota_storage"));
    assert(is_directory("test_anedota_storage/objetos"));
    assert(is_directory("test_anedota_storage/referencias"));
    assert(is_directory("test_anedota_storage/referencias/branches"));

    const char *sentinel_path = "test_anedota_storage/objetos/sentinel";
    struct pal_file *sentinel = pal_open(sentinel_path, "wb");
    assert(sentinel != NULL);
    assert(pal_write(sentinel, "preservar", 9) == 9);
    assert(pal_close(sentinel) == 0);

    assert(anedota_storage_init("test_anedota_storage") == 0);
    sentinel = pal_open(sentinel_path, "rb");
    assert(sentinel != NULL);
    char content[9];
    assert(pal_read(sentinel, content, sizeof(content)) == 9);
    assert(memcmp(content, "preservar", sizeof(content)) == 0);
    assert(pal_close(sentinel) == 0);

    cleanup_storage();
    assert(!is_directory("test_anedota_storage"));

    printf("[PASS] test_storage_init\n");
}

static void test_blob_write_file(void)
{
    cleanup_storage();

    const char *root = "test_anedota_storage";
    const char *source_a = "test_blob_source_a.txt";
    const char *source_b = "test_blob_source_b.bin";
    const char *empty_source = "test_blob_empty.txt";
    const char *abc_hash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const char *empty_hash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    const char *binary_hash = "9e8a7798d05322487ac8cfe4b88e15f6dc4a19d718b37b8675476f691468f534";

    write_test_file(source_a, "abc", 3);

    /* Falha se o armazenamento não foi inicializado ou argumentos forem inválidos */
    char hash[ANEDOTA_SHA256_HEX_SIZE] = "unchanged";
    assert(anedota_blob_write_file(root, source_a, hash) == -1);
    assert(strcmp(hash, "unchanged") == 0);

    assert(anedota_storage_init(root) == 0);
    assert(anedota_blob_write_file(NULL, source_a, hash) == -1);
    assert(anedota_blob_write_file("", source_a, hash) == -1);
    assert(anedota_blob_write_file(root, NULL, hash) == -1);
    assert(anedota_blob_write_file(root, "", hash) == -1);
    assert(anedota_blob_write_file(root, source_a, NULL) == -1);
    assert(anedota_blob_write_file(root, "arquivo_inexistente_12345.txt", hash) == -1);
    assert(strcmp(hash, "unchanged") == 0);

    /* Salva o Blob identificado pelo hash contendo apenas o conteúdo bruto (sem metadados) */
    struct utimbuf time_a = { 1000000000, 1000000000 };
    assert(utime(source_a, &time_a) == 0);
    assert(chmod(source_a, 0755) == 0);

    assert(anedota_blob_write_file(root, source_a, hash) == 0);
    assert(strcmp(hash, abc_hash) == 0);

    const char *abc_blob_path =
        "test_anedota_storage/objetos/ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    struct pal_file *blob = pal_open(abc_blob_path, "rb");
    assert(blob != NULL);
    char buffer[16];
    assert(pal_read(blob, buffer, sizeof(buffer)) == 3);
    assert(memcmp(buffer, "abc", 3) == 0);
    assert(pal_read(blob, buffer, sizeof(buffer)) == 0);
    assert(pal_close(blob) == 0);

    /* Outro arquivo com nome, data e permissões diferentes gera o mesmo hash e conteúdo */
    write_test_file(source_b, "abc", 3);
    struct utimbuf time_b = { 1700000000, 1700000000 };
    assert(utime(source_b, &time_b) == 0);
    assert(chmod(source_b, 0600) == 0);

    char hash_b[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_blob_write_file("test_anedota_storage/", source_b, hash_b) == 0);
    assert(strcmp(hash_b, abc_hash) == 0);

    /* Conteúdo binário com bytes nulos e arquivo vazio */
    const unsigned char binary[] = { 0, 1, 255, 'a', 'b', 'c', 0 };
    write_test_file(source_b, binary, sizeof(binary));
    assert(anedota_blob_write_file(root, source_b, hash) == 0);
    assert(strcmp(hash, binary_hash) == 0);

    const char *binary_blob_path =
        "test_anedota_storage/objetos/9e8a7798d05322487ac8cfe4b88e15f6dc4a19d718b37b8675476f691468f534";
    blob = pal_open(binary_blob_path, "rb");
    assert(blob != NULL);
    assert(pal_read(blob, buffer, sizeof(buffer)) == (int)sizeof(binary));
    assert(memcmp(buffer, binary, sizeof(binary)) == 0);
    assert(pal_close(blob) == 0);

    write_test_file(empty_source, "", 0);
    assert(anedota_blob_write_file(root, empty_source, hash) == 0);
    assert(strcmp(hash, empty_hash) == 0);

    const char *empty_blob_path =
        "test_anedota_storage/objetos/e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    blob = pal_open(empty_blob_path, "rb");
    assert(blob != NULL);
    assert(pal_read(blob, buffer, sizeof(buffer)) == 0);
    assert(pal_close(blob) == 0);

    cleanup_storage();
    printf("[PASS] test_blob_write_file\n");
}

int main(void)
{
    printf("Executando testes do armazenamento...\n");
    test_storage_init();
    test_blob_write_file();
    printf("Testes do armazenamento concluídos com sucesso\n");
    return 0;
}
