/* zipx.h - unpacks a downloaded archive into a folder: a zip (zipx.c, on zlib)
 * or a .tar.xz (tarxz.c), told apart by their first bytes. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Writes only the entries inside the folders or files listed in `only`
 * (NULL-terminated; NULL for all). Entries with unsafe names (absolute, "..",
 * drive letters) are refused. done/total count entries for a zip, bytes read
 * for a .tar.xz. */
bool unpack_to_dir(const char *path, const char *dest, const char *const *only, volatile int *cancel,
                   volatile size_t *done, volatile size_t *total, char *err, size_t errlen);
/* Copies one file out of a zip (a zip inside a downloaded archive, such as
 * Swiss's EXTRACT_TO_ROOT.zip) to out_path. */
bool unzip_file(const char *zip_path, const char *member, const char *out_path, char *err, size_t errlen);
