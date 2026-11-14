#include <assert.h>
#include "../include/pal/pal.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void cleanup_test_artifacts(void)
{
    unlink("test_pal_file.txt");
    rmdir("test_pal_dir");
}

static void test_dir_create_and_delete(void)
{
    cleanup_test_artifacts();

    /* Criar diretório */
    assert(pal_dir_create("test_pal_dir") == 0);

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

    /* Abrir para escrita */
    struct pal_file *wf = pal_open(test_path, "wb");
    assert(wf != NULL);

    /* Escrita inválida */
    assert(pal_write(NULL, msg, msg_len) == -1);
    assert(pal_write(wf, NULL, msg_len) == -1);

    /* Escrever conteúdo */
    int written = pal_write(wf, msg, msg_len);
    assert(written == (int)msg_len);

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
    test_file_open_write_read_close();
    printf("Testes da PAL concluídos com sucesso\n");
    return 0;
}
