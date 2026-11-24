#include <assert.h>
#include "../include/pal/pal.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void cleanup_test_artifacts(void)
{
    unlink("test_pal_file.txt");
    unlink("test_pal_dir/file.txt");
    unlink("test_pal_dir/subdir/nested.txt");
    rmdir("test_pal_dir/subdir");
    rmdir("test_pal_dir");
}

static void test_dir_iteration(void)
{
    cleanup_test_artifacts();

    assert(pal_dir_open(NULL) == NULL);
    assert(pal_dir_open("") == NULL);
    assert(pal_dir_open("missing_test_pal_dir") == NULL);

    assert(pal_dir_create("test_pal_dir") == 0);
    assert(pal_dir_create("test_pal_dir/subdir") == 0);
    struct pal_file *file = pal_open("test_pal_dir/file.txt", "wb");
    assert(file != NULL);
    assert(pal_close(file) == 0);

    pal_directory_t *directory = pal_dir_open("test_pal_dir");
    assert(directory != NULL);

    int found_file = 0;
    int found_subdirectory = 0;
    const char *name = NULL;
    pal_dir_entry_type_t type;
    int result;
    while ((result = pal_dir_next(directory, &name, &type)) == 1)
    {
        assert(strcmp(name, ".") != 0);
        assert(strcmp(name, "..") != 0);
        if (strcmp(name, "file.txt") == 0)
        {
            assert(type == PAL_DIR_ENTRY_FILE);
            found_file = 1;
        }
        else if (strcmp(name, "subdir") == 0)
        {
            assert(type == PAL_DIR_ENTRY_DIRECTORY);
            found_subdirectory = 1;
        }
        else
        {
            assert(0 && "unexpected directory entry");
        }
    }
    assert(result == 0);
    assert(found_file);
    assert(found_subdirectory);
    assert(pal_dir_next(NULL, &name, &type) == -1);
    assert(pal_dir_next(directory, NULL, &type) == -1);
    assert(pal_dir_next(directory, &name, NULL) == -1);
    assert(pal_dir_close(directory) == 0);
    assert(pal_dir_close(NULL) == -1);

    cleanup_test_artifacts();
    printf("[PASS] test_dir_iteration\n");
}

static void test_dir_create_and_delete(void)
{
    cleanup_test_artifacts();

    /* Criar diretório */
    assert(pal_dir_create("test_pal_dir") == 0);
    assert(pal_dir_exists("test_pal_dir") == 1);
    assert(pal_dir_exists(NULL) == -1);
    assert(pal_file_exists("test_pal_dir") == 0);
    assert(pal_file_exists(NULL) == -1);

    struct stat st;
    assert(stat("test_pal_dir", &st) == 0);
    assert(S_ISDIR(st.st_mode));

    /* Criar diretório já existente deve falhar */
    assert(pal_dir_create("test_pal_dir") == -1);

    /* Criar diretório com caminho nulo deve falhar */
    assert(pal_dir_create(NULL) == -1);

    /* Remover diretório */
    assert(pal_dir_delete("test_pal_dir") == 0);
    assert(stat("test_pal_dir", &st) == -1);
    assert(pal_dir_exists("test_pal_dir") == 0);

    /* Remover diretório inexistente ou nulo deve falhar */
    assert(pal_dir_delete("test_pal_dir") == -1);
    assert(pal_dir_delete(NULL) == -1);

    printf("[PASS] test_dir_create_and_delete\n");
}

static void test_file_open_write_read_close(void)
{
    cleanup_test_artifacts();

    const char *test_path = "test_pal_file.txt";
    const char *msg = "Hello, ANEDOTA PAL!";
    size_t msg_len = strlen(msg);

    /* Argumentos inválidos no open */
    assert(pal_open(NULL, "w") == NULL);
    assert(pal_open(test_path, NULL) == NULL);
    assert(pal_open("arquivo_que_nao_existe_12345.txt", "r") == NULL);
    assert(pal_file_exists(test_path) == 0);

    /* Abrir para escrita */
    struct pal_file *wf = pal_open(test_path, "wb");
    assert(wf != NULL);

    /* Escrita inválida */
    assert(pal_write(NULL, msg, msg_len) == -1);
    assert(pal_write(wf, NULL, msg_len) == -1);

    /* Escrever conteúdo */
    int written = pal_write(wf, msg, msg_len);
    assert(written == (int)msg_len);
    assert(pal_dir_exists(test_path) == 0);
    assert(pal_file_exists(test_path) == 1);

    /* Fechar arquivo */
    assert(pal_close(wf) == 0);
    assert(pal_close(NULL) == -1);

    /* Abrir para leitura */
    struct pal_file *rf = pal_open(test_path, "rb");
    assert(rf != NULL);

    char buffer[64];
    memset(buffer, 0, sizeof(buffer));

    /* Leitura inválida */
    assert(pal_read(NULL, buffer, sizeof(buffer)) == -1);
    assert(pal_read(rf, NULL, sizeof(buffer)) == -1);

    /* Ler conteúdo */
    int bytes_read = pal_read(rf, buffer, sizeof(buffer) - 1);
    assert(bytes_read == (int)msg_len);
    assert(memcmp(buffer, msg, msg_len) == 0);

    /* Ler no EOF deve retornar 0 */
    assert(pal_read(rf, buffer, sizeof(buffer) - 1) == 0);

    assert(pal_close(rf) == 0);

    cleanup_test_artifacts();
    printf("[PASS] test_file_open_write_read_close\n");
}

int main(void)
{
    printf("Executando testes da PAL...\n");
    test_dir_create_and_delete();
    test_dir_iteration();
    test_file_open_write_read_close();
    printf("Testes da PAL concluídos com sucesso\n");
    return 0;
}
