#define _POSIX_C_SOURCE 200809L
#include <pal/pal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

struct pal_file
{
    FILE *file;
};

struct pal_directory
{
    DIR *directory;
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

struct pal_file *pal_open_temp(char *path)
{
    if (!path) return NULL;
    pal_file_t *pf = malloc(sizeof(*pf));
    if (!pf) return NULL;
    int fd = mkstemp(path);
    if (fd < 0) { free(pf); return NULL; }
    pf->file = fdopen(fd, "wb");
    if (!pf->file)
    {
        close(fd);
        unlink(path);
        free(pf);
        return NULL;
    }
    return pf;
}

int pal_file_exists(const char *path)
{
    if (!path) return -1;

    struct stat st;
    if (stat(path, &st) == 0)
    {
        return S_ISREG(st.st_mode) ? 1 : 0;
    }

    return errno == ENOENT || errno == ENOTDIR ? 0 : -1;
}

int pal_file_rename(const char *source, const char *destination)
{
    if (!source || !destination) return -1;
    return rename(source, destination) == 0 ? 0 : -1;
}

int pal_file_delete(const char *path)
{
    if (!path) return -1;
    return unlink(path) == 0 ? 0 : -1;
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

int pal_dir_exists(const char *path)
{
    if (!path) return -1;

    struct stat st;
    if (stat(path, &st) == 0)
    {
        return S_ISDIR(st.st_mode) ? 1 : 0;
    }

    return errno == ENOENT || errno == ENOTDIR ? 0 : -1;
}

pal_directory_t *pal_dir_open(const char *path)
{
    if (!path || path[0] == '\0') return NULL;

    DIR *directory = opendir(path);
    if (!directory) return NULL;

    pal_directory_t *iterator = malloc(sizeof(*iterator));
    if (!iterator)
    {
        closedir(directory);
        return NULL;
    }

    iterator->directory = directory;
    return iterator;
}

int pal_dir_next(pal_directory_t *directory, const char **name_out,
                 pal_dir_entry_type_t *type_out)
{
    if (!directory || !directory->directory || !name_out || !type_out) return -1;

    struct dirent *entry;
    while (1)
    {
        errno = 0;
        entry = readdir(directory->directory);
        if (!entry)
        {
            return errno == 0 ? 0 : -1;
        }

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        struct stat st;
        if (fstatat(dirfd(directory->directory), entry->d_name, &st,
                    AT_SYMLINK_NOFOLLOW) != 0)
        {
            return -1;
        }

        pal_dir_entry_type_t type = PAL_DIR_ENTRY_OTHER;
        if (S_ISREG(st.st_mode))
        {
            type = PAL_DIR_ENTRY_FILE;
        }
        else if (S_ISDIR(st.st_mode))
        {
            type = PAL_DIR_ENTRY_DIRECTORY;
        }

        *name_out = entry->d_name;
        *type_out = type;
        return 1;
    }
}

int pal_dir_close(pal_directory_t *directory)
{
    if (!directory || !directory->directory) return -1;

    const int result = closedir(directory->directory);
    free(directory);
    return result == 0 ? 0 : -1;
}

