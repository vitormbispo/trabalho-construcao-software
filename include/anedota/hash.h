#ifndef ANEDOTA_HASH_H
#define ANEDOTA_HASH_H

#include <pal/pal.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ANEDOTA_SHA256_HEX_SIZE 65

/* Hashes an in-memory byte buffer.
 * On success writes 64 lowercase hexadecimal characters and a NUL, returning 0.
 * On invalid arguments or an oversized input returns -1 and leaves output
 * unchanged. */
int anedota_sha256_bytes(const void *data, size_t size,
                         char output[ANEDOTA_SHA256_HEX_SIZE]);

/* Hashes the remaining bytes of an open file. The caller retains ownership.
 * On success writes 64 lowercase hexadecimal characters and a NUL, returning 0.
 * On invalid arguments, a read error, or an oversized input returns -1 and
 * leaves output unchanged. */
int anedota_sha256_file(pal_file_t *file, char output[ANEDOTA_SHA256_HEX_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
