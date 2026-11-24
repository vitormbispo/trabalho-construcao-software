#include <anedota/commands.h>
#include <anedota/log.h>
#include <stdio.h>
#include <stdlib.h>

int anedota_command_log(int argc, char **argv)
{
    (void)argv;
    if (argc != 0)
    {
        fprintf(stderr, "Uso: anedota log\n");
        return 1;
    }

    anedota_log_t *log = NULL;
    if (anedota_log_read(".anedota", &log) != 0)
    {
        fprintf(stderr, "Erro: não foi possível consultar o histórico do armazenamento ANEDOTA.\n");
        return 1;
    }

    if (log->count == 0)
    {
        puts("Nenhum commit encontrado no histórico.");
        free(log);
        return 0;
    }

    for (size_t i = 0; i < log->count; ++i)
    {
        const anedota_log_entry_t *entry = &log->entries[i];
        if (i > 0) putchar('\n');
        printf("commit %s\n", entry->hash);
        printf("Autor: %s\n", entry->author);
        printf("Data: %s\n", entry->timestamp);
        printf("Mensagem: %s\n", entry->message);
    }

    free(log);
    return 0;
}
