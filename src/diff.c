#include <anedota/commit.h>
#include <anedota/diff.h>
#include <anedota/tree.h>
#include <pal/pal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TREE_DEPTH 256

typedef struct
{
    char *path;
    anedota_diff_kind_t kind;
    anedota_tree_entry_type_t entry_type;
    int has_old_hash;
    char old_hash[ANEDOTA_SHA256_HEX_SIZE];
    int has_new_hash;
    char new_hash[ANEDOTA_SHA256_HEX_SIZE];
} collected_diff_t;

typedef struct
{
    collected_diff_t *items;
    size_t count;
    size_t capacity;
} diff_builder_t;

static int is_valid_sha256_hex(const char *hash)
{
    if (!hash || strlen(hash) != ANEDOTA_SHA256_HEX_SIZE - 1) return 0;

    for (size_t i = 0; i < ANEDOTA_SHA256_HEX_SIZE - 1; ++i)
    {
        const char c = hash[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
    }
    return 1;
}

static char *join_path(const char *base_path, const char *relative_path)
{
    if (!base_path || base_path[0] == '\0')
    {
        const size_t len = strlen(relative_path);
        char *copy = malloc(len + 1);
        if (!copy) return NULL;
        memcpy(copy, relative_path, len + 1);
        return copy;
    }

    const size_t base_length = strlen(base_path);
    const size_t relative_length = strlen(relative_path);
    const size_t separator_length = base_path[base_length - 1] == '/' ? 0 : 1;
    if (relative_length > SIZE_MAX - base_length - separator_length - 1)
    {
        return NULL;
    }

    char *path = malloc(base_length + separator_length + relative_length + 1);
    if (!path) return NULL;

    memcpy(path, base_path, base_length);
    if (separator_length) path[base_length] = '/';
    memcpy(path + base_length + separator_length,
           relative_path, relative_length + 1);
    return path;
}

static int add_size_checked(size_t *total, size_t delta)
{
    if (delta > SIZE_MAX - *total) return -1;
    *total += delta;
    return 0;
}

static void free_diff_builder(diff_builder_t *builder)
{
    if (!builder || !builder->items) return;
    for (size_t i = 0; i < builder->count; ++i)
    {
        free(builder->items[i].path);
    }
    free(builder->items);
    builder->items = NULL;
    builder->count = 0;
    builder->capacity = 0;
}

static int append_diff(diff_builder_t *builder,
                       const char *path,
                       anedota_diff_kind_t kind,
                       anedota_tree_entry_type_t entry_type,
                       const char *old_hash,
                       const char *new_hash)
{
    if (builder->count == builder->capacity)
    {
        const size_t new_capacity = builder->capacity == 0 ? 16 : builder->capacity * 2;
        if (new_capacity <= builder->capacity ||
            new_capacity > SIZE_MAX / sizeof(*builder->items))
        {
            return ANEDOTA_DIFF_ERR_IO;
        }
        collected_diff_t *grown = realloc(builder->items,
                                          new_capacity * sizeof(*builder->items));
        if (!grown) return ANEDOTA_DIFF_ERR_IO;
        builder->items = grown;
        builder->capacity = new_capacity;
    }

    const size_t path_len = strlen(path);
    char *path_copy = malloc(path_len + 1);
    if (!path_copy) return ANEDOTA_DIFF_ERR_IO;
    memcpy(path_copy, path, path_len + 1);

    collected_diff_t *slot = &builder->items[builder->count];
    slot->path = path_copy;
    slot->kind = kind;
    slot->entry_type = entry_type;
    slot->has_old_hash = old_hash != NULL;
    slot->has_new_hash = new_hash != NULL;
    if (old_hash) memcpy(slot->old_hash, old_hash, ANEDOTA_SHA256_HEX_SIZE);
    if (new_hash) memcpy(slot->new_hash, new_hash, ANEDOTA_SHA256_HEX_SIZE);
    ++builder->count;
    return ANEDOTA_DIFF_OK;
}

static int resolve_version_to_tree(const char *root_path,
                                   const char *objects_path,
                                   const char *version_hash,
                                   char tree_hash_out[ANEDOTA_SHA256_HEX_SIZE])
{
    if (!is_valid_sha256_hex(version_hash))
    {
        return ANEDOTA_DIFF_ERR_INVALID_VERSION;
    }

    char *object_path = join_path(objects_path, version_hash);
    if (!object_path) return ANEDOTA_DIFF_ERR_IO;

    const int exists = pal_file_exists(object_path);
    free(object_path);
    if (exists < 0) return ANEDOTA_DIFF_ERR_IO;
    if (exists == 0) return ANEDOTA_DIFF_ERR_VERSION_NOT_FOUND;

    anedota_commit_t *commit = NULL;
    if (anedota_commit_read(root_path, version_hash, &commit) == 0)
    {
        anedota_tree_t *tree = NULL;
        const int tree_ok = anedota_tree_read(root_path, commit->tree_hash, &tree);
        if (tree_ok == 0)
        {
            memcpy(tree_hash_out, commit->tree_hash, ANEDOTA_SHA256_HEX_SIZE);
        }
        free(tree);
        free(commit);
        return tree_ok == 0 ? ANEDOTA_DIFF_OK : ANEDOTA_DIFF_ERR_INVALID_VERSION;
    }

    anedota_tree_t *tree = NULL;
    if (anedota_tree_read(root_path, version_hash, &tree) == 0)
    {
        free(tree);
        memcpy(tree_hash_out, version_hash, ANEDOTA_SHA256_HEX_SIZE);
        return ANEDOTA_DIFF_OK;
    }

    return ANEDOTA_DIFF_ERR_INVALID_VERSION;
}

static int collect_subtree_entries(const char *root_path,
                                   const char *tree_hash,
                                   const char *prefix,
                                   anedota_diff_kind_t kind,
                                   unsigned int depth,
                                   diff_builder_t *builder)
{
    if (depth >= MAX_TREE_DEPTH) return ANEDOTA_DIFF_ERR_INVALID_VERSION;

    anedota_tree_t *tree = NULL;
    if (anedota_tree_read(root_path, tree_hash, &tree) != 0)
    {
        return ANEDOTA_DIFF_ERR_INVALID_VERSION;
    }

    int status = ANEDOTA_DIFF_OK;
    if (tree->count == 0 && prefix && prefix[0] != '\0')
    {
        const char *old_hash = kind == ANEDOTA_DIFF_REMOVED ? tree_hash : NULL;
        const char *new_hash = kind == ANEDOTA_DIFF_ADDED ? tree_hash : NULL;
        status = append_diff(builder, prefix, kind, ANEDOTA_TREE_ENTRY_TREE,
                             old_hash, new_hash);
    }

    for (size_t i = 0; status == ANEDOTA_DIFF_OK && i < tree->count; ++i)
    {
        const anedota_tree_entry_t *entry = &tree->entries[i];
        char *child_path = join_path(prefix, entry->name);
        if (!child_path)
        {
            status = ANEDOTA_DIFF_ERR_IO;
            break;
        }

        if (entry->type == ANEDOTA_TREE_ENTRY_BLOB)
        {
            const char *old_hash = kind == ANEDOTA_DIFF_REMOVED ? entry->hash : NULL;
            const char *new_hash = kind == ANEDOTA_DIFF_ADDED ? entry->hash : NULL;
            status = append_diff(builder, child_path, kind,
                                 ANEDOTA_TREE_ENTRY_BLOB, old_hash, new_hash);
        }
        else
        {
            status = collect_subtree_entries(root_path, entry->hash, child_path,
                                             kind, depth + 1, builder);
        }
        free(child_path);
    }

    free(tree);
    return status;
}

static int emit_single_entry(const char *root_path,
                             const anedota_tree_entry_t *entry,
                             const char *prefix,
                             anedota_diff_kind_t kind,
                             unsigned int depth,
                             diff_builder_t *builder)
{
    char *child_path = join_path(prefix, entry->name);
    if (!child_path) return ANEDOTA_DIFF_ERR_IO;

    int status;
    if (entry->type == ANEDOTA_TREE_ENTRY_BLOB)
    {
        const char *old_hash = kind == ANEDOTA_DIFF_REMOVED ? entry->hash : NULL;
        const char *new_hash = kind == ANEDOTA_DIFF_ADDED ? entry->hash : NULL;
        status = append_diff(builder, child_path, kind,
                             ANEDOTA_TREE_ENTRY_BLOB, old_hash, new_hash);
    }
    else
    {
        status = collect_subtree_entries(root_path, entry->hash, child_path,
                                         kind, depth + 1, builder);
    }

    free(child_path);
    return status;
}

static int diff_trees_recursive(const char *root_path,
                                const char *from_tree_hash,
                                const char *to_tree_hash,
                                const char *prefix,
                                unsigned int depth,
                                diff_builder_t *builder)
{
    if (strcmp(from_tree_hash, to_tree_hash) == 0) return ANEDOTA_DIFF_OK;
    if (depth >= MAX_TREE_DEPTH) return ANEDOTA_DIFF_ERR_INVALID_VERSION;

    anedota_tree_t *from_tree = NULL;
    anedota_tree_t *to_tree = NULL;
    if (anedota_tree_read(root_path, from_tree_hash, &from_tree) != 0)
    {
        return ANEDOTA_DIFF_ERR_INVALID_VERSION;
    }
    if (anedota_tree_read(root_path, to_tree_hash, &to_tree) != 0)
    {
        free(from_tree);
        return ANEDOTA_DIFF_ERR_INVALID_VERSION;
    }

    size_t i = 0;
    size_t j = 0;
    int status = ANEDOTA_DIFF_OK;

    while (status == ANEDOTA_DIFF_OK &&
           (i < from_tree->count || j < to_tree->count))
    {
        if (i < from_tree->count && j < to_tree->count)
        {
            const anedota_tree_entry_t *left = &from_tree->entries[i];
            const anedota_tree_entry_t *right = &to_tree->entries[j];
            const int cmp = strcmp(left->name, right->name);

            if (cmp < 0)
            {
                status = emit_single_entry(root_path, left, prefix,
                                           ANEDOTA_DIFF_REMOVED, depth, builder);
                ++i;
            }
            else if (cmp > 0)
            {
                status = emit_single_entry(root_path, right, prefix,
                                           ANEDOTA_DIFF_ADDED, depth, builder);
                ++j;
            }
            else
            {
                if (left->type == right->type)
                {
                    if (strcmp(left->hash, right->hash) != 0)
                    {
                        char *child_path = join_path(prefix, left->name);
                        if (!child_path)
                        {
                            status = ANEDOTA_DIFF_ERR_IO;
                        }
                        else if (left->type == ANEDOTA_TREE_ENTRY_BLOB)
                        {
                            status = append_diff(builder, child_path,
                                                 ANEDOTA_DIFF_MODIFIED,
                                                 ANEDOTA_TREE_ENTRY_BLOB,
                                                 left->hash, right->hash);
                        }
                        else
                        {
                            status = diff_trees_recursive(root_path,
                                                          left->hash,
                                                          right->hash,
                                                          child_path,
                                                          depth + 1,
                                                          builder);
                        }
                        free(child_path);
                    }
                }
                else
                {
                    status = emit_single_entry(root_path, left, prefix,
                                               ANEDOTA_DIFF_REMOVED, depth, builder);
                    if (status == ANEDOTA_DIFF_OK)
                    {
                        status = emit_single_entry(root_path, right, prefix,
                                                   ANEDOTA_DIFF_ADDED, depth, builder);
                    }
                }
                ++i;
                ++j;
            }
        }
        else if (i < from_tree->count)
        {
            status = emit_single_entry(root_path, &from_tree->entries[i], prefix,
                                       ANEDOTA_DIFF_REMOVED, depth, builder);
            ++i;
        }
        else
        {
            status = emit_single_entry(root_path, &to_tree->entries[j], prefix,
                                       ANEDOTA_DIFF_ADDED, depth, builder);
            ++j;
        }
    }

    free(from_tree);
    free(to_tree);
    return status;
}

static char *copy_string_into(char **dest, const char *src)
{
    const size_t len = strlen(src);
    char *start = *dest;
    memcpy(start, src, len + 1);
    *dest += len + 1;
    return start;
}

static int pack_diff(const diff_builder_t *builder, anedota_diff_t **diff_out)
{
    if (builder->count > (SIZE_MAX - sizeof(anedota_diff_t)) /
                             sizeof(anedota_diff_entry_t))
    {
        return ANEDOTA_DIFF_ERR_IO;
    }

    const size_t entries_size = builder->count * sizeof(anedota_diff_entry_t);
    size_t total_size = sizeof(anedota_diff_t) + entries_size;

    for (size_t i = 0; i < builder->count; ++i)
    {
        const collected_diff_t *item = &builder->items[i];
        if (add_size_checked(&total_size, strlen(item->path) + 1) != 0 ||
            (item->has_old_hash &&
             add_size_checked(&total_size, ANEDOTA_SHA256_HEX_SIZE) != 0) ||
            (item->has_new_hash &&
             add_size_checked(&total_size, ANEDOTA_SHA256_HEX_SIZE) != 0))
        {
            return ANEDOTA_DIFF_ERR_IO;
        }
    }

    anedota_diff_t *diff = malloc(total_size);
    if (!diff) return ANEDOTA_DIFF_ERR_IO;

    diff->count = builder->count;
    char *strings = (char *)diff + sizeof(*diff) + entries_size;
    for (size_t i = 0; i < builder->count; ++i)
    {
        const collected_diff_t *item = &builder->items[i];
        diff->entries[i].path = copy_string_into(&strings, item->path);
        diff->entries[i].kind = item->kind;
        diff->entries[i].entry_type = item->entry_type;
        diff->entries[i].old_hash = item->has_old_hash
            ? copy_string_into(&strings, item->old_hash)
            : NULL;
        diff->entries[i].new_hash = item->has_new_hash
            ? copy_string_into(&strings, item->new_hash)
            : NULL;
    }

    *diff_out = diff;
    return ANEDOTA_DIFF_OK;
}

int anedota_diff_versions(const char *root_path,
                          const char *from_version,
                          const char *to_version,
                          anedota_diff_t **diff_out)
{
    if (!root_path || root_path[0] == '\0' ||
        !from_version || !to_version || !diff_out)
    {
        return ANEDOTA_DIFF_ERR_INVALID_ARG;
    }

    char *objects_path = join_path(root_path, "objetos");
    if (!objects_path) return ANEDOTA_DIFF_ERR_IO;

    const int dir_exists = pal_dir_exists(objects_path);
    if (dir_exists != 1)
    {
        free(objects_path);
        return dir_exists < 0 ? ANEDOTA_DIFF_ERR_IO : ANEDOTA_DIFF_ERR_UNINITIALIZED;
    }

    char from_tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    char to_tree_hash[ANEDOTA_SHA256_HEX_SIZE];

    int status = resolve_version_to_tree(root_path, objects_path,
                                         from_version, from_tree_hash);
    if (status == ANEDOTA_DIFF_OK)
    {
        status = resolve_version_to_tree(root_path, objects_path,
                                         to_version, to_tree_hash);
    }
    free(objects_path);
    if (status != ANEDOTA_DIFF_OK) return status;

    diff_builder_t builder = { NULL, 0, 0 };
    status = diff_trees_recursive(root_path, from_tree_hash, to_tree_hash,
                                  "", 0, &builder);
    if (status == ANEDOTA_DIFF_OK)
    {
        status = pack_diff(&builder, diff_out);
    }

    free_diff_builder(&builder);
    return status;
}

const char *anedota_diff_status_message(int status)
{
    switch (status)
    {
    case ANEDOTA_DIFF_OK:
        return "Operação concluída com sucesso.";
    case ANEDOTA_DIFF_ERR_INVALID_ARG:
        return "Argumentos inválidos para comparação entre versões.";
    case ANEDOTA_DIFF_ERR_UNINITIALIZED:
        return "Armazenamento ANEDOTA não inicializado.";
    case ANEDOTA_DIFF_ERR_VERSION_NOT_FOUND:
        return "Uma ou ambas as versões informadas não existem no armazenamento.";
    case ANEDOTA_DIFF_ERR_INVALID_VERSION:
        return "Uma ou ambas as versões informadas são inválidas ou não correspondem a Commit/Tree.";
    case ANEDOTA_DIFF_ERR_IO:
        return "Erro de leitura ou memória ao comparar versões.";
    default:
        return "Erro desconhecido ao comparar versões.";
    }
}
