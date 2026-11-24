#include <anedota/commit.h>
#include <anedota/ref.h>
#include <pal/pal.h>
#include <stdlib.h>
#include <string.h>

static const char default_head_ref[] = "referencias/branches/main";
static const char symbolic_ref_prefix[] = "ref: ";

static char *join_path(const char *base_path, const char *relative_path)
{
    const size_t base_length = strlen(base_path);
    const size_t relative_length = strlen(relative_path);
    const size_t separator_length = base_path[base_length - 1] == '/' ? 0 : 1;
    char *path = malloc(base_length + separator_length + relative_length + 1);
    if (!path) return NULL;

    memcpy(path, base_path, base_length);
    if (separator_length) path[base_length] = '/';
    memcpy(path + base_length + separator_length,
           relative_path, relative_length + 1);
    return path;
}

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

static int is_valid_relative_ref(const char *relative_ref)
{
    if (!relative_ref || relative_ref[0] == '\0' || relative_ref[0] == '/')
    {
        return 0;
    }

    const char *segment = relative_ref;
    while (1)
    {
        const char *slash = strchr(segment, '/');
        const size_t length = slash ? (size_t)(slash - segment) : strlen(segment);
        if (length == 0 ||
            (length == 1 && segment[0] == '.') ||
            (length == 2 && segment[0] == '.' && segment[1] == '.'))
        {
            return 0;
        }
        if (!slash) break;
        segment = slash + 1;
    }

    return 1;
}

static int storage_is_initialized(const char *root_path)
{
    if (!root_path || root_path[0] == '\0') return 0;

    char *objects_path = join_path(root_path, "objetos");
    char *branches_path = join_path(root_path, "referencias/branches");
    if (!objects_path || !branches_path)
    {
        free(objects_path);
        free(branches_path);
        return 0;
    }

    const int valid = pal_dir_exists(objects_path) == 1 &&
                      pal_dir_exists(branches_path) == 1;
    free(objects_path);
    free(branches_path);
    return valid;
}

static int read_small_text_file(const char *path, char *buffer, size_t capacity)
{
    pal_file_t *file = pal_open(path, "rb");
    if (!file) return -1;

    size_t length = 0;
    int status = 0;
    while (1)
    {
        if (length == capacity - 1)
        {
            char extra;
            const int more = pal_read(file, &extra, 1);
            if (more != 0) status = -1;
            break;
        }
        const int count = pal_read(file, buffer + length, capacity - 1 - length);
        if (count < 0)
        {
            status = -1;
            break;
        }
        if (count == 0) break;
        length += (size_t)count;
    }

    if (pal_close(file) != 0) status = -1;
    if (status != 0) return -1;

    for (size_t i = 0; i < length; ++i)
    {
        if (buffer[i] == '\0') return -1;
    }
    while (length > 0 && (buffer[length - 1] == '\n' || buffer[length - 1] == '\r'))
    {
        --length;
    }
    buffer[length] = '\0';
    return 0;
}

static char *parent_directory_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (!slash)
    {
        char *dot = malloc(2);
        if (!dot) return NULL;
        dot[0] = '.';
        dot[1] = '\0';
        return dot;
    }

    const size_t length = slash == path ? 1 : (size_t)(slash - path);
    char *dir = malloc(length + 1);
    if (!dir) return NULL;
    memcpy(dir, path, length);
    dir[length] = '\0';
    return dir;
}

static int write_text_file_atomic(const char *path, const char *content)
{
    char *parent = parent_directory_of(path);
    if (!parent) return -1;

    char *temp_path = join_path(parent, ".ref-XXXXXX");
    free(parent);
    if (!temp_path) return -1;

    pal_file_t *temp = pal_open_temp(temp_path);
    if (!temp)
    {
        free(temp_path);
        return -1;
    }

    const size_t length = strlen(content);
    int result = 0;
    size_t written_total = 0;
    while (written_total < length)
    {
        const size_t chunk = length - written_total > 4096
            ? 4096
            : length - written_total;
        const int written = pal_write(temp, content + written_total, chunk);
        if (written <= 0 || (size_t)written > chunk)
        {
            result = -1;
            break;
        }
        written_total += (size_t)written;
    }

    if (pal_close(temp) != 0) result = -1;
    if (result == 0 && pal_file_rename(temp_path, path) != 0) result = -1;
    if (result != 0) pal_file_delete(temp_path);

    free(temp_path);
    return result;
}

