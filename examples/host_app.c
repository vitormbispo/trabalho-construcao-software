#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#endif

#include <anedota/anedota.h>
#include <pal/pal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void remove_dir_recursive(const char *path)
{
    pal_directory_t *dir = pal_dir_open(path);
    if (!dir)
    {
        unlink(path);
        return;
    }

    const char *name = NULL;
    pal_dir_entry_type_t type;
    while (pal_dir_next(dir, &name, &type) == 1)
    {
        char child[1024];
        if (snprintf(child, sizeof(child), "%s/%s", path, name) >= (int)sizeof(child))
        {
            continue;
        }
        if (type == PAL_DIR_ENTRY_DIRECTORY)
            remove_dir_recursive(child);
        else
            unlink(child);
    }
    pal_dir_close(dir);
    rmdir(path);
}

static const char *diff_kind_label(anedota_diff_kind_t kind)
{
    switch (kind)
    {
    case ANEDOTA_DIFF_ADDED:
        return "ADICIONADO";
    case ANEDOTA_DIFF_REMOVED:
        return "REMOVIDO";
    case ANEDOTA_DIFF_MODIFIED:
        return "MODIFICADO";
    default:
        return "DESCONHECIDO";
    }
}

/* Programa exemplo hospedeiro independente da CLI.
 * Demonstra o uso completo da libanedota sobre dados genéricos em memória:
 * inicializar, armazenar, recuperar, criar commits, consultar histórico e
 * comparar versões. */
