#include <anedota/commit.h>
#include <anedota/ref.h>
#include <anedota/storage.h>
#include <anedota/tree.h>
#include <pal/pal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char commit_magic[] = {
    'A', 'N', 'E', 'D', 'O', 'T', 'A', 'C'
};

typedef struct
{
    const unsigned char *tree_hash;
    const unsigned char *parent_hash;
    const unsigned char *author;
    size_t author_length;
    const unsigned char *timestamp;
    size_t timestamp_length;
    const unsigned char *message;
    size_t message_length;
} parsed_commit_t;

static char *join_storage_path(const char *base_path, const char *relative_path)
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

static int is_valid_hex_bytes(const unsigned char *bytes, size_t length)
{
    if (!bytes || length != ANEDOTA_SHA256_HEX_SIZE - 1) return 0;

    for (size_t i = 0; i < length; ++i)
    {
        const unsigned char c = bytes[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
    }
    return 1;
}

static int is_valid_sha256_hex(const char *hash)
{
    if (!hash) return 0;
    return is_valid_hex_bytes((const unsigned char *)hash, strlen(hash));
}

static int is_valid_text_field(const char *text)
{
    if (!text || text[0] == '\0') return 0;
    return strlen(text) <= UINT32_MAX;
}

static int write_all(pal_file_t *file, const void *buffer, size_t size)
{
    const char *bytes = buffer;
    while (size > 0)
    {
        const size_t chunk = size > 4096 ? 4096 : size;
        const int written = pal_write(file, bytes, chunk);
        if (written <= 0 || (size_t)written > chunk) return -1;
        bytes += written;
        size -= (size_t)written;
    }
    return 0;
}

static int write_u32_be(pal_file_t *file, uint32_t value)
{
    const unsigned char bytes[4] = {
        (unsigned char)(value >> 24),
        (unsigned char)(value >> 16),
        (unsigned char)(value >> 8),
        (unsigned char)value
    };
    return write_all(file, bytes, sizeof(bytes));
}

static uint32_t read_u32_be(const unsigned char *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static int write_length_prefixed(pal_file_t *file, const char *text)
{
    const size_t length = strlen(text);
    if (write_u32_be(file, (uint32_t)length) != 0) return -1;
    return write_all(file, text, length);
}

static int verify_tree_exists(const char *root_path, const char *tree_hash)
{
    anedota_tree_t *tree = NULL;
    if (anedota_tree_read(root_path, tree_hash, &tree) != 0) return -1;
    free(tree);
    return 0;
}

static int verify_parent_exists(const char *root_path, const char *parent_hash)
{
    anedota_commit_t *parent = NULL;
    if (anedota_commit_read(root_path, parent_hash, &parent) != 0) return -1;
    free(parent);
    return 0;
}

static int has_parent_hash(const anedota_commit_t *commit)
{
    return commit->parent_hash != NULL && commit->parent_hash[0] != '\0';
}

static int validate_commit_input(const char *root_path,
                                 const anedota_commit_t *commit)
{
    if (!commit ||
        !is_valid_sha256_hex(commit->tree_hash) ||
        !is_valid_text_field(commit->author) ||
        !is_valid_text_field(commit->timestamp) ||
        !is_valid_text_field(commit->message))
    {
        return -1;
    }

    if (verify_tree_exists(root_path, commit->tree_hash) != 0) return -1;

    if (has_parent_hash(commit))
    {
        if (!is_valid_sha256_hex(commit->parent_hash) ||
            verify_parent_exists(root_path, commit->parent_hash) != 0)
        {
            return -1;
        }
    }

    return 0;
}

static int serialize_commit_file(pal_file_t *file,
                                 const anedota_commit_t *commit)
{
    const unsigned char version = 1;
    const unsigned char has_parent = has_parent_hash(commit) ? 1 : 0;

    if (write_all(file, commit_magic, sizeof(commit_magic)) != 0 ||
        write_all(file, &version, sizeof(version)) != 0 ||
        write_all(file, commit->tree_hash, ANEDOTA_SHA256_HEX_SIZE - 1) != 0 ||
        write_all(file, &has_parent, sizeof(has_parent)) != 0)
    {
        return -1;
    }

    if (has_parent &&
        write_all(file, commit->parent_hash, ANEDOTA_SHA256_HEX_SIZE - 1) != 0)
    {
        return -1;
    }

    if (write_length_prefixed(file, commit->author) != 0 ||
        write_length_prefixed(file, commit->timestamp) != 0 ||
        write_length_prefixed(file, commit->message) != 0)
    {
        return -1;
    }

    return 0;
}

int anedota_commit_write(const char *root_path,
                         const anedota_commit_t *commit,
                         char hash_out[ANEDOTA_SHA256_HEX_SIZE])
{
    if (!root_path || root_path[0] == '\0' || !hash_out) return -1;

    char *objects_path = join_storage_path(root_path, "objetos");
    if (!objects_path) return -1;

    if (pal_dir_exists(objects_path) != 1 ||
        validate_commit_input(root_path, commit) != 0)
    {
        free(objects_path);
        return -1;
    }

    char *temp_path = join_storage_path(objects_path, ".commit-XXXXXX");
    free(objects_path);
    if (!temp_path) return -1;

    pal_file_t *temp = pal_open_temp(temp_path);
    if (!temp)
    {
        free(temp_path);
        return -1;
    }

    int result = serialize_commit_file(temp, commit);
    if (pal_close(temp) != 0) result = -1;

    char commit_hash[ANEDOTA_SHA256_HEX_SIZE];
    if (result == 0 &&
        anedota_blob_write_file(root_path, temp_path, commit_hash) != 0)
    {
        result = -1;
    }
    if (pal_file_delete(temp_path) != 0) result = -1;
    free(temp_path);

    if (result == 0 &&
        anedota_ref_update_head(root_path, commit_hash) != 0)
    {
        result = -1;
    }

    if (result == 0)
    {
        memcpy(hash_out, commit_hash, sizeof(commit_hash));
    }
    return result;
}

static int parse_text_field(const unsigned char **cursor, size_t *remaining,
                            const unsigned char **text_out, size_t *length_out)
{
    if (*remaining < 4) return -1;
    const size_t length = (size_t)read_u32_be(*cursor);
    *cursor += 4;
    *remaining -= 4;

    if (length == 0 || length > *remaining) return -1;
    for (size_t i = 0; i < length; ++i)
    {
        if ((*cursor)[i] == '\0') return -1;
    }

    *text_out = *cursor;
    *length_out = length;
    *cursor += length;
    *remaining -= length;
    return 0;
}

static int parse_commit_payload(const unsigned char *payload,
                                size_t payload_size,
                                parsed_commit_t *parsed)
{
    const size_t hash_len = ANEDOTA_SHA256_HEX_SIZE - 1;
    const size_t header_size = sizeof(commit_magic) + 1 + hash_len + 1;
    if (!payload || payload_size < header_size ||
        memcmp(payload, commit_magic, sizeof(commit_magic)) != 0 ||
        payload[sizeof(commit_magic)] != 1)
    {
        return -1;
    }

    const unsigned char *cursor = payload + sizeof(commit_magic) + 1;
    size_t remaining = payload_size - (sizeof(commit_magic) + 1);

    parsed->tree_hash = cursor;
    if (!is_valid_hex_bytes(parsed->tree_hash, hash_len)) return -1;
    cursor += hash_len;
    remaining -= hash_len;

    const unsigned char has_parent = *cursor++;
    remaining -= 1;
    if (has_parent != 0 && has_parent != 1) return -1;

    parsed->parent_hash = NULL;
    if (has_parent == 1)
    {
        if (remaining < hash_len || !is_valid_hex_bytes(cursor, hash_len))
        {
            return -1;
        }
        parsed->parent_hash = cursor;
        cursor += hash_len;
        remaining -= hash_len;
    }

    if (parse_text_field(&cursor, &remaining,
                         &parsed->author, &parsed->author_length) != 0 ||
        parse_text_field(&cursor, &remaining,
                         &parsed->timestamp, &parsed->timestamp_length) != 0 ||
        parse_text_field(&cursor, &remaining,
                         &parsed->message, &parsed->message_length) != 0 ||
        remaining != 0)
    {
        return -1;
    }

    return 0;
}

static char *copy_string_into(char **dest, const unsigned char *src,
                              size_t length)
{
    char *start = *dest;
    memcpy(start, src, length);
    start[length] = '\0';
    *dest += length + 1;
    return start;
}

static int add_size_checked(size_t *total, size_t delta)
{
    if (delta > SIZE_MAX - *total) return -1;
    *total += delta;
    return 0;
}

int anedota_commit_read(const char *root_path, const char *hash,
                        anedota_commit_t **commit_out)
{
    if (!root_path || root_path[0] == '\0' ||
        !is_valid_sha256_hex(hash) || !commit_out)
    {
        return -1;
    }

    char *payload = NULL;
    size_t payload_size = 0;
    if (anedota_blob_read(root_path, hash, &payload, &payload_size) != 0)
    {
        return -1;
    }

    parsed_commit_t parsed;
    if (parse_commit_payload((const unsigned char *)payload,
                             payload_size, &parsed) != 0)
    {
        free(payload);
        return -1;
    }

    size_t total_size = sizeof(anedota_commit_t);
    const size_t hash_len = ANEDOTA_SHA256_HEX_SIZE - 1;
    if (add_size_checked(&total_size, ANEDOTA_SHA256_HEX_SIZE) != 0 ||
        (parsed.parent_hash &&
         add_size_checked(&total_size, ANEDOTA_SHA256_HEX_SIZE) != 0) ||
        add_size_checked(&total_size, parsed.author_length + 1) != 0 ||
        add_size_checked(&total_size, parsed.timestamp_length + 1) != 0 ||
        add_size_checked(&total_size, parsed.message_length + 1) != 0)
    {
        free(payload);
        return -1;
    }

    anedota_commit_t *commit = malloc(total_size);
    if (!commit)
    {
        free(payload);
        return -1;
    }

    char *strings = (char *)commit + sizeof(*commit);
    commit->tree_hash = copy_string_into(&strings, parsed.tree_hash, hash_len);
    commit->parent_hash = parsed.parent_hash
        ? copy_string_into(&strings, parsed.parent_hash, hash_len)
        : NULL;
    commit->author = copy_string_into(&strings, parsed.author,
                                      parsed.author_length);
    commit->timestamp = copy_string_into(&strings, parsed.timestamp,
                                         parsed.timestamp_length);
    commit->message = copy_string_into(&strings, parsed.message,
                                       parsed.message_length);

    free(payload);
    *commit_out = commit;
    return 0;
}
