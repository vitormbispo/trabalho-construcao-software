#include <anedota/commands.h>
#include <anedota/tree.h>
#include <stdio.h>

int anedota_command_write_tree(int argc, char **argv)
{
    if (argc > 1)
    {
        fprintf(stderr, "Uso: anedota write-tree [PATH]\n");
        return 1;
    }

    const char *dir_path = argc == 1 ? argv[0] : ".";
    if (dir_path[0] == '\0')
    {
        fprintf(stderr, "Erro: o caminho não pode ser vazio.\n");
        return 1;
    }

    char hash[ANEDOTA_SHA256_HEX_SIZE];
    if (anedota_tree_write_directory(".anedota", dir_path, hash) != 0)
    {
        fprintf(stderr, "Erro: não foi possível gravar a Tree para '%s'.\n", dir_path);
        return 1;
    }

    puts(hash);
    return 0;
}
