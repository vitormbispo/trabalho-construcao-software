#ifndef ANEDOTA_H
#define ANEDOTA_H

/* Consolidated public API header for libanedota.
 * Exposes all versioning operations decoupled from the CLI. */

#define ANEDOTA_VERSION_MAJOR 0
#define ANEDOTA_VERSION_MINOR 1
#define ANEDOTA_VERSION_PATCH 0
#define ANEDOTA_VERSION_STRING "0.1.0"

#include <anedota/hash.h>
#include <anedota/storage.h>
#include <anedota/tree.h>
#include <anedota/commit.h>
#include <anedota/ref.h>
#include <anedota/log.h>
#include <anedota/diff.h>

#endif
