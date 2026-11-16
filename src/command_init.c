#include <anedota/commands.h>
#include <anedota/storage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int anedota_command_init(int argc, char **argv)
{
    if (argc > 1)
    {
        fprintf(stderr, "Uso: anedota init [PATH]\n");
        return 1;
    }

    const char *base = argc == 1 ? argv[0] : ".";
    const size_t length = strlen(base);
    if (length == 0)
    {
        fprintf(stderr, "Erro: o caminho não pode ser vazio.\n");
        return 1;
    }

    const char *suffix = base[length - 1] == '/' ? ".anedota" : "/.anedota";
    char *root = malloc(length + strlen(suffix) + 1);
    if (!root)
    {
        fprintf(stderr, "Erro: memória insuficiente para inicializar o armazenamento.\n");
        return 1;
    }
    strcpy(root, base);
    strcat(root, suffix);

    const int result = anedota_storage_init(root);
    if (result == 0)
        printf("Armazenamento ANEDOTA inicializado em %s\n", root);
    else
        fprintf(stderr, "Erro: não foi possível inicializar o armazenamento em %s.\n", root);

    free(root);
    return result == 0 ? 0 : 1;
}
