/* fsutil.h - small file helpers on top of stdio/dirent (libfat on the console). */
#pragma once
#include <stdbool.h>
#include <stddef.h>

bool fs_exists(const char *path);
bool fs_is_dir(const char *path);
long long fs_file_size(const char *path);          /* -1 when missing */
bool fs_mkdirs(const char *path);                   /* creates every missing parent */
bool fs_read_file(const char *path, char **out, size_t *len, size_t max);   /* malloc'd, nul-terminated */
bool fs_write_file(const char *path, const void *data, size_t len);         /* via .tmp + rename */
bool fs_remove_tree(const char *path);              /* file or directory, recursively */
bool fs_copy_file(const char *src, const char *dst);
/* Copies a tree; existing files at the destination are left alone when
 * `keep_existing` is set. */
bool fs_copy_tree(const char *src, const char *dst, bool keep_existing);
bool fs_free_space(const char *path, unsigned long long *bytes);
bool fs_files_equal(const char *a, const char *b);  /* same bytes; false if either is missing */
/* Puts a copy of src at target and moves the file that was there into
 * archive_dir (archive_dir-2, -3, ... when that already holds one of the same
 * name). The copy is written beside target and checked first, so target is
 * only ever missing between two renames; if the second fails, the old file
 * goes back. Writes where the old file went to moved_to. */
bool fs_replace_archiving(const char *src, const char *target, const char *archive_dir,
                          char *moved_to, size_t movedcap, char *err, size_t errlen);
/* True for a relative path that stays inside its folder: no leading '/', no
 * drive, no "..", no control characters. */
bool fs_safe_relative(const char *rel);
/* Joins dir and rel into out (always nul-terminated); false when too long. */
bool fs_join(char *out, size_t cap, const char *dir, const char *rel);
