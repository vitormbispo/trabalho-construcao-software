#ifndef ANEDOTA_DIFF_H
#define ANEDOTA_DIFF_H

#include <anedota/hash.h>
#include <anedota/tree.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    ANEDOTA_DIFF_OK = 0,
    ANEDOTA_DIFF_ERR_INVALID_ARG = -1,
    ANEDOTA_DIFF_ERR_UNINITIALIZED = -2,
    ANEDOTA_DIFF_ERR_VERSION_NOT_FOUND = -3,
    ANEDOTA_DIFF_ERR_INVALID_VERSION = -4,
    ANEDOTA_DIFF_ERR_IO = -5
} anedota_diff_status_t;

typedef enum
{
    ANEDOTA_DIFF_ADDED = 1,
    ANEDOTA_DIFF_REMOVED = 2,
    ANEDOTA_DIFF_MODIFIED = 3
} anedota_diff_kind_t;

typedef struct
{
    const char *path;
    anedota_diff_kind_t kind;
    anedota_tree_entry_type_t entry_type;
    const char *old_hash;
    const char *new_hash;
} anedota_diff_entry_t;

typedef struct
{
    size_t count;
    anedota_diff_entry_t entries[];
} anedota_diff_t;

/* Compares two versions (Commit hashes or Tree hashes) stored in root_path.
 * Traverses Trees recursively via the PAL and classifies added, removed, and
 * modified contents in deterministic path order.
 * On success returns ANEDOTA_DIFF_OK (0) and sets *diff_out to a single
 * allocation (with count == 0 when both versions are equal). Release with free().
 * On error returns a negative anedota_diff_status_t without changing *diff_out. */
int anedota_diff_versions(const char *root_path,
                          const char *from_version,
                          const char *to_version,
                          anedota_diff_t **diff_out);

/* Returns a human-readable description for an anedota_diff_status_t code. */
const char *anedota_diff_status_message(int status);

#ifdef __cplusplus
}
#endif

#endif
