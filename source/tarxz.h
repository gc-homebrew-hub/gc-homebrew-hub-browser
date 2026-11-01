/* tarxz.h - unpacks a .tar.xz into a folder (liblzma underneath). */
#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Writes only the entries inside the folders or files listed in `only`
 * (NULL-terminated; NULL for all), so a large archive costs card space only
 * for what is installed.
 * Regular files and folders are written; links and devices are skipped.
 * Entries with unsafe names (absolute, "..", drive letters) are refused.
 * done/total count bytes of the archive read. */
bool untarxz_to_dir(const char *path, const char *dest, const char *const *only, volatile int *cancel,
                    volatile size_t *done, volatile size_t *total, char *err, size_t errlen);
