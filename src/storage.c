#include <anedota/storage.h>
#include <anedota/tree.h>
#include <pal/pal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char tree_magic[] = { 'A', 'N', 'E', 'D', 'O', 'T', 'A', 'T' };

static char *storage_path(const char *root_path, const char *relative_path)
{
    const size_t root_length = strlen(root_path);
    const size_t relative_length = strlen(relative_path);
    const size_t separator_length = root_path[root_length - 1] == '/' ? 0 : 1;
    char *path = malloc(root_length + separator_length + relative_length + 1);

    if (!path) return NULL;

    memcpy(path, root_path, root_length);
    if (separator_length)
    {
        path[root_length] = '/';
    }
    memcpy(path + root_length + separator_length, relative_path, relative_length + 1);
    return path;
}

static int ensure_directory(const char *path)
{
    const int exists = pal_dir_exists(path);
    if (exists == 1) return 0;
    if (exists < 0) return -1;

    if (pal_dir_create(path) == 0) return 0;
    return pal_dir_exists(path) == 1 ? 0 : -1;
}

int anedota_storage_init(const char *root_path)
{
    if (!root_path || root_path[0] == '\0') return -1;

    char *objects_path = storage_path(root_path, "objetos");
    char *references_path = storage_path(root_path, "referencias");
    char *branches_path = storage_path(root_path, "referencias/branches");

    if (!objects_path || !references_path || !branches_path)
    {
        free(objects_path);
        free(references_path);
        free(branches_path);
        return -1;
    }

    int result = 0;
    if (ensure_directory(root_path) != 0 ||
        ensure_directory(objects_path) != 0 ||
        ensure_directory(references_path) != 0 ||
        ensure_directory(branches_path) != 0)
    {
        result = -1;
    }

    free(objects_path);
    free(references_path);
    free(branches_path);
    return result;
}

