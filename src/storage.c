#include <anedota/storage.h>
#include <pal/pal.h>
#include <stdlib.h>
#include <string.h>

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

static int copy_file_contents(const char *source_path, const char *destination_path)
{
    pal_file_t *source = pal_open(source_path, "rb");
    if (!source) return -1;

    pal_file_t *destination = pal_open(destination_path, "wb");
    if (!destination)
    {
        pal_close(source);
        return -1;
    }

    char buffer[4096];
    int read_bytes;
    int result = 0;

    while ((read_bytes = pal_read(source, buffer, sizeof(buffer))) > 0)
    {
        if (pal_write(destination, buffer, (size_t)read_bytes) != read_bytes)
        {
            result = -1;
            break;
        }
    }
    if (read_bytes < 0) result = -1;

    if (pal_close(destination) != 0) result = -1;
    if (pal_close(source) != 0) result = -1;
    return result;
}

int anedota_blob_write_file(const char *root_path, const char *source_path,
                            char hash_out[ANEDOTA_SHA256_HEX_SIZE])
{
    if (!root_path || root_path[0] == '\0' ||
        !source_path || source_path[0] == '\0' || !hash_out)
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

    pal_file_t *source = pal_open(source_path, "rb");
    if (!source)
    {
        free(objects_path);
        return -1;
    }

    char hash[ANEDOTA_SHA256_HEX_SIZE];
    const int hash_result = anedota_sha256_file(source, hash);
    if (pal_close(source) != 0 || hash_result != 0)
    {
        free(objects_path);
        return -1;
    }

    char *blob_path = storage_path(objects_path, hash);
    free(objects_path);
    if (!blob_path) return -1;

    const int copy_result = copy_file_contents(source_path, blob_path);
    free(blob_path);
    if (copy_result != 0) return -1;

    memcpy(hash_out, hash, ANEDOTA_SHA256_HEX_SIZE);
    return 0;
}
