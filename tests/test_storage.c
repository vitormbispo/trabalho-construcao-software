#include <anedota/storage.h>
#include <assert.h>
#include <dirent.h>
#include <pal/pal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

static int is_directory(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int count_directory_entries(const char *path)
{
    DIR *dir = opendir(path);
    assert(dir != NULL);
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            ++count;
    }
    assert(closedir(dir) == 0);
    return count;
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
    unlink("test_anedota_storage/objetos/1e973d029df2b2c66cb42a942c5edb45966f02abaff29fe99410e44d271d0efc");
    unlink("test_blob_source_a.txt");
    unlink("test_blob_source_b.bin");
    unlink("test_blob_empty.txt");
    unlink("test_blob_large.bin");
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

static void test_blob_write_new(void)
{
    cleanup_storage();

    const char *root = "test_anedota_storage";
    const char *objects_dir = "test_anedota_storage/objetos";
    const char *source_a = "test_blob_source_a.txt";
    const char *source_b = "test_blob_source_b.bin";
    const char *empty_source = "test_blob_empty.txt";
    const char *large_source = "test_blob_large.bin";
    const char *abc_hash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const char *empty_hash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    const char *binary_hash = "9e8a7798d05322487ac8cfe4b88e15f6dc4a19d718b37b8675476f691468f534";
    const char *large_hash = "1e973d029df2b2c66cb42a942c5edb45966f02abaff29fe99410e44d271d0efc";

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
    assert(count_directory_entries(objects_dir) == 0);

    /* Salva um Blob novo identificado pelo hash contendo apenas o conteúdo bruto (sem metadados) */
    struct utimbuf time_a = { 1000000000, 1000000000 };
    assert(utime(source_a, &time_a) == 0);
    assert(chmod(source_a, 0755) == 0);

    assert(anedota_blob_write_file(root, source_a, hash) == 0);
    assert(strcmp(hash, abc_hash) == 0);
    assert(count_directory_entries(objects_dir) == 1);

    const char *abc_blob_path =
        "test_anedota_storage/objetos/ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    struct pal_file *blob = pal_open(abc_blob_path, "rb");
    assert(blob != NULL);
    char buffer[16];
    assert(pal_read(blob, buffer, sizeof(buffer)) == 3);
    assert(memcmp(buffer, "abc", 3) == 0);
    assert(pal_read(blob, buffer, sizeof(buffer)) == 0);
    assert(pal_close(blob) == 0);

    /* Blob novo com conteúdo binário (incluindo bytes nulos) */
    const unsigned char binary[] = { 0, 1, 255, 'a', 'b', 'c', 0 };
    write_test_file(source_b, binary, sizeof(binary));
    assert(anedota_blob_write_file(root, source_b, hash) == 0);
    assert(strcmp(hash, binary_hash) == 0);
    assert(count_directory_entries(objects_dir) == 2);

    const char *binary_blob_path =
        "test_anedota_storage/objetos/9e8a7798d05322487ac8cfe4b88e15f6dc4a19d718b37b8675476f691468f534";
    blob = pal_open(binary_blob_path, "rb");
    assert(blob != NULL);
    assert(pal_read(blob, buffer, sizeof(buffer)) == (int)sizeof(binary));
    assert(memcmp(buffer, binary, sizeof(binary)) == 0);
    assert(pal_close(blob) == 0);

    /* Blob novo para arquivo vazio */
    write_test_file(empty_source, "", 0);
    assert(anedota_blob_write_file(root, empty_source, hash) == 0);
    assert(strcmp(hash, empty_hash) == 0);
    assert(count_directory_entries(objects_dir) == 3);

    const char *empty_blob_path =
        "test_anedota_storage/objetos/e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    blob = pal_open(empty_blob_path, "rb");
    assert(blob != NULL);
    assert(pal_read(blob, buffer, sizeof(buffer)) == 0);
    assert(pal_close(blob) == 0);

    /* Blob novo com múltiplos blocos (> 4096 bytes) */
    unsigned char large_bytes[4097];
    for (size_t i = 0; i < sizeof(large_bytes); ++i)
        large_bytes[i] = (unsigned char)i;
    write_test_file(large_source, large_bytes, sizeof(large_bytes));
    assert(anedota_blob_write_file(root, large_source, hash) == 0);
    assert(strcmp(hash, large_hash) == 0);
    assert(count_directory_entries(objects_dir) == 4);

    const char *large_blob_path =
        "test_anedota_storage/objetos/1e973d029df2b2c66cb42a942c5edb45966f02abaff29fe99410e44d271d0efc";
    blob = pal_open(large_blob_path, "rb");
    assert(blob != NULL);
    unsigned char large_read[4097];
    assert(pal_read(blob, (char *)large_read, sizeof(large_read)) == (int)sizeof(large_read));
    assert(memcmp(large_read, large_bytes, sizeof(large_bytes)) == 0);
    assert(pal_read(blob, buffer, sizeof(buffer)) == 0);
    assert(pal_close(blob) == 0);

    cleanup_storage();
    printf("[PASS] test_blob_write_new\n");
}

static void test_blob_write_duplicate(void)
{
    cleanup_storage();

    const char *root = "test_anedota_storage";
    const char *objects_dir = "test_anedota_storage/objetos";
    const char *source_a = "test_blob_source_a.txt";
    const char *source_b = "test_blob_source_b.bin";
    const char *abc_hash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const char *abc_blob_path =
        "test_anedota_storage/objetos/ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

    assert(anedota_storage_init(root) == 0);

    write_test_file(source_a, "abc", 3);
    struct utimbuf time_a = { 1000000000, 1000000000 };
    assert(utime(source_a, &time_a) == 0);
    assert(chmod(source_a, 0755) == 0);

    char hash_a[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_blob_write_file(root, source_a, hash_a) == 0);
    assert(strcmp(hash_a, abc_hash) == 0);
    assert(count_directory_entries(objects_dir) == 1);

    struct stat st_initial, st_after;
    assert(stat(abc_blob_path, &st_initial) == 0);

    /* Regravar o mesmo arquivo de origem mantém um único Blob e preserva o arquivo original */
    char hash_repeat[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_blob_write_file(root, source_a, hash_repeat) == 0);
    assert(strcmp(hash_repeat, abc_hash) == 0);
    assert(count_directory_entries(objects_dir) == 1);
    assert(stat(abc_blob_path, &st_after) == 0);
    assert(st_initial.st_ino == st_after.st_ino);

    /* O próprio objeto pode ser usado como origem sem perder seus bytes nem criar arquivos extras */
    assert(anedota_blob_write_file(root, abc_blob_path, hash_repeat) == 0);
    assert(strcmp(hash_repeat, abc_hash) == 0);
    assert(count_directory_entries(objects_dir) == 1);
    assert(stat(abc_blob_path, &st_after) == 0);
    assert(st_initial.st_ino == st_after.st_ino);

    /* Outro arquivo com nome, data e permissões diferentes gera o mesmo hash e um único Blob em disco */
    write_test_file(source_b, "abc", 3);
    struct utimbuf time_b = { 1700000000, 1700000000 };
    assert(utime(source_b, &time_b) == 0);
    assert(chmod(source_b, 0600) == 0);

    char hash_b[ANEDOTA_SHA256_HEX_SIZE];
    assert(anedota_blob_write_file("test_anedota_storage/", source_b, hash_b) == 0);
    assert(strcmp(hash_b, abc_hash) == 0);
    assert(count_directory_entries(objects_dir) == 1);
    assert(stat(abc_blob_path, &st_after) == 0);
    assert(st_initial.st_ino == st_after.st_ino);

    struct pal_file *blob = pal_open(abc_blob_path, "rb");
    assert(blob != NULL);
    char buffer[16];
    assert(pal_read(blob, buffer, sizeof(buffer)) == 3);
    assert(memcmp(buffer, "abc", 3) == 0);
    assert(pal_read(blob, buffer, sizeof(buffer)) == 0);
    assert(pal_close(blob) == 0);

    cleanup_storage();
    printf("[PASS] test_blob_write_duplicate\n");
}

static void test_blob_read(void)
{
    cleanup_storage();

    const char *root = "test_anedota_storage";
    const char *source_a = "test_blob_source_a.txt";
    const char *source_b = "test_blob_source_b.bin";
    const char *empty_source = "test_blob_empty.txt";
    const char *large_source = "test_blob_large.bin";
    const char *abc_hash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const char *missing_hash = "0000000000000000000000000000000000000000000000000000000000000000";

    char *data = (char *)0x1;
    size_t size = 999;

    /* Falha se o armazenamento não foi inicializado ou os argumentos forem inválidos */
    assert(anedota_blob_read(root, abc_hash, &data, &size) == -1);
    assert(data == (char *)0x1 && size == 999);

    assert(anedota_storage_init(root) == 0);
    assert(anedota_blob_read(NULL, abc_hash, &data, &size) == -1);
    assert(anedota_blob_read("", abc_hash, &data, &size) == -1);
    assert(anedota_blob_read(root, NULL, &data, &size) == -1);
    assert(anedota_blob_read(root, "", &data, &size) == -1);
    assert(anedota_blob_read(root, "curto", &data, &size) == -1);
    assert(anedota_blob_read(root, "../ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f2001", &data, &size) == -1);
    assert(anedota_blob_read(root, "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", &data, &size) == -1);
    assert(anedota_blob_read(root, abc_hash, NULL, &size) == -1);
    assert(anedota_blob_read(root, abc_hash, &data, NULL) == -1);
    assert(anedota_blob_read(root, missing_hash, &data, &size) == -1);
    assert(data == (char *)0x1 && size == 999);

    /* Hash válido retorna o conteúdo textual original do Blob */
    char hash[ANEDOTA_SHA256_HEX_SIZE];
    write_test_file(source_a, "abc", 3);
    assert(anedota_blob_write_file(root, source_a, hash) == 0);
    assert(anedota_blob_read(root, hash, &data, &size) == 0);
    assert(data != NULL);
    assert(size == 3);
    assert(memcmp(data, "abc", 3) == 0);
    assert(data[3] == '\0');
    free(data);

    /* Hash válido retorna conteúdo binário com bytes nulos */
    const unsigned char binary[] = { 0, 1, 255, 'a', 'b', 'c', 0 };
    write_test_file(source_b, binary, sizeof(binary));
    assert(anedota_blob_write_file(root, source_b, hash) == 0);
    assert(anedota_blob_read("test_anedota_storage/", hash, &data, &size) == 0);
    assert(data != NULL);
    assert(size == sizeof(binary));
    assert(memcmp(data, binary, sizeof(binary)) == 0);
    free(data);

    /* Hash válido retorna conteúdo de Blob vazio */
    write_test_file(empty_source, "", 0);
    assert(anedota_blob_write_file(root, empty_source, hash) == 0);
    assert(anedota_blob_read(root, hash, &data, &size) == 0);
    assert(data != NULL);
    assert(size == 0);
    assert(data[0] == '\0');
    free(data);

    /* Hash válido retorna conteúdo de Blob com múltiplos blocos (> 4096 bytes) */
    unsigned char large_bytes[4097];
    for (size_t i = 0; i < sizeof(large_bytes); ++i)
        large_bytes[i] = (unsigned char)i;
    write_test_file(large_source, large_bytes, sizeof(large_bytes));
    assert(anedota_blob_write_file(root, large_source, hash) == 0);
    assert(anedota_blob_read(root, hash, &data, &size) == 0);
    assert(data != NULL);
    assert(size == sizeof(large_bytes));
    assert(memcmp(data, large_bytes, sizeof(large_bytes)) == 0);
    free(data);

    cleanup_storage();
    printf("[PASS] test_blob_read\n");
}

int main(void)
{
    printf("Executando testes do armazenamento...\n");
    test_storage_init();
    test_blob_write_new();
    test_blob_write_duplicate();
    test_blob_read();
    printf("Testes do armazenamento concluídos com sucesso\n");
    return 0;
}
