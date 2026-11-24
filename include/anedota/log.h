#ifndef ANEDOTA_LOG_H
#define ANEDOTA_LOG_H

#include <anedota/hash.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    char hash[ANEDOTA_SHA256_HEX_SIZE];
    const char *tree_hash;
    const char *parent_hash;
    const char *author;
    const char *timestamp;
    const char *message;
} anedota_log_entry_t;

typedef struct
{
    size_t count;
    anedota_log_entry_t entries[];
} anedota_log_t;

/* Reads the commit history starting from the current HEAD reference, ordered
 * from most recent to oldest.
 * On success allocates a single block containing the history and all strings,
 * assigns it to *log_out, and returns 0 (with count == 0 when the storage has
 * no commits). The caller must release *log_out with free().
 * On invalid arguments, uninitialized storage, corrupt reference/commit, or
 * cycle detection returns -1 without changing *log_out. */
int anedota_log_read(const char *root_path, anedota_log_t **log_out);

/* Reads the commit history starting from a specific commit hash, ordered from
 * that commit backwards to the root commit.
 * Same ownership and error semantics as anedota_log_read. */
int anedota_log_read_from(const char *root_path,
                          const char *start_commit_hash,
                          anedota_log_t **log_out);

#ifdef __cplusplus
}
#endif

#endif
