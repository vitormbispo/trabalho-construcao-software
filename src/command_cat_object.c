#include <anedota/commands.h>
#include <anedota/storage.h>
#include <stdio.h>
#include <stdlib.h>

int anedota_command_cat_object(int argc, char **argv)
{
    if (argc != 1)
    {
        fprintf(stderr, "Uso: anedota cat-object HASH\n");
        return 1;
    }

    char *data = NULL;
    size_t size = 0;
    if (anedota_blob_read(".anedota", argv[0], &data, &size) != 0)
    {
        fprintf(stderr, "Erro: não foi possível ler o Blob '%s'.\n", argv[0]);
        return 1;
    }

    const size_t written = fwrite(data, 1, size, stdout);
    free(data);
    if (written != size || ferror(stdout))
    {
        fprintf(stderr, "Erro: não foi possível imprimir o conteúdo do Blob.\n");
        return 1;
    }

    return 0;
}
