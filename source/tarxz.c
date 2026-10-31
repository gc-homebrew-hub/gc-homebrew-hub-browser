#include "tarxz.h"
#include "fsutil.h"
#include <lzma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_SIZE 32768
/* The decoder needs about the archive's dictionary size: 8.5 MB for
 * Swiss's (xz -6). Anything wanting more would not fit next to the program. */
#define XZ_MEMLIMIT (16u << 20)
#define MAX_ENTRIES 4096

typedef enum { T_HEADER, T_DATA, T_LONGNAME, T_PAX, T_SKIP, T_END } TarState;

typedef struct {
    TarState state;
    unsigned char hdr[512];
    size_t hdr_len;
    unsigned long long left;          /* bytes of the current entry's data still to come */
    size_t pad;                       /* zero bytes after it, up to the next 512 */
    char next_name[300];              /* from a GNU long name or pax header, for the next entry */
    char meta[1024];                  /* the long name or pax header being read */
    size_t meta_len;
    int zero_blocks, entries;
    FILE *out;
    char out_path[512];
    const char *dest, *only;
    char *err; size_t errlen;
} Tar;

static unsigned long long octal(const unsigned char *p, size_t n, bool *ok) {
    unsigned long long v = 0;
    size_t i = 0;
    if (n && (p[0] & 0x80)) { *ok = false; return 0; }      /* base-256: only for huge sizes */
    while (i < n && p[i] == ' ') i++;
    for (; i < n && p[i] >= '0' && p[i] <= '7'; i++) v = v * 8 + (p[i] - '0');
    return v;
}

static bool header_ok(const unsigned char *h) {
    unsigned sum = 0;
    for (int i = 0; i < 512; i++) sum += (i >= 148 && i < 156) ? ' ' : h[i];
    bool ok = true;
    return octal(h + 148, 8, &ok) == sum && ok;
}

/* The path=... record of a pax header, if any */
static void pax_path(const char *rec, size_t len, char *out, size_t cap) {
    size_t i = 0;
    while (i < len) {
        size_t n = 0, j = i;
        while (j < len && rec[j] >= '0' && rec[j] <= '9') n = n * 10 + (size_t)(rec[j++] - '0');
        if (n == 0 || i + n > len) return;
        if (j < len && rec[j] == ' ' && j + 6 < i + n && !strncmp(rec + j + 1, "path=", 5)) {
            size_t vlen = i + n - (j + 6) - 1;                  /* drop the newline */
            snprintf(out, cap, "%.*s", (int)vlen, rec + j + 6);
        }
        i += n;
    }
}

static bool fail(Tar *t, const char *msg, const char *name) {
    snprintf(t->err, t->errlen, msg, name);
    return false;
}

/* A header is complete: set up for its data. */
static bool tar_entry(Tar *t) {
    const unsigned char *h = t->hdr;
    bool ok = true;
    unsigned long long size = octal(h + 124, 12, &ok);
    if (!ok) return fail(t, "A file in the archive is too large", "");
    char type = (char)h[156];
    t->left = size;
    t->pad = (size_t)((512 - size % 512) % 512);
    if (type == 'L' || type == 'x') {
        if (size >= sizeof t->meta) return fail(t, "The archive has a name that is too long", "");
        t->meta_len = 0;
        t->state = type == 'L' ? T_LONGNAME : T_PAX;
        return true;
    }
    char name[300];
    if (*t->next_name) {
        snprintf(name, sizeof name, "%s", t->next_name);
        t->next_name[0] = 0;
    } else if (!memcmp(h + 257, "ustar", 5) && h[345])
        snprintf(name, sizeof name, "%.155s/%.100s", (const char *)h + 345, (const char *)h);
    else
        snprintf(name, sizeof name, "%.100s", (const char *)h);
    t->state = T_SKIP;
    if (type != '0' && type != '\0' && type != '7' && type != '5') return true;   /* links, devices, 'g' */

    /* the same clean-up as for zips */
    for (char *p = name; *p; p++) if (*p == '\\') *p = '/';
    const char *rel = name;
    while (!strncmp(rel, "./", 2)) rel += 2;
    size_t rl = strlen(rel);
    while (rl && rel[rl - 1] == '/') rl--;
    if (!rl) return true;
    char clean[300];
    snprintf(clean, sizeof clean, "%.*s", (int)rl, rel);
    if (!fs_safe_relative(clean)) return fail(t, "The archive contains an unsafe path", "");
    if (t->only && *t->only) {
        size_t ol = strlen(t->only);
        if (strncmp(clean, t->only, ol) || (clean[ol] && clean[ol] != '/')) return true;
    }
    if (++t->entries > MAX_ENTRIES) return fail(t, "The archive has too many files", "");
    char path[512];
    if (!fs_join(path, sizeof path, t->dest, clean)) return fail(t, "A path in the archive is too long", "");
    if (type == '5') return fs_mkdirs(path) || fail(t, "Cannot create a folder on the card", "");
    char *slash = strrchr(path, '/');
    if (slash && slash > path) {
        *slash = 0; ok = fs_mkdirs(path); *slash = '/';
        if (!ok) return fail(t, "Cannot create a folder on the card", "");
    }
    t->out = fopen(path, "wb");
    if (!t->out) return fail(t, "Cannot write %s", clean);
    snprintf(t->out_path, sizeof t->out_path, "%s", path);
    t->state = T_DATA;
    return true;
}

