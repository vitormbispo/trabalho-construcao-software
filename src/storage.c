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
    if (!blob_path || pal_file_rename(temp_path, blob_path) != 0) result = -1;
    if (result != 0) pal_file_delete(temp_path);
    else memcpy(hash_out, hash, ANEDOTA_SHA256_HEX_SIZE);
    free(blob_path);
    free(temp_path);
    free(objects_path);
    return result;
}
