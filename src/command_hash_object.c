#include <anedota/commands.h>
#include <anedota/storage.h>
#include <stdio.h>

int anedota_command_hash_object(int argc, char **argv)
{
    if (argc != 1)
    {
        fprintf(stderr, "Uso: anedota hash-object FILE\n");
        return 1;
    }

    if (argv[0][0] == '\0')
    {
        fprintf(stderr, "Erro: o caminho não pode ser vazio.\n");
        return 1;
    }

    char hash[ANEDOTA_SHA256_HEX_SIZE];
    if (anedota_blob_write_file(".anedota", argv[0], hash) != 0)
    {
        fprintf(stderr, "Erro: não foi possível gravar o objeto para '%s'.\n", argv[0]);
        return 1;
    }

    puts(hash);
    return 0;
}
