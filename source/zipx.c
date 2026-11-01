#include "zipx.h"
#include "fsutil.h"
#include "log.h"
#include "memstat.h"
#include "tarxz.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zip.h>

#define BUF_SIZE 65536
#define ZIP_RESERVE ((size_t)2 << 20)    /* left free beside a zip read whole */

/* Opens a zip. When it fits in memory it is read whole and opened from there:
 * libzip's file source goes through open, fdopen, stat and fseeko, and on the
 * console that failed with "Internal error" for every zip. The file source
 * stays as the way for a zip too big to hold. */
static zip_t *open_zip(const char *path, char *err, size_t errlen) {
    long long size = fs_file_size(path);
    if (size > 0 && (size_t)size + ZIP_RESERVE <= mem_largest_free()) {
        char *data; size_t len;
        if (fs_read_file(path, &data, &len, (size_t)size + 1)) {
            zip_error_t e; zip_error_init(&e);
            zip_source_t *src = zip_source_buffer_create(data, len, 1, &e);
            zip_t *z = NULL;
            if (!src) free(data);
            else if (!(z = zip_open_from_source(src, ZIP_RDONLY, &e))) zip_source_free(src);
            if (z) { zip_error_fini(&e); return z; }
            log_printf("Zip %s from memory: %s (libzip %d, system %d)", path, zip_error_strerror(&e),
                       zip_error_code_zip(&e), zip_error_code_system(&e));
            snprintf(err, errlen, "Cannot open the zip: %s", zip_error_strerror(&e));
            zip_error_fini(&e);
            return NULL;
        }
        log_printf("Zip %s: could not read it into memory", path);
    }
    int zerr = 0;
    errno = 0;
    zip_t *z = zip_open(path, ZIP_RDONLY, &zerr);
    if (!z) {
        int sys = errno;
        zip_error_t e; zip_error_init_with_code(&e, zerr);
        log_printf("Zip %s from the card: %s (libzip %d, errno %d)", path, zip_error_strerror(&e), zerr, sys);
        snprintf(err, errlen, "Cannot open the zip: %s", zip_error_strerror(&e));
        zip_error_fini(&e);
    }
    return z;
}