static int resolve_head_target(const char *root_path,
                               char **target_path_out,
                               char detached_hash_out[ANEDOTA_SHA256_HEX_SIZE],
                               int *is_detached_out)
{
    char *head_path = join_path(root_path, "HEAD");
    if (!head_path) return -1;

    const int head_exists = pal_file_exists(head_path);
    if (head_exists < 0)
    {
        free(head_path);
        return -1;
    }

    if (head_exists == 0)
    {
        free(head_path);
        *target_path_out = join_path(root_path, default_head_ref);
        *is_detached_out = 0;
        return *target_path_out ? 0 : -1;
    }

    char head_content[512];
    const int read_status = read_small_text_file(head_path, head_content,
                                                 sizeof(head_content));
    if (read_status != 0)
    {
        free(head_path);
        return -1;
    }

    const size_t prefix_len = strlen(symbolic_ref_prefix);
    if (strncmp(head_content, symbolic_ref_prefix, prefix_len) == 0)
    {
        const char *relative_ref = head_content + prefix_len;
        free(head_path);
        if (!is_valid_relative_ref(relative_ref)) return -1;
        *target_path_out = join_path(root_path, relative_ref);
        *is_detached_out = 0;
        return *target_path_out ? 0 : -1;
    }

    if (is_valid_sha256_hex(head_content))
    {
        memcpy(detached_hash_out, head_content, ANEDOTA_SHA256_HEX_SIZE);
        *target_path_out = head_path;
        *is_detached_out = 1;
        return 0;
    }

    free(head_path);
    return -1;
}

int anedota_ref_read_head(const char *root_path,
                          char hash_out[ANEDOTA_SHA256_HEX_SIZE])
{
    if (!hash_out || !storage_is_initialized(root_path)) return -1;

    char *target_path = NULL;
    char detached_hash[ANEDOTA_SHA256_HEX_SIZE];
    int is_detached = 0;
    if (resolve_head_target(root_path, &target_path,
                            detached_hash, &is_detached) != 0)
    {
        return -1;
    }

    if (is_detached)
    {
        free(target_path);
        memcpy(hash_out, detached_hash, ANEDOTA_SHA256_HEX_SIZE);
        return 1;
    }

    const int ref_exists = pal_file_exists(target_path);
    if (ref_exists < 0)
    {
        free(target_path);
        return -1;
    }
    if (ref_exists == 0)
    {
        free(target_path);
        hash_out[0] = '\0';
        return 0;
    }

    char ref_content[128];
    const int read_status = read_small_text_file(target_path, ref_content,
                                                 sizeof(ref_content));
    free(target_path);
    if (read_status != 0 || !is_valid_sha256_hex(ref_content)) return -1;

    memcpy(hash_out, ref_content, ANEDOTA_SHA256_HEX_SIZE);
    return 1;
}

static int ensure_default_head_file(const char *root_path)
{
    char *head_path = join_path(root_path, "HEAD");
    if (!head_path) return -1;

    const int exists = pal_file_exists(head_path);
    int result = 0;
    if (exists < 0)
    {
        result = -1;
    }
    else if (exists == 0)
    {
        result = write_text_file_atomic(head_path,
                                        "ref: referencias/branches/main\n");
    }

    free(head_path);
    return result;
}

int anedota_ref_update_head(const char *root_path, const char *commit_hash)
{
    if (!storage_is_initialized(root_path) || !is_valid_sha256_hex(commit_hash))
    {
        return -1;
    }

    anedota_commit_t *commit = NULL;
    if (anedota_commit_read(root_path, commit_hash, &commit) != 0) return -1;
    free(commit);

    if (ensure_default_head_file(root_path) != 0) return -1;

    char *target_path = NULL;
    char detached_hash[ANEDOTA_SHA256_HEX_SIZE];
    int is_detached = 0;
    if (resolve_head_target(root_path, &target_path,
                            detached_hash, &is_detached) != 0)
    {
        return -1;
    }

    char payload[ANEDOTA_SHA256_HEX_SIZE + 1];
    memcpy(payload, commit_hash, ANEDOTA_SHA256_HEX_SIZE - 1);
    payload[ANEDOTA_SHA256_HEX_SIZE - 1] = '\n';
    payload[ANEDOTA_SHA256_HEX_SIZE] = '\0';

    const int status = write_text_file_atomic(target_path, payload);
    free(target_path);
    return status;
}
