#include <anedota/commands.h>
#include <anedota/commit.h>
#include <anedota/ref.h>
#include <anedota/tree.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int parse_commit_args(int argc, char **argv,
                             const char **message_out,
                             const char **author_out)
{
    const char *message = NULL;
    const char *author = NULL;

    for (int i = 0; i < argc; ++i)
    {
        if (strcmp(argv[i], "-m") == 0)
        {
            if (message || i + 1 >= argc) return -1;
            message = argv[++i];
        }
        else if (strcmp(argv[i], "-a") == 0)
        {
            if (author || i + 1 >= argc) return -1;
            author = argv[++i];
        }
        else
        {
            return -1;
        }
    }

    if (!message || message[0] == '\0') return -1;
    if (author && author[0] == '\0') return -1;

    *message_out = message;
    *author_out = author;
    return 0;
}

static const char *resolve_author(const char *explicit_author)
{
    if (explicit_author) return explicit_author;

    const char *env_author = getenv("ANEDOTA_AUTHOR");
    if (env_author && env_author[0] != '\0') return env_author;

    const char *env_user = getenv("USER");
    if (env_user && env_user[0] != '\0') return env_user;

    return "desconhecido";
}

static int format_utc_timestamp(char *buffer, size_t size)
{
    const time_t now = time(NULL);
    if (now == (time_t)-1) return -1;

    const struct tm *utc = gmtime(&now);
    if (!utc) return -1;

    const size_t written = strftime(buffer, size, "%Y-%m-%dT%H:%M:%SZ", utc);
    return written > 0 ? 0 : -1;
}

int anedota_command_commit(int argc, char **argv)
{
    const char *message = NULL;
    const char *explicit_author = NULL;
    if (parse_commit_args(argc, argv, &message, &explicit_author) != 0)
    {
        fprintf(stderr, "Uso: anedota commit -m MESSAGE [-a AUTHOR]\n");
        return 1;
    }

    char parent_hash[ANEDOTA_SHA256_HEX_SIZE];
    const int head_status = anedota_ref_read_head(".anedota", parent_hash);
    if (head_status < 0)
    {
        fprintf(stderr, "Erro: armazenamento ANEDOTA não inicializado ou referência inválida.\n");
        return 1;
    }

    char tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    if (anedota_tree_write_directory(".anedota", ".", tree_hash) != 0)
    {
        fprintf(stderr, "Erro: não foi possível gerar o snapshot do diretório atual.\n");
        return 1;
    }

    char timestamp[32];
    if (format_utc_timestamp(timestamp, sizeof(timestamp)) != 0)
    {
        fprintf(stderr, "Erro: não foi possível obter a data atual.\n");
        return 1;
    }

    anedota_commit_t commit = {
        tree_hash,
        head_status == 1 ? parent_hash : NULL,
        resolve_author(explicit_author),
        timestamp,
        message
    };

    char commit_hash[ANEDOTA_SHA256_HEX_SIZE];
    if (anedota_commit_write(".anedota", &commit, commit_hash) != 0)
    {
        fprintf(stderr, "Erro: não foi possível criar o Commit.\n");
        return 1;
    }

    puts(commit_hash);
    return 0;
}
