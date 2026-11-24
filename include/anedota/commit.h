#ifndef ANEDOTA_COMMIT_H
#define ANEDOTA_COMMIT_H

#include <anedota/hash.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    const char *tree_hash;
    const char *parent_hash;
    const char *author;
    const char *timestamp;
    const char *message;
} anedota_commit_t;

int anedota_commit_write(const char *root_path,
                         const anedota_commit_t *commit,
                         char hash_out[ANEDOTA_SHA256_HEX_SIZE]);

int anedota_commit_read(const char *root_path, const char *hash,
                        anedota_commit_t **commit_out);

#ifdef __cplusplus
}
#endif

#endif
