#ifndef ANEDOTA_STORAGE_H
#define ANEDOTA_STORAGE_H

#include <anedota/hash.h>

int anedota_storage_init(const char *root_path);

/* Writes the contents of source_path as a Blob object in root_path/objetos/<hash>.
 * Only the raw file content is stored, without original file metadata.
 * On success writes the 64-character hexadecimal SHA-256 and a NUL to hash_out
 * and returns 0. On invalid arguments or I/O error returns -1. */
int anedota_blob_write_file(const char *root_path, const char *source_path,
                            char hash_out[ANEDOTA_SHA256_HEX_SIZE]);

#endif