int anedota_blob_write_file(const char *root_path, const char *source_path,
                            char hash_out[ANEDOTA_SHA256_HEX_SIZE])
{
    if (!root_path || root_path[0] == '\0' ||
        !source_path || source_path[0] == '\0' || !hash_out) return -1;

    char *objects_path = storage_path(root_path, "objetos");
    if (!objects_path) return -1;
    if (pal_dir_exists(objects_path) != 1)
    {
        free(objects_path);
        return -1;
    }

    pal_file_t *source = pal_open(source_path, "rb");
    char *temp_path = storage_path(objects_path, ".blob-XXXXXX");
    if (!source || !temp_path)
    {
        if (source) pal_close(source);
        free(temp_path);
        free(objects_path);
        return -1;
    }
    pal_file_t *temp = pal_open_temp(temp_path);
    if (!temp)
    {
        pal_close(source);
        free(temp_path);
        free(objects_path);
        return -1;
    }

    char buffer[4096];
    int count, result = 0;
    while ((count = pal_read(source, buffer, sizeof(buffer))) > 0)
    {
        if (pal_write(temp, buffer, (size_t)count) != count)
        {
            result = -1;
            break;
        }
    }
    if (count < 0) result = -1;
    if (pal_close(source) != 0) result = -1;
    if (pal_close(temp) != 0) result = -1;

    char hash[ANEDOTA_SHA256_HEX_SIZE];
    if (result == 0)
    {
        temp = pal_open(temp_path, "rb");
        if (!temp) result = -1;
        else
        {
            if (anedota_sha256_file(temp, hash) != 0) result = -1;
            if (pal_close(temp) != 0) result = -1;
        }
    }
    char *blob_path = result == 0 ? storage_path(objects_path, hash) : NULL;
    if (!blob_path)
    {
        result = -1;
    }
    else
    {
        const int exists = pal_file_exists(blob_path);
        if (exists == 1)
        {
            if (pal_file_delete(temp_path) != 0) result = -1;
        }
        else if (exists == 0)
        {
            if (pal_file_rename(temp_path, blob_path) != 0) result = -1;
        }
        else
        {
            result = -1;
        }
    }
    if (result != 0) pal_file_delete(temp_path);
    else memcpy(hash_out, hash, ANEDOTA_SHA256_HEX_SIZE);
    free(blob_path);
    free(temp_path);
    free(objects_path);
    return result;
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

int anedota_blob_read(const char *root_path, const char *hash,
                      char **data_out, size_t *size_out)
{
    if (!root_path || root_path[0] == '\0' ||
        !is_valid_sha256_hex(hash) || !data_out || !size_out)
    {
        return -1;
    }

    char *objects_path = storage_path(root_path, "objetos");
    if (!objects_path) return -1;
    if (pal_dir_exists(objects_path) != 1)
    {
        free(objects_path);
        return -1;
    }

    char *blob_path = storage_path(objects_path, hash);
    free(objects_path);
    if (!blob_path) return -1;

    if (pal_file_exists(blob_path) != 1)
    {
        free(blob_path);
        return -1;
    }

    pal_file_t *file = pal_open(blob_path, "rb");
    free(blob_path);
    if (!file) return -1;

    size_t capacity = 4096;
    size_t length = 0;
    char *data = malloc(capacity + 1);
    if (!data)
    {
        pal_close(file);
        return -1;
    }

    char chunk[4096];
    int count;
    int result = 0;
    while ((count = pal_read(file, chunk, sizeof(chunk))) > 0)
    {
        const size_t read_bytes = (size_t)count;
        if (read_bytes > SIZE_MAX - 1 - length)
        {
            result = -1;
            break;
        }
        if (length + read_bytes > capacity)
        {
            size_t new_capacity = capacity;
            while (new_capacity < length + read_bytes)
            {
                if (new_capacity > (SIZE_MAX - 1) / 2)
                {
                    new_capacity = length + read_bytes;
                    break;
                }
                new_capacity *= 2;
            }
            char *grown = realloc(data, new_capacity + 1);
            if (!grown)
            {
                result = -1;
                break;
            }
            data = grown;
            capacity = new_capacity;
        }
        memcpy(data + length, chunk, read_bytes);
        length += read_bytes;
    }
    if (count < 0) result = -1;
    if (pal_close(file) != 0) result = -1;

    if (result != 0)
    {
        free(data);
        return -1;
    }

    data[length] = '\0';
    *data_out = data;
    *size_out = length;
    return 0;
}

static int is_valid_tree_name(const char *name)
{
    if (!name || name[0] == '\0' ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
        strchr(name, '/') != NULL)
    {
        return 0;
    }

    return 1;
}

static int is_valid_tree_name_bytes(const unsigned char *name, size_t length)
{
    if (!name || length == 0 ||
        (length == 1 && name[0] == '.') ||
        (length == 2 && name[0] == '.' && name[1] == '.'))
    {
        return 0;
    }

    for (size_t i = 0; i < length; ++i)
    {
        if (name[i] == '\0' || name[i] == '/')
        {
            return 0;
        }
    }

    return 1;
}

static int is_valid_tree_entry_type(anedota_tree_entry_type_t type)
{
    return type == ANEDOTA_TREE_ENTRY_BLOB || type == ANEDOTA_TREE_ENTRY_TREE;
}

static int compare_tree_entries(const void *left, const void *right)
{
    const anedota_tree_entry_t *left_entry = left;
    const anedota_tree_entry_t *right_entry = right;
    return strcmp(left_entry->name, right_entry->name);
}

static int write_all(pal_file_t *file, const void *buffer, size_t size)
{
    const char *bytes = buffer;
    while (size > 0)
    {
        const size_t chunk_size = size > 4096 ? 4096 : size;
        const int written = pal_write(file, bytes, chunk_size);
        if (written <= 0 || (size_t)written > chunk_size) return -1;
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

int anedota_tree_write(const char *root_path,
                       const anedota_tree_entry_t *entries, size_t count,
                       char hash_out[ANEDOTA_SHA256_HEX_SIZE])
{
    if (!root_path || root_path[0] == '\0' || !hash_out ||
        (count > 0 && !entries) || count > UINT32_MAX ||
        count > SIZE_MAX / sizeof(*entries))
    {
        return -1;
    }

    char *objects_path = storage_path(root_path, "objetos");
    if (!objects_path) return -1;
    if (pal_dir_exists(objects_path) != 1)
    {
        free(objects_path);
        return -1;
    }

    anedota_tree_entry_t *sorted_entries = NULL;
    if (count > 0)
    {
        sorted_entries = malloc(count * sizeof(*sorted_entries));
        if (!sorted_entries)
        {
            free(objects_path);
            return -1;
        }
        memcpy(sorted_entries, entries, count * sizeof(*sorted_entries));
    }

    int result = 0;
    for (size_t i = 0; i < count; ++i)
    {
        if (!is_valid_tree_name(sorted_entries[i].name) ||
            !is_valid_tree_entry_type(sorted_entries[i].type) ||
            !is_valid_sha256_hex(sorted_entries[i].hash) ||
            strlen(sorted_entries[i].name) > UINT32_MAX)
        {
            result = -1;
            break;
        }

        char *child_path = storage_path(objects_path, sorted_entries[i].hash);
        if (!child_path)
        {
            result = -1;
            break;
        }
        const int child_exists = pal_file_exists(child_path);
        free(child_path);
        if (child_exists != 1)
        {
            result = -1;
            break;
        }
    }

    if (result == 0 && count > 1)
    {
        qsort(sorted_entries, count, sizeof(*sorted_entries), compare_tree_entries);
        for (size_t i = 1; i < count; ++i)
        {
            if (strcmp(sorted_entries[i - 1].name, sorted_entries[i].name) == 0)
            {
                result = -1;
                break;
            }
        }
    }

    char *temp_path = result == 0 ? storage_path(objects_path, ".tree-XXXXXX") : NULL;
    if (result == 0 && !temp_path) result = -1;

    if (result == 0)
    {
        pal_file_t *temp = pal_open_temp(temp_path);
        if (!temp)
        {
            result = -1;
        }
        else
        {
            const unsigned char version = 1;
            if (write_all(temp, tree_magic, sizeof(tree_magic)) != 0 ||
                write_all(temp, &version, sizeof(version)) != 0 ||
                write_u32_be(temp, (uint32_t)count) != 0)
            {
                result = -1;
            }

            for (size_t i = 0; result == 0 && i < count; ++i)
            {
                const unsigned char type = (unsigned char)sorted_entries[i].type;
                const size_t name_length = strlen(sorted_entries[i].name);
                if (write_all(temp, &type, sizeof(type)) != 0 ||
                    write_u32_be(temp, (uint32_t)name_length) != 0 ||
                    write_all(temp, sorted_entries[i].hash,
                              ANEDOTA_SHA256_HEX_SIZE - 1) != 0 ||
                    write_all(temp, sorted_entries[i].name, name_length) != 0)
                {
                    result = -1;
                }
            }

            if (pal_close(temp) != 0) result = -1;

            char tree_hash[ANEDOTA_SHA256_HEX_SIZE];
            if (result == 0 &&
                anedota_blob_write_file(root_path, temp_path, tree_hash) != 0)
            {
                result = -1;
            }
            if (pal_file_delete(temp_path) != 0) result = -1;
            if (result == 0)
            {
                memcpy(hash_out, tree_hash, sizeof(tree_hash));
            }
        }
    }

    free(temp_path);
    free(sorted_entries);
    free(objects_path);
    return result;
}

static uint32_t read_u32_be(const unsigned char *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static int validate_tree_payload(const unsigned char *payload, size_t payload_size,
                                 size_t *count_out, size_t *strings_size_out)
{
    const size_t header_size = sizeof(tree_magic) + 1 + 4;
    const size_t record_header_size = 1 + 4 + ANEDOTA_SHA256_HEX_SIZE - 1;
    const size_t minimum_record_size = record_header_size + 1;
    if (!payload || payload_size < header_size ||
        memcmp(payload, tree_magic, sizeof(tree_magic)) != 0 ||
        payload[sizeof(tree_magic)] != 1)
    {
        return -1;
    }

    const size_t count = (size_t)read_u32_be(payload + sizeof(tree_magic) + 1);
    size_t remaining = payload_size - header_size;
    if (count > remaining / minimum_record_size) return -1;

    const unsigned char *cursor = payload + header_size;
    const unsigned char *previous_name = NULL;
    size_t previous_name_length = 0;
    size_t strings_size = 0;

    for (size_t i = 0; i < count; ++i)
    {
        if (remaining < record_header_size) return -1;
        const unsigned char type = *cursor++;
        const size_t name_length = (size_t)read_u32_be(cursor);
        cursor += 4;
        remaining -= 1 + 4;

        if (type != ANEDOTA_TREE_ENTRY_BLOB && type != ANEDOTA_TREE_ENTRY_TREE)
            return -1;
        if (remaining < ANEDOTA_SHA256_HEX_SIZE - 1 ||
            name_length == 0 ||
            name_length > remaining - (ANEDOTA_SHA256_HEX_SIZE - 1))
            return -1;

        const unsigned char *hash = cursor;
        for (size_t j = 0; j < ANEDOTA_SHA256_HEX_SIZE - 1; ++j)
        {
            const unsigned char c = hash[j];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                return -1;
        }
        cursor += ANEDOTA_SHA256_HEX_SIZE - 1;
        remaining -= ANEDOTA_SHA256_HEX_SIZE - 1;

        const unsigned char *name = cursor;
        if (!is_valid_tree_name_bytes(name, name_length)) return -1;
        if (previous_name)
        {
            const size_t common_length = previous_name_length < name_length
                ? previous_name_length : name_length;
            const int comparison = memcmp(previous_name, name, common_length);
            if (comparison > 0 ||
                (comparison == 0 && previous_name_length >= name_length))
            {
                return -1;
            }
        }

        if (strings_size > SIZE_MAX - 66 ||
            name_length > SIZE_MAX - strings_size - 66)
        {
            return -1;
        }
        strings_size += name_length + 66;
        previous_name = name;
        previous_name_length = name_length;
        cursor += name_length;
        remaining -= name_length;
    }

    if (remaining != 0) return -1;
    *count_out = count;
    *strings_size_out = strings_size;
    return 0;
}

int anedota_tree_read(const char *root_path, const char *hash,
                      anedota_tree_t **tree_out)
{
    if (!root_path || root_path[0] == '\0' ||
        !is_valid_sha256_hex(hash) || !tree_out)
    {
        return -1;
    }

    char *payload = NULL;
    size_t payload_size = 0;
    if (anedota_blob_read(root_path, hash, &payload, &payload_size) != 0)
        return -1;

    size_t count = 0;
    size_t strings_size = 0;
    if (validate_tree_payload((const unsigned char *)payload, payload_size,
                              &count, &strings_size) != 0)
    {
        free(payload);
        return -1;
    }

    if (count > (SIZE_MAX - sizeof(anedota_tree_t)) /
                    sizeof(anedota_tree_entry_t))
    {
        free(payload);
        return -1;
    }
    const size_t entries_size = count * sizeof(anedota_tree_entry_t);
    if (strings_size > SIZE_MAX - sizeof(anedota_tree_t) - entries_size)
    {
        free(payload);
        return -1;
    }

    anedota_tree_t *tree = malloc(sizeof(*tree) + entries_size + strings_size);
    if (!tree)
    {
        free(payload);
        return -1;
    }

    tree->count = count;
    char *strings = (char *)tree + sizeof(*tree) + entries_size;
    const unsigned char *cursor = (const unsigned char *)payload +
                                  sizeof(tree_magic) + 1 + 4;
    for (size_t i = 0; i < count; ++i)
    {
        tree->entries[i].type = (anedota_tree_entry_type_t)*cursor++;
        const size_t name_length = (size_t)read_u32_be(cursor);
        cursor += 4;
        tree->entries[i].hash = strings;
        memcpy(strings, cursor, ANEDOTA_SHA256_HEX_SIZE - 1);
        strings[ANEDOTA_SHA256_HEX_SIZE - 1] = '\0';
        strings += ANEDOTA_SHA256_HEX_SIZE;
        cursor += ANEDOTA_SHA256_HEX_SIZE - 1;
        tree->entries[i].name = strings;
        memcpy(strings, cursor, name_length);
        strings[name_length] = '\0';
        strings += name_length + 1;
        cursor += name_length;
    }

    free(payload);
    *tree_out = tree;
    return 0;
}

typedef struct
{
    char *name;
    pal_dir_entry_type_t pal_type;
    char hash[ANEDOTA_SHA256_HEX_SIZE];
} collected_dir_entry_t;

static char *duplicate_string(const char *text)
{
    const size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (!copy) return NULL;
    memcpy(copy, text, length + 1);
    return copy;
}

static void free_collected_entries(collected_dir_entry_t *entries, size_t count)
{
    if (!entries) return;
    for (size_t i = 0; i < count; ++i)
    {
        free(entries[i].name);
    }
    free(entries);
}

static int write_tree_directory_recursive(const char *root_path,
                                          const char *dir_path,
                                          char hash_out[ANEDOTA_SHA256_HEX_SIZE])
{
    pal_directory_t *directory = pal_dir_open(dir_path);
    if (!directory) return -1;

    collected_dir_entry_t *collected = NULL;
    size_t count = 0;
    size_t capacity = 0;
    int result = 0;

    const char *name = NULL;
    pal_dir_entry_type_t entry_type;
    int next_status;
    while ((next_status = pal_dir_next(directory, &name, &entry_type)) == 1)
    {
        if (entry_type == PAL_DIR_ENTRY_DIRECTORY &&
            strcmp(name, ".anedota") == 0)
        {
            continue;
        }

        if ((entry_type != PAL_DIR_ENTRY_FILE &&
             entry_type != PAL_DIR_ENTRY_DIRECTORY) ||
            !is_valid_tree_name(name))
        {
            result = -1;
            break;
        }

        if (count == capacity)
        {
            size_t new_capacity = capacity == 0 ? 16 : capacity * 2;
            if (new_capacity <= capacity ||
                new_capacity > SIZE_MAX / sizeof(*collected))
            {
                result = -1;
                break;
            }
            collected_dir_entry_t *grown = realloc(collected,
                                                   new_capacity * sizeof(*collected));
            if (!grown)
            {
                result = -1;
                break;
            }
            collected = grown;
            capacity = new_capacity;
        }

        char *name_copy = duplicate_string(name);
        if (!name_copy)
        {
            result = -1;
            break;
        }

        collected[count].name = name_copy;
        collected[count].pal_type = entry_type;
        collected[count].hash[0] = '\0';
        ++count;
    }

    if (next_status < 0) result = -1;
    if (pal_dir_close(directory) != 0) result = -1;

    anedota_tree_entry_t *tree_entries = NULL;
    if (result == 0 && count > 0)
    {
        if (count > SIZE_MAX / sizeof(*tree_entries))
        {
            result = -1;
        }
        else
        {
            tree_entries = malloc(count * sizeof(*tree_entries));
            if (!tree_entries) result = -1;
        }
    }

    for (size_t i = 0; result == 0 && i < count; ++i)
    {
        char *child_path = storage_path(dir_path, collected[i].name);
        if (!child_path)
        {
            result = -1;
            break;
        }

        if (collected[i].pal_type == PAL_DIR_ENTRY_FILE)
        {
            if (anedota_blob_write_file(root_path, child_path,
                                        collected[i].hash) != 0)
            {
                result = -1;
            }
            tree_entries[i].type = ANEDOTA_TREE_ENTRY_BLOB;
        }
        else
        {
            if (write_tree_directory_recursive(root_path, child_path,
                                               collected[i].hash) != 0)
            {
                result = -1;
            }
            tree_entries[i].type = ANEDOTA_TREE_ENTRY_TREE;
        }

        tree_entries[i].name = collected[i].name;
        tree_entries[i].hash = collected[i].hash;
        free(child_path);
    }

    char tree_hash[ANEDOTA_SHA256_HEX_SIZE];
    if (result == 0)
    {
        if (anedota_tree_write(root_path, tree_entries, count, tree_hash) != 0)
        {
            result = -1;
        }
        else
        {
            memcpy(hash_out, tree_hash, sizeof(tree_hash));
        }
    }

    free(tree_entries);
    free_collected_entries(collected, count);
    return result;
}

int anedota_tree_write_directory(const char *root_path, const char *dir_path,
                                 char hash_out[ANEDOTA_SHA256_HEX_SIZE])
{
    if (!root_path || root_path[0] == '\0' ||
        !dir_path || dir_path[0] == '\0' || !hash_out)
    {
        return -1;
    }

    char *objects_path = storage_path(root_path, "objetos");
    if (!objects_path) return -1;
    const int objects_exists = pal_dir_exists(objects_path);
    free(objects_path);
    if (objects_exists != 1 || pal_dir_exists(dir_path) != 1)
    {
        return -1;
    }

    return write_tree_directory_recursive(root_path, dir_path, hash_out);
}
