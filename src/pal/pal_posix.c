#include <pal/pal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

struct pal_file
{
    FILE *file;
};

struct pal_file *pal_open(const char *path, char *mode)
{
    if (!path || !mode) return NULL;

    pal_file_t *pf = (pal_file_t *)malloc(sizeof(pal_file_t));
    if (!pf) return NULL;

    pf->file = fopen(path, mode);
    if (pf->file == NULL)
    {
        free(pf);
        return NULL;
    }
    return pf;
}

int pal_close(struct pal_file *file)
{
    if (!file || !file->file) return -1;

    const int res = fclose(file->file);
    free(file);
    return res == 0 ? 0 : -1;
}

int pal_read(struct pal_file *file, char *buffer, size_t size)
{
    if (!file || !file->file || !buffer) return -1;
    if (size == 0) return 0;

    size_t read_bytes = fread(buffer, 1, size, file->file);
    if (read_bytes == 0 && ferror(file->file))
    {
        return -1;
    }
    return (int)read_bytes;
}

int pal_write(struct pal_file *file, const char *buffer, size_t size)
{
    if (!file || !file->file || !buffer) return -1;
    if (size == 0) return 0;

    size_t written_bytes = fwrite(buffer, 1, size, file->file);
    if (written_bytes < size && ferror(file->file))
    {
        return -1;
    }
    return (int)written_bytes;
}

int pal_dir_create(const char *path)
{
    if (!path) return -1;

    return mkdir(path, 0755) == 0 ? 0 : -1;
}

int pal_dir_delete(const char *path)
{
    if (!path) return -1;

    return rmdir(path) == 0 ? 0 : -1;
}