static bool tar_close(Tar *t) {
    bool ok = fclose(t->out) == 0;
    t->out = NULL;
    if (!ok) { remove(t->out_path); return fail(t, "Cannot unpack %s (card full?)", t->out_path); }
    return true;
}

/* Feeds decompressed bytes through the tar reader. */
static bool tar_feed(Tar *t, const unsigned char *p, size_t n) {
    while (n) {
        if (t->state == T_END) return true;               /* trailing zeros after the end blocks */
        if (t->state == T_HEADER) {
            size_t take = 512 - t->hdr_len;
            if (take > n) take = n;
            memcpy(t->hdr + t->hdr_len, p, take);
            t->hdr_len += take; p += take; n -= take;
            if (t->hdr_len < 512) return true;
            t->hdr_len = 0;
            bool zero = true;
            for (int i = 0; i < 512 && zero; i++) zero = t->hdr[i] == 0;
            if (zero) { if (++t->zero_blocks == 2) t->state = T_END; continue; }
            t->zero_blocks = 0;
            if (!header_ok(t->hdr)) return fail(t, "Not a valid tar archive", "");
            if (!tar_entry(t)) return false;
            if (t->left == 0 && t->state == T_DATA && !tar_close(t)) return false;
            if (t->left == 0) t->state = T_HEADER;
            continue;
        }
        if (t->left) {
            size_t take = t->left < n ? (size_t)t->left : n;
            if (t->state == T_DATA && fwrite(p, 1, take, t->out) != take) {
                fclose(t->out); t->out = NULL; remove(t->out_path);
                return fail(t, "Cannot unpack %s (card full?)", t->out_path);
            }
            if (t->state == T_LONGNAME || t->state == T_PAX) {
                memcpy(t->meta + t->meta_len, p, take);
                t->meta_len += take;
            }
            t->left -= take; p += take; n -= take;
            if (t->left) continue;
            if (t->state == T_DATA && !tar_close(t)) return false;
            if (t->state == T_LONGNAME) snprintf(t->next_name, sizeof t->next_name, "%.*s", (int)t->meta_len, t->meta);
            if (t->state == T_PAX) pax_path(t->meta, t->meta_len, t->next_name, sizeof t->next_name);
            t->state = T_SKIP;
        }
        size_t take = t->pad < n ? t->pad : n;
        t->pad -= take; p += take; n -= take;
        if (!t->pad) t->state = T_HEADER;
    }
    return true;
}

bool untarxz_to_dir(const char *path, const char *dest, const char *only, volatile int *cancel,
                    volatile size_t *done, volatile size_t *total, char *err, size_t errlen) {
    FILE *in = fopen(path, "rb");
    if (!in) { snprintf(err, errlen, "Cannot open the archive"); return false; }
    long long size = fs_file_size(path);
    if (total) *total = size > 0 ? (size_t)size : 0;
    if (done) *done = 0;
    unsigned char *inbuf = malloc(BUF_SIZE), *outbuf = malloc(BUF_SIZE);
    Tar *t = calloc(1, sizeof *t);
    lzma_stream s = LZMA_STREAM_INIT;
    bool ok = inbuf && outbuf && t;
    if (!ok) snprintf(err, errlen, "Out of memory");
    if (ok && lzma_stream_decoder(&s, XZ_MEMLIMIT, LZMA_CONCATENATED) != LZMA_OK) {
        snprintf(err, errlen, "Out of memory"); ok = false;
    }
    if (ok && !fs_mkdirs(dest)) { snprintf(err, errlen, "Cannot create %s", dest); ok = false; }
    if (ok) {
        t->dest = dest; t->only = only; t->err = err; t->errlen = errlen;
        lzma_action action = LZMA_RUN;
        size_t nread = 0;
        for (;;) {
            if (cancel && *cancel) { snprintf(err, errlen, "Cancelled"); ok = false; break; }
            if (s.avail_in == 0 && action == LZMA_RUN) {
                s.next_in = inbuf;
                s.avail_in = fread(inbuf, 1, BUF_SIZE, in);
                if (ferror(in)) { snprintf(err, errlen, "Cannot read the archive"); ok = false; break; }
                if (feof(in)) action = LZMA_FINISH;
                nread += s.avail_in;
                if (done) *done = nread;
            }
            s.next_out = outbuf;
            s.avail_out = BUF_SIZE;
            lzma_ret r = lzma_code(&s, action);
            if (!tar_feed(t, outbuf, BUF_SIZE - s.avail_out)) { ok = false; break; }
            if (r == LZMA_STREAM_END) break;
            if (r == LZMA_OK) continue;
            if (r == LZMA_MEMLIMIT_ERROR)
                snprintf(err, errlen, "Unpacking needs %llu MB of memory; there is not that much",
                         (unsigned long long)(lzma_memusage(&s) >> 20) + 1);
            else if (r == LZMA_MEM_ERROR)
                snprintf(err, errlen, "Not enough memory to unpack the archive");
            else if (r == LZMA_FORMAT_ERROR)
                snprintf(err, errlen, "Not an xz archive");
            else
                snprintf(err, errlen, "The archive is damaged");
            ok = false; break;
        }
        if (ok && (t->state == T_DATA || t->left || t->hdr_len)) {
            snprintf(err, errlen, "The archive is cut short"); ok = false;
        }
        if (t->out) { fclose(t->out); remove(t->out_path); }
    }
    lzma_end(&s);
    free(t); free(outbuf); free(inbuf);
    fclose(in);
    return ok;
}
