#include <anedota/storage.h>
#include <assert.h>
#include <pal/pal.h>
#include <stdio.h>
#include <sys/stat.h>

static int is_directory(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void cleanup_storage(void)
{
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

    assert(anedota_storage_init("test_anedota_storage") == -1);

    cleanup_storage();
    assert(!is_directory("test_anedota_storage"));

    printf("[PASS] test_storage_init\n");
}

int main(void)
{
    printf("Executando testes do armazenamento...\n");
    test_storage_init();
    printf("Testes do armazenamento concluídos com sucesso\n");
    return 0;
}