static bool unzip_to_dir(const char *zip_path, const char *dest, const char *const *only, volatile int *cancel,
                         volatile size_t *done, volatile size_t *total, char *err, size_t errlen) {
    zip_t *z = open_zip(zip_path, err, errlen);
    if (!z) return false;
    zip_int64_t n = zip_get_num_entries(z, 0);
    if (n < 0 || n > 4096) { snprintf(err, errlen, "The zip has too many files"); zip_discard(z); return false; }
    if (total) *total = (size_t)n;
    char *buf = malloc(BUF_SIZE);
    if (!buf) { snprintf(err, errlen, "Out of memory"); zip_discard(z); return false; }
    bool ok = fs_mkdirs(dest);
    if (!ok) snprintf(err, errlen, "Cannot create %s", dest);
    for (zip_int64_t i = 0; ok && i < n; i++) {
        if (cancel && *cancel) { snprintf(err, errlen, "Cancelled"); ok = false; break; }
        zip_stat_t st;
        if (zip_stat_index(z, (zip_uint64_t)i, 0, &st) != 0 || !st.name) { snprintf(err, errlen, "Bad zip entry"); ok = false; break; }
        /* tolerate backslashes and a leading "./" from odd zip tools */
        char name[300];
        snprintf(name, sizeof name, "%s", st.name);
        for (char *p = name; *p; p++) if (*p == '\\') *p = '/';
        const char *rel = name;
        while (!strncmp(rel, "./", 2)) rel += 2;
        size_t rl = strlen(rel);
        if (rl == 0) continue;
        bool is_dir = rel[rl - 1] == '/';
        char clean[300];
        snprintf(clean, sizeof clean, "%.*s", (int)(is_dir ? rl - 1 : rl), rel);
        if (!*clean) continue;
        if (!fs_safe_relative(clean)) { snprintf(err, errlen, "The zip contains an unsafe path"); ok = false; break; }
        if (!fs_path_wanted(clean, only)) continue;
        char path[512];
        if (!fs_join(path, sizeof path, dest, clean)) { snprintf(err, errlen, "A path in the zip is too long"); ok = false; break; }
        if (is_dir) { ok = fs_mkdirs(path); if (!ok) snprintf(err, errlen, "Cannot create a folder on the card"); continue; }
        /* make sure the parent exists even when the zip has no folder entries */
        char *slash = strrchr(path, '/');
        if (slash && slash > path) { *slash = 0; ok = fs_mkdirs(path); *slash = '/'; if (!ok) { snprintf(err, errlen, "Cannot create a folder on the card"); break; } }
        zip_file_t *f = zip_fopen_index(z, (zip_uint64_t)i, 0);
        if (!f) { snprintf(err, errlen, "Cannot read %s from the zip", clean); ok = false; break; }
        FILE *out = fopen(path, "wb");
        if (!out) { zip_fclose(f); snprintf(err, errlen, "Cannot write %s", clean); ok = false; break; }
        zip_int64_t got;
        while ((got = zip_fread(f, buf, BUF_SIZE)) > 0) {
            if (fwrite(buf, 1, (size_t)got, out) != (size_t)got) { got = -1; break; }
            if (cancel && *cancel) { got = -2; break; }
        }
        zip_fclose(f);
        if (fclose(out) != 0 && got >= 0) got = -1;
        if (got < 0) {
            remove(path);
            snprintf(err, errlen, got == -2 ? "Cancelled" : "Cannot unpack %s (card full or bad zip)", clean);
            ok = false; break;
        }
        if (done) *done = (size_t)i + 1;
    }
    free(buf);
    zip_discard(z);
    return ok;
}

bool unpack_to_dir(const char *path, const char *dest, const char *const *only, volatile int *cancel,
                   volatile size_t *done, volatile size_t *total, char *err, size_t errlen) {
    unsigned char magic[6] = { 0 };
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errlen, "Cannot open the download"); return false; }
    size_t n = fread(magic, 1, sizeof magic, f);
    fclose(f);
    if (n >= 4 && !memcmp(magic, "PK\3\4", 4))
        return unzip_to_dir(path, dest, only, cancel, done, total, err, errlen);
    if (n == 6 && !memcmp(magic, "\xfd" "7zXZ\0", 6))
        return untarxz_to_dir(path, dest, only, cancel, done, total, err, errlen);
    snprintf(err, errlen, "The download is not a zip or .tar.xz");
    return false;
}

bool unzip_file(const char *zip_path, const char *member, const char *out_path, char *err, size_t errlen) {
    char why[96];
    zip_t *z = open_zip(zip_path, why, sizeof why);
    if (!z) { snprintf(err, errlen, "cannot open the inner zip"); return false; }
    zip_int64_t i = zip_name_locate(z, member, 0);
    zip_file_t *f = i >= 0 ? zip_fopen_index(z, (zip_uint64_t)i, 0) : NULL;
    if (!f) { snprintf(err, errlen, "%.60s is not in the inner zip", member); zip_discard(z); return false; }
    FILE *out = fopen(out_path, "wb");
    char *buf = malloc(BUF_SIZE);
    zip_int64_t got = out && buf ? 0 : -1;
    while (got >= 0 && (got = zip_fread(f, buf, BUF_SIZE)) > 0)
        if (fwrite(buf, 1, (size_t)got, out) != (size_t)got) got = -1;
    free(buf);
    zip_fclose(f);
    zip_discard(z);
    if (out && fclose(out) != 0) got = -1;
    if (got < 0) { remove(out_path); snprintf(err, errlen, "cannot unpack %.60s (card full?)", member); return false; }
    return true;
}
