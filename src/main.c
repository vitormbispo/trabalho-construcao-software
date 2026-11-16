#include <anedota/commands.h>
#include <stdio.h>
#include <string.h>

#define ANEDOTA_CLI "anedota"
#define ANEDOTA_VERSION "0.1.0"
#define ANEDOTA_LICENSE "BSD 3-Clause"
#define AUTHORS "Vitor Bispo, Bruno Yozo,\n" \
    "Julio Cesar e Gustavo Amaro"

#define HELP_MESSAGE ANEDOTA_CLI " usage:\n" \
    ANEDOTA_CLI " [options] | [command] [options]\n" \
    "OPTIONS:\n" \
    "\t--help, -h: Mostra essa mensagem\n" \
    "\t--version, -v: Mostra a versão, licença e autores\n" \
    "COMMANDS:\n" \
    "\tinit [PATH]: Inicializa o repositório em PATH, ou no diretório atual se vazio"

#define VERSION_MESSAGE ANEDOTA_CLI " " ANEDOTA_VERSION " Licença: " \
	ANEDOTA_LICENSE "\n" "Escrito por " AUTHORS

static void help_message(void)
{
    puts(HELP_MESSAGE);
}

static void version_message(void)
{
    puts(VERSION_MESSAGE);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        help_message();
        return 0;
    }

    if (strcmp(argv[1], "init") == 0)
        return anedota_command_init(argc - 2, argv + 2);

    if (argc != 2)
    {
        fprintf(stderr, "Erro: argumentos excedentes. Use anedota --help.\n");
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)
        help_message();
    else if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)
        version_message();
    else
    {
        fprintf(stderr, "Erro: comando desconhecido '%s'. Use anedota --help.\n", argv[1]);
        return 1;
    }
    return 0;
}
