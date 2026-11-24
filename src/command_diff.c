#include <anedota/commands.h>
#include <anedota/diff.h>
#include <stdio.h>
#include <stdlib.h>

int anedota_command_diff(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Uso: anedota diff VERSION_A VERSION_B\n");
        return 1;
    }

    anedota_diff_t *diff = NULL;
    const int status = anedota_diff_versions(".anedota", argv[0], argv[1], &diff);
    if (status != ANEDOTA_DIFF_OK)
    {
        fprintf(stderr, "Erro: %s\n", anedota_diff_status_message(status));
        return 1;
    }

    if (diff->count == 0)
    {
        puts("Sem diferenças entre as versões.");
        free(diff);
        return 0;
    }

    for (size_t i = 0; i < diff->count; ++i)
    {
        const anedota_diff_entry_t *entry = &diff->entries[i];
        switch (entry->kind)
        {
        case ANEDOTA_DIFF_ADDED:
            printf("+ [adicionado] %s\n", entry->path);
            break;
        case ANEDOTA_DIFF_REMOVED:
            printf("- [removido] %s\n", entry->path);
            break;
        case ANEDOTA_DIFF_MODIFIED:
            printf("~ [modificado] %s\n", entry->path);
            break;
        }
    }

    free(diff);
    return 0;
}
