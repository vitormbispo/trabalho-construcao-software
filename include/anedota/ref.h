#ifndef ANEDOTA_REF_H
#define ANEDOTA_REF_H

#include <anedota/hash.h>

int anedota_ref_read_head(const char *root_path,
                          char hash_out[ANEDOTA_SHA256_HEX_SIZE]);

int anedota_ref_update_head(const char *root_path, const char *commit_hash);

#endif
