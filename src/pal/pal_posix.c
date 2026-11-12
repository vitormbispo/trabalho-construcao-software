#include <pal/pal.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct pal_file
{
    FILE *file;
} pal_file_t;

struct pal_file *pal_open(const char *path, char *mode)
{
    pal_file_t *pf = (pal_file_t *)malloc(sizeof(pal_file_t));
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

    return fclose(file->file);
}