int main(int argc, char **argv)
{
    char temp_workspace[] = "/tmp/anedota-host-example-XXXXXX";
    char root_path[512];
    int cleanup_temp = 0;

    if (argc > 2)
    {
        fprintf(stderr, "Uso: %s [DIRETORIO_ARMAZENAMENTO]\n", argv[0]);
        return 1;
    }

    if (argc == 2)
    {
        if (snprintf(root_path, sizeof(root_path), "%s", argv[1]) >= (int)sizeof(root_path))
        {
            fprintf(stderr, "Erro: caminho muito longo.\n");
            return 1;
        }
    }
    else
    {
        if (!mkdtemp(temp_workspace))
        {
            fprintf(stderr, "Erro: não foi possível criar diretório temporário.\n");
            return 1;
        }
        cleanup_temp = 1;
        if (snprintf(root_path, sizeof(root_path), "%s/.anedota", temp_workspace) >= (int)sizeof(root_path))
        {
            remove_dir_recursive(temp_workspace);
            return 1;
        }
    }

    int exit_code = 1;

    /* 1. Inicializar armazenamento */
    if (anedota_storage_init(root_path) != 0)
    {
        fprintf(stderr, "Erro ao inicializar armazenamento em %s\n", root_path);
        goto done;
    }

    /* 2. Armazenar dados genéricos em memória (texto e binário) */
    const char nota_v1[] = "# Notas de Construção de Software\nVersão inicial.";
    const unsigned char registro_binario[] = { 0x00, 0x41, 0x4E, 0x00, 0xFF, 0x10 };
    const char config_v1[] = "{\"tema\":\"claro\",\"versao\":1}";

    char hash_nota_v1[ANEDOTA_SHA256_HEX_SIZE];
    char hash_binario[ANEDOTA_SHA256_HEX_SIZE];
    char hash_config_v1[ANEDOTA_SHA256_HEX_SIZE];

    if (anedota_blob_write(root_path, nota_v1, sizeof(nota_v1) - 1, hash_nota_v1) != 0 ||
        anedota_blob_write(root_path, registro_binario, sizeof(registro_binario), hash_binario) != 0 ||
        anedota_blob_write(root_path, config_v1, sizeof(config_v1) - 1, hash_config_v1) != 0)
    {
        fprintf(stderr, "Erro ao armazenar blobs em memória.\n");
        goto done;
    }

    /* 3. Recuperar conteúdo armazenado e validar integridade */
    char *recuperado = NULL;
    size_t recuperado_tam = 0;
    if (anedota_blob_read(root_path, hash_binario, &recuperado, &recuperado_tam) != 0 ||
        recuperado_tam != sizeof(registro_binario) ||
        memcmp(recuperado, registro_binario, sizeof(registro_binario)) != 0)
    {
        free(recuperado);
        fprintf(stderr, "Erro ao recuperar blob binário.\n");
        goto done;
    }
    free(recuperado);

    /* 4. Montar Tree e criar primeiro Commit */
    const anedota_tree_entry_t sub_v1_entries[] = {
        { "resumo.md", ANEDOTA_TREE_ENTRY_BLOB, hash_nota_v1 }
    };
    char sub_v1_tree[ANEDOTA_SHA256_HEX_SIZE];
    if (anedota_tree_write(root_path, sub_v1_entries, 1, sub_v1_tree) != 0)
    {
        fprintf(stderr, "Erro ao gravar subtree v1.\n");
        goto done;
    }

    const anedota_tree_entry_t root_v1_entries[] = {
        { "config.json", ANEDOTA_TREE_ENTRY_BLOB, hash_config_v1 },
        { "estado.bin", ANEDOTA_TREE_ENTRY_BLOB, hash_binario },
        { "notas", ANEDOTA_TREE_ENTRY_TREE, sub_v1_tree }
    };
    char root_v1_tree[ANEDOTA_SHA256_HEX_SIZE];
    if (anedota_tree_write(root_path, root_v1_entries, 3, root_v1_tree) != 0)
    {
        fprintf(stderr, "Erro ao gravar tree raiz v1.\n");
        goto done;
    }

    const anedota_commit_t c1_in = {
        root_v1_tree, NULL, "AplicacaoHospedeira", "2026-11-24T18:00:00Z",
        "Versão inicial dos dados em memória"
    };
    char c1_hash[ANEDOTA_SHA256_HEX_SIZE];
    if (anedota_commit_write(root_path, &c1_in, c1_hash) != 0)
    {
        fprintf(stderr, "Erro ao criar commit 1.\n");
        goto done;
    }

    /* 5. Evoluir dados (modificar nota, remover estado.bin, adicionar extra.md) */
    const char nota_v2[] = "# Notas de Construção de Software\nVersão revisada com API pública.";
    const char extra_v2[] = "Nova anotação adicionada na versão 2.";
    char hash_nota_v2[ANEDOTA_SHA256_HEX_SIZE];
    char hash_extra_v2[ANEDOTA_SHA256_HEX_SIZE];

    if (anedota_blob_write(root_path, nota_v2, sizeof(nota_v2) - 1, hash_nota_v2) != 0 ||
        anedota_blob_write(root_path, extra_v2, sizeof(extra_v2) - 1, hash_extra_v2) != 0)
    {
        fprintf(stderr, "Erro ao armazenar blobs da versão 2.\n");
        goto done;
    }

    const anedota_tree_entry_t sub_v2_entries[] = {
        { "extra.md", ANEDOTA_TREE_ENTRY_BLOB, hash_extra_v2 },
        { "resumo.md", ANEDOTA_TREE_ENTRY_BLOB, hash_nota_v2 }
    };
    char sub_v2_tree[ANEDOTA_SHA256_HEX_SIZE];
    if (anedota_tree_write(root_path, sub_v2_entries, 2, sub_v2_tree) != 0)
    {
        fprintf(stderr, "Erro ao gravar subtree v2.\n");
        goto done;
    }

    const anedota_tree_entry_t root_v2_entries[] = {
        { "config.json", ANEDOTA_TREE_ENTRY_BLOB, hash_config_v1 },
        { "notas", ANEDOTA_TREE_ENTRY_TREE, sub_v2_tree }
    };
    char root_v2_tree[ANEDOTA_SHA256_HEX_SIZE];
    if (anedota_tree_write(root_path, root_v2_entries, 2, root_v2_tree) != 0)
    {
        fprintf(stderr, "Erro ao gravar tree raiz v2.\n");
        goto done;
    }

    const anedota_commit_t c2_in = {
        root_v2_tree, c1_hash, "AplicacaoHospedeira", "2026-11-24T18:15:00Z",
        "Atualiza resumo, adiciona extra e remove estado.bin"
    };
    char c2_hash[ANEDOTA_SHA256_HEX_SIZE];
    if (anedota_commit_write(root_path, &c2_in, c2_hash) != 0)
    {
        fprintf(stderr, "Erro ao criar commit 2.\n");
        goto done;
    }

    /* 6. Consultar histórico a partir de HEAD */
    anedota_log_t *log = NULL;
    if (anedota_log_read(root_path, &log) != 0)
    {
        fprintf(stderr, "Erro ao consultar histórico.\n");
        goto done;
    }

    printf("=== Exemplo Hospedeiro libanedota %s ===\n", ANEDOTA_VERSION_STRING);
    printf("Histórico (%zu commits):\n", log->count);
    for (size_t i = 0; i < log->count; ++i)
    {
        printf("  [%zu] %.12s | %s | %s\n",
               i + 1, log->entries[i].hash,
               log->entries[i].author, log->entries[i].message);
    }
    free(log);

    /* 7. Comparar versões c1 -> c2 */
    anedota_diff_t *diff = NULL;
    const int diff_status = anedota_diff_versions(root_path, c1_hash, c2_hash, &diff);
    if (diff_status != ANEDOTA_DIFF_OK)
    {
        fprintf(stderr, "Erro ao comparar versões: %s\n",
                anedota_diff_status_message(diff_status));
        goto done;
    }

    printf("Diferenças entre %.12s e %.12s (%zu itens):\n",
           c1_hash, c2_hash, diff->count);
    for (size_t i = 0; i < diff->count; ++i)
    {
        printf("  - [%s] %s\n",
               diff_kind_label(diff->entries[i].kind),
               diff->entries[i].path);
    }
    free(diff);

    exit_code = 0;

done:
    if (cleanup_temp) remove_dir_recursive(temp_workspace);
    return exit_code;
}
