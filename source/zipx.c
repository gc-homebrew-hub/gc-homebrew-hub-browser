#include "zipx.h"
#include "fsutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zip.h>

#define BUF_SIZE 65536

bool unzip_to_dir(const char *zip_path, const char *dest, volatile int *cancel,
                  volatile size_t *done, volatile size_t *total, char *err, size_t errlen) {
    int zerr = 0;
    zip_t *z = zip_open(zip_path, ZIP_RDONLY, &zerr);
    if (!z) {
        zip_error_t e; zip_error_init_with_code(&e, zerr);
        snprintf(err, errlen, "Cannot open the zip: %s", zip_error_strerror(&e));
        zip_error_fini(&e);
        return false;
    }
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
