#include "fsutil.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#ifdef GEKKO
#include <sys/iosupport.h>
#endif

#define MAX_DEPTH 12
#define PATH_CAP 512

bool fs_exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
bool fs_is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
long long fs_file_size(const char *p) { struct stat st; return stat(p, &st) == 0 ? (long long)st.st_size : -1; }

bool fs_join(char *out, size_t cap, const char *dir, const char *rel) {
    size_t dl = strlen(dir);
    bool slash = dl && dir[dl - 1] == '/';
    int n = snprintf(out, cap, "%s%s%s", dir, slash ? "" : "/", rel);
    return n > 0 && (size_t)n < cap;
}

bool fs_mkdirs(const char *path) {
    char buf[PATH_CAP];
    size_t n = strlen(path);
    if (n >= sizeof buf) return false;
#ifdef GEKKO
    /* libogc's mkdir() does not check for an unmounted device and reads
       through a null pointer (no SD card, e.g. Dolphin without one) */
    if (strchr(path, ':') && FindDevice(path) < 0) return false;
#endif
    memcpy(buf, path, n + 1);
    /* skip the "drive:/" prefix */
    char *p = strchr(buf, ':');
    p = p ? p + 1 : buf;
    while (*p == '/') p++;
    for (; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (!fs_is_dir(buf) && mkdir(buf, 0777) != 0 && !fs_is_dir(buf)) return false;
            *p = '/';
        }
    }
    return fs_is_dir(buf) || mkdir(buf, 0777) == 0 || fs_is_dir(buf);
}

bool fs_read_file(const char *path, char **out, size_t *len, size_t max) {
    *out = NULL; if (len) *len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    long long sz = fs_file_size(path);
    if (sz < 0 || (size_t)sz > max) { fclose(f); return false; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return false; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = 0;
    *out = buf; if (len) *len = got;
    return true;
}

bool fs_write_file(const char *path, const void *data, size_t len) {
    char tmp[PATH_CAP];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return false;
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(tmp); return false; }
    remove(path);
    if (rename(tmp, path) != 0) { remove(tmp); return false; }
    return true;
}

static bool remove_dir(const char *path, int depth) {
    if (depth > MAX_DEPTH) return false;
    DIR *d = opendir(path);
    if (!d) return false;
    bool ok = true;
    struct dirent *e;
    char child[PATH_CAP];
    while (ok && (e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!fs_join(child, sizeof child, path, e->d_name)) { ok = false; break; }
        if (fs_is_dir(child)) ok = remove_dir(child, depth + 1);
        else ok = remove(child) == 0;
    }
    closedir(d);
    return ok && rmdir(path) == 0;
}

bool fs_remove_tree(const char *path) {
    if (!fs_exists(path)) return true;
    if (fs_is_dir(path)) return remove_dir(path, 0);
    return remove(path) == 0;
}

bool fs_copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    char *buf = malloc(65536);
    bool ok = buf != NULL;
    size_t n;
    while (ok && (n = fread(buf, 1, 65536, in)) > 0) ok = fwrite(buf, 1, n, out) == n;
    free(buf);
    fclose(in);
    if (fclose(out) != 0) ok = false;
    if (!ok) remove(dst);
    return ok;
}

static bool copy_dir(const char *src, const char *dst, bool keep, int depth) {
    if (depth > MAX_DEPTH) return false;
    if (!fs_mkdirs(dst)) return false;
    DIR *d = opendir(src);
    if (!d) return false;
    bool ok = true;
    struct dirent *e;
    char a[PATH_CAP], b[PATH_CAP];
    while (ok && (e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!fs_join(a, sizeof a, src, e->d_name) || !fs_join(b, sizeof b, dst, e->d_name)) { ok = false; break; }
        if (fs_is_dir(a)) ok = copy_dir(a, b, keep, depth + 1);
        else if (keep && fs_exists(b)) continue;
        else ok = fs_copy_file(a, b);
    }
    closedir(d);
    return ok;
}

bool fs_copy_tree(const char *src, const char *dst, bool keep_existing) {
    if (fs_is_dir(src)) return copy_dir(src, dst, keep_existing, 0);
    if (keep_existing && fs_exists(dst)) return true;
    return fs_copy_file(src, dst);
}

bool fs_free_space(const char *path, unsigned long long *bytes) {
    struct statvfs st;
    if (statvfs(path, &st) != 0) return false;
    *bytes = (unsigned long long)st.f_bsize * st.f_bavail;
    return true;
}

bool fs_safe_relative(const char *rel) {
    if (!rel || !*rel || *rel == '/' || *rel == '\\' || strchr(rel, ':')) return false;
    for (const char *p = rel; *p; p++)
        if ((unsigned char)*p < 32 || *p == '\\') return false;
    const char *p = rel;
    while (*p) {
        const char *seg = p;
        while (*p && *p != '/') p++;
        size_t n = (size_t)(p - seg);
        if (n == 0 || (n == 1 && seg[0] == '.') || (n == 2 && seg[0] == '.' && seg[1] == '.')) return false;
        if (*p == '/') p++;
    }
    return true;
}
