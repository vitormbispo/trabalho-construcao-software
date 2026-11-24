#ifndef PAL_H
#define PAL_H
#include <stddef.h>
typedef struct pal_file pal_file_t;
typedef struct pal_directory pal_directory_t;

typedef enum
{
    PAL_DIR_ENTRY_FILE,
    PAL_DIR_ENTRY_DIRECTORY,
    PAL_DIR_ENTRY_OTHER
} pal_dir_entry_type_t;

struct pal_file *pal_open(const char *path, char *mode);
/* Creates a unique temporary file by replacing the final XXXXXX in path. */
struct pal_file *pal_open_temp(char *path);
int pal_file_exists(const char *path);
int pal_file_rename(const char *source, const char *destination);
int pal_file_delete(const char *path);
int pal_close(struct pal_file *file);
int pal_read(struct pal_file *file, char *buffer, size_t size);
int pal_write(struct pal_file *file, const char *buffer, size_t size);

int pal_dir_create(const char *path);
int pal_dir_delete(const char *path);
int pal_dir_exists(const char *path);
pal_directory_t *pal_dir_open(const char *path);
/* Returns 1 for an entry, 0 at end, or -1 on error.
 * The name is owned by the iterator and remains valid until the next call. */
int pal_dir_next(pal_directory_t *directory, const char **name_out,
                 pal_dir_entry_type_t *type_out);
int pal_dir_close(pal_directory_t *directory);

#endif

