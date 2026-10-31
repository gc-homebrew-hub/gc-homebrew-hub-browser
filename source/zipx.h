/* zipx.h - unpacks a downloaded archive into a folder: a zip (libzip + zlib)
 * or a .tar.xz (tarxz.c), told apart by their first bytes. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Writes only the entries inside `only` (a folder in the archive; "" or NULL
 * for all). Entries with unsafe names (absolute, "..", drive letters) are
 * refused. done/total count entries for a zip, bytes read for a .tar.xz. */
bool unpack_to_dir(const char *path, const char *dest, const char *only, volatile int *cancel,
                   volatile size_t *done, volatile size_t *total, char *err, size_t errlen);
