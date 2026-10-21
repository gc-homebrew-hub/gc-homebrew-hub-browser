/* zipx.h - unpacks a zip file into a folder (libzip + zlib underneath). */
#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Entries with unsafe names (absolute, "..", drive letters) are refused.
 * done/total count entries. */
bool unzip_to_dir(const char *zip_path, const char *dest_dir, volatile int *cancel,
                  volatile size_t *done, volatile size_t *total, char *err, size_t errlen);
