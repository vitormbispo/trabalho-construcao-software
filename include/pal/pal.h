#ifndef PAL_H
#define PAL_H
#include <stddef.h>
typedef struct pal_file pal_file_t;

struct pal_file *pal_open(const char *path, char *mode);
/* Creates a unique temporary file by replacing the final XXXXXX in path. */
struct pal_file *pal_open_temp(char *path);
int pal_file_rename(const char *source, const char *destination);
int pal_file_delete(const char *path);
int pal_close(struct pal_file *file);
int pal_read(struct pal_file *file, char *buffer, size_t size);
int pal_write(struct pal_file *file, const char *buffer, size_t size);

int pal_dir_create(const char *path);
int pal_dir_delete(const char *path);
int pal_dir_exists(const char *path);

#endif

