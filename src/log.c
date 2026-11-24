#include <anedota/commit.h>
#include <anedota/log.h>
#include <anedota/ref.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char hash[ANEDOTA_SHA256_HEX_SIZE];
    anedota_commit_t *commit;
} collected_commit_t;

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

static int add_size_checked(size_t *total, size_t delta)
{
    if (delta > SIZE_MAX - *total) return -1;
    *total += delta;
    return 0;
}

static int contains_hash(const collected_commit_t *items, size_t count,
                         const char *hash)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (strcmp(items[i].hash, hash) == 0) return 1;
    }
    return 0;
}

static void free_collected_commits(collected_commit_t *items, size_t count)
{
    if (!items) return;
    for (size_t i = 0; i < count; ++i)
    {
        free(items[i].commit);
    }
    free(items);
}

static char *copy_string_into(char **dest, const char *src)
{
    const size_t len = strlen(src);
    char *start = *dest;
    memcpy(start, src, len + 1);
    *dest += len + 1;
    return start;
}

static int pack_log(const collected_commit_t *items, size_t count,
                    anedota_log_t **log_out)
{
    if (count > (SIZE_MAX - sizeof(anedota_log_t)) / sizeof(anedota_log_entry_t))
    {
        return -1;
    }

    size_t entries_size = count * sizeof(anedota_log_entry_t);
    size_t total_size = sizeof(anedota_log_t) + entries_size;

    for (size_t i = 0; i < count; ++i)
    {
        const anedota_commit_t *c = items[i].commit;
        if (add_size_checked(&total_size, strlen(c->tree_hash) + 1) != 0 ||
            (c->parent_hash &&
             add_size_checked(&total_size, strlen(c->parent_hash) + 1) != 0) ||
            add_size_checked(&total_size, strlen(c->author) + 1) != 0 ||
            add_size_checked(&total_size, strlen(c->timestamp) + 1) != 0 ||
            add_size_checked(&total_size, strlen(c->message) + 1) != 0)
        {
            return -1;
        }
    }

    anedota_log_t *log = malloc(total_size);
    if (!log) return -1;

    log->count = count;
    char *strings = (char *)log + sizeof(*log) + entries_size;
    for (size_t i = 0; i < count; ++i)
    {
        const anedota_commit_t *c = items[i].commit;
        memcpy(log->entries[i].hash, items[i].hash, ANEDOTA_SHA256_HEX_SIZE);
        log->entries[i].tree_hash = copy_string_into(&strings, c->tree_hash);
        log->entries[i].parent_hash = c->parent_hash
            ? copy_string_into(&strings, c->parent_hash)
            : NULL;
        log->entries[i].author = copy_string_into(&strings, c->author);
        log->entries[i].timestamp = copy_string_into(&strings, c->timestamp);
        log->entries[i].message = copy_string_into(&strings, c->message);
    }

    *log_out = log;
    return 0;
}

int anedota_log_read_from(const char *root_path,
                          const char *start_commit_hash,
                          anedota_log_t **log_out)
{
    if (!root_path || root_path[0] == '\0' ||
        !is_valid_sha256_hex(start_commit_hash) || !log_out)
    {
        return -1;
    }

    collected_commit_t *items = NULL;
    size_t count = 0;
    size_t capacity = 0;
    char current_hash[ANEDOTA_SHA256_HEX_SIZE];
    memcpy(current_hash, start_commit_hash, ANEDOTA_SHA256_HEX_SIZE);

    while (1)
    {
        if (contains_hash(items, count, current_hash))
        {
            free_collected_commits(items, count);
            return -1;
        }

        anedota_commit_t *commit = NULL;
        if (anedota_commit_read(root_path, current_hash, &commit) != 0)
        {
            free_collected_commits(items, count);
            return -1;
        }

        if (count == capacity)
        {
            const size_t new_capacity = capacity == 0 ? 16 : capacity * 2;
            if (new_capacity <= capacity ||
                new_capacity > SIZE_MAX / sizeof(*items))
            {
                free(commit);
                free_collected_commits(items, count);
                return -1;
            }
            collected_commit_t *grown = realloc(items, new_capacity * sizeof(*items));
            if (!grown)
            {
                free(commit);
                free_collected_commits(items, count);
                return -1;
            }
            items = grown;
            capacity = new_capacity;
        }

        memcpy(items[count].hash, current_hash, ANEDOTA_SHA256_HEX_SIZE);
        items[count].commit = commit;
        ++count;

        if (!commit->parent_hash) break;
        memcpy(current_hash, commit->parent_hash, ANEDOTA_SHA256_HEX_SIZE);
    }

    const int status = pack_log(items, count, log_out);
    free_collected_commits(items, count);
    return status;
}

int anedota_log_read(const char *root_path, anedota_log_t **log_out)
{
    if (!root_path || root_path[0] == '\0' || !log_out) return -1;

    char head_hash[ANEDOTA_SHA256_HEX_SIZE];
    const int head_status = anedota_ref_read_head(root_path, head_hash);
    if (head_status < 0) return -1;

    if (head_status == 0)
    {
        anedota_log_t *empty = malloc(sizeof(*empty));
        if (!empty) return -1;
        empty->count = 0;
        *log_out = empty;
        return 0;
    }

    return anedota_log_read_from(root_path, head_hash, log_out);
}
