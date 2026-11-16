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
