#ifndef ANEDOTA_TREE_H
#define ANEDOTA_TREE_H

#include <anedota/hash.h>
#include <stddef.h>

typedef enum
{
    ANEDOTA_TREE_ENTRY_BLOB = 1,
    ANEDOTA_TREE_ENTRY_TREE = 2
} anedota_tree_entry_type_t;

typedef struct
{
    const char *name;
    anedota_tree_entry_type_t type;
    const char *hash;
} anedota_tree_entry_t;

typedef struct
{
    size_t count;
    anedota_tree_entry_t entries[];
} anedota_tree_t;

/* Stores a Tree object from in-memory entries. Child objects must already exist
 * in root_path/objetos. Entries are sorted by name before serialization.
 * On success writes the Tree's SHA-256 to hash_out and returns 0; on error
 * returns -1 without changing hash_out. */
int anedota_tree_write(const char *root_path,
                       const anedota_tree_entry_t *entries, size_t count,
                       char hash_out[ANEDOTA_SHA256_HEX_SIZE]);

/* Reads and validates the Tree identified by hash. On success, *tree_out points
 * to one allocation containing the entries and their strings; release it with
 * free(*tree_out). On error returns -1 without changing *tree_out. */
int anedota_tree_read(const char *root_path, const char *hash,
                      anedota_tree_t **tree_out);

#endif
