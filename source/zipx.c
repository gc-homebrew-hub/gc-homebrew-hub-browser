#include "zipx.h"
#include "fsutil.h"
#include "log.h"
#include "tarxz.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* A zip reader of its own on zlib: libzip 1.10.1 as built for the console
 * failed with "Internal error" on every zip, from the card and from memory
 * alike, while the same zips open everywhere else. Stored and deflated
 * entries, as every release zip uses; no zip64, encryption or other methods.
 * Each file's size and CRC-32 are checked. */

#define IN_SIZE   32768
#define OUT_SIZE  65536
#define MAX_ENTRIES 4096
#define MAX_CDIR  ((size_t)1 << 20)

typedef struct {
    FILE *f;
    long size;
    unsigned char *cd;           /* the central directory, read whole */
    size_t cd_len;
    unsigned count;
} Zip;

typedef struct {
    const char *name;            /* not nul-terminated: name_len bytes */
    unsigned name_len;
    unsigned flags, method;
    uint32_t crc, csize, usize, offset;
} Entry;

static unsigned get16(const unsigned char *p) { return p[0] | p[1] << 8; }
static uint32_t get32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static void zip_close(Zip *z) {
    if (z->f) fclose(z->f);
    free(z->cd);
    memset(z, 0, sizeof *z);
}

/* Finds the end record in the last 64 KB and reads the directory it points to. */
static bool zip_load(Zip *z, const char *path, char *err, size_t errlen) {
    memset(z, 0, sizeof *z);
    z->f = fopen(path, "rb");
    if (!z->f) { snprintf(err, errlen, "Cannot open the zip"); return false; }
    if (fseek(z->f, 0, SEEK_END) != 0 || (z->size = ftell(z->f)) < 22) {
        snprintf(err, errlen, "The download is not a zip");
        zip_close(z); return false;
    }
    long tail = z->size < 65535 + 22 ? z->size : 65535 + 22;
    unsigned char *buf = malloc((size_t)tail);
    if (!buf) { snprintf(err, errlen, "Out of memory"); zip_close(z); return false; }
    if (fseek(z->f, z->size - tail, SEEK_SET) != 0 || fread(buf, 1, (size_t)tail, z->f) != (size_t)tail) {
        free(buf); snprintf(err, errlen, "Cannot read the zip"); zip_close(z); return false;
    }
    const unsigned char *e = NULL;
    for (long i = tail - 22; i >= 0; i--)
        if (get32(buf + i) == 0x06054b50 && i + 22 + (long)get16(buf + i + 20) <= tail) { e = buf + i; break; }
    if (!e) { free(buf); snprintf(err, errlen, "The download is not a zip"); zip_close(z); return false; }
    unsigned count = get16(e + 10);
    uint32_t cd_size = get32(e + 12), cd_off = get32(e + 16);
    bool split = get16(e + 4) != 0 || get16(e + 6) != 0 || get16(e + 8) != count;
    free(buf);
    if (count == 0xffff || cd_size == 0xffffffff || cd_off == 0xffffffff) {
        snprintf(err, errlen, "Zip64 archives are not supported"); zip_close(z); return false;
    }
    if (split) { snprintf(err, errlen, "Split zips are not supported"); zip_close(z); return false; }
    if (count > MAX_ENTRIES) { snprintf(err, errlen, "The zip has too many files"); zip_close(z); return false; }
    if (cd_size > MAX_CDIR || (long)cd_off + (long)cd_size > z->size) {
        snprintf(err, errlen, "The zip is damaged"); zip_close(z); return false;
    }
    z->cd = malloc(cd_size ? cd_size : 1);
    if (!z->cd) { snprintf(err, errlen, "Out of memory"); zip_close(z); return false; }
    if (fseek(z->f, (long)cd_off, SEEK_SET) != 0 || fread(z->cd, 1, cd_size, z->f) != cd_size) {
        snprintf(err, errlen, "Cannot read the zip"); zip_close(z); return false;
    }
    z->cd_len = cd_size;
    z->count = count;
    return true;
}

/* The directory entry at *pos; moves *pos past it. */
static bool zip_entry(const Zip *z, size_t *pos, Entry *en, char *err, size_t errlen) {
    const unsigned char *p = z->cd + *pos;
    if (*pos + 46 > z->cd_len || get32(p) != 0x02014b50) { snprintf(err, errlen, "The zip is damaged"); return false; }
    size_t len = 46 + (size_t)get16(p + 28) + get16(p + 30) + get16(p + 32);
    if (*pos + len > z->cd_len) { snprintf(err, errlen, "The zip is damaged"); return false; }
    en->flags = get16(p + 8);
    en->method = get16(p + 10);
    en->crc = get32(p + 16);
    en->csize = get32(p + 20);
    en->usize = get32(p + 24);
    en->name_len = get16(p + 28);
    en->offset = get32(p + 42);
    en->name = (const char *)p + 46;
    *pos += len;
    if (en->csize == 0xffffffff || en->usize == 0xffffffff || en->offset == 0xffffffff) {
        snprintf(err, errlen, "Zip64 archives are not supported"); return false;
    }
    return true;
}

/* Writes the entry's contents to out_path, checking its size and CRC-32. */
static bool zip_extract(Zip *z, const Entry *en, const char *out_path, volatile int *cancel, char *err, size_t errlen) {
    if (en->flags & 1) { snprintf(err, errlen, "The zip is encrypted"); return false; }
    if (en->method != 0 && en->method != 8) { snprintf(err, errlen, "The zip uses an unsupported compression method"); return false; }
    unsigned char lh[30];
    if (fseek(z->f, (long)en->offset, SEEK_SET) != 0 || fread(lh, 1, 30, z->f) != 30 || get32(lh) != 0x04034b50) {
        snprintf(err, errlen, "The zip is damaged"); return false;
    }
    long data = (long)en->offset + 30 + get16(lh + 26) + get16(lh + 28);
    if (data + (long)en->csize > z->size || fseek(z->f, data, SEEK_SET) != 0) {
        snprintf(err, errlen, "The zip is damaged"); return false;
    }
    FILE *out = fopen(out_path, "wb");
    unsigned char *in = malloc(IN_SIZE), *ob = malloc(OUT_SIZE);
    z_stream s;
    memset(&s, 0, sizeof s);
    bool inflating = false, ok = out && in && ob;
    if (!ok) snprintf(err, errlen, out ? "Out of memory" : "Cannot write a file on the card");
    if (ok && en->method == 8) {
        inflating = inflateInit2(&s, -MAX_WBITS) == Z_OK;
        if (!inflating) { snprintf(err, errlen, "Out of memory"); ok = false; }
    }
    uint32_t left = en->csize, crc = (uint32_t)crc32(0, Z_NULL, 0);
    unsigned long long written = 0;
    int zr = Z_OK;
    while (ok && left > 0 && zr != Z_STREAM_END) {
        if (cancel && *cancel) { snprintf(err, errlen, "Cancelled"); ok = false; break; }
        size_t want = left < IN_SIZE ? left : IN_SIZE;
        if (fread(in, 1, want, z->f) != want) { snprintf(err, errlen, "Cannot read the zip"); ok = false; break; }
        left -= (uint32_t)want;
        if (en->method == 0) {
            crc = (uint32_t)crc32(crc, in, (uInt)want);
            written += want;
            if (fwrite(in, 1, want, out) != want) { snprintf(err, errlen, "Cannot write a file (card full?)"); ok = false; }
            continue;
        }
        s.next_in = in;
        s.avail_in = (uInt)want;
        do {
            s.next_out = ob;
            s.avail_out = OUT_SIZE;
            zr = inflate(&s, Z_NO_FLUSH);
            if (zr != Z_OK && zr != Z_STREAM_END && zr != Z_BUF_ERROR) { snprintf(err, errlen, "The zip is damaged"); ok = false; break; }
            size_t n = OUT_SIZE - s.avail_out;
            crc = (uint32_t)crc32(crc, ob, (uInt)n);
            written += n;
            if (n && fwrite(ob, 1, n, out) != n) { snprintf(err, errlen, "Cannot write a file (card full?)"); ok = false; break; }
        } while (zr != Z_STREAM_END && s.avail_out == 0);
    }
    if (ok && en->method == 8 && zr != Z_STREAM_END) { snprintf(err, errlen, "The zip is damaged"); ok = false; }
    if (ok && (written != en->usize || crc != en->crc)) { snprintf(err, errlen, "The zip is damaged (a file does not check out)"); ok = false; }
    if (inflating) inflateEnd(&s);
    free(in); free(ob);
    if (out && fclose(out) != 0 && ok) { snprintf(err, errlen, "Cannot write a file (card full?)"); ok = false; }
    if (!ok && out) remove(out_path);
    return ok;
}

static bool unzip_to_dir(const char *zip_path, const char *dest, const char *const *only, volatile int *cancel,
                         volatile size_t *done, volatile size_t *total, char *err, size_t errlen) {
    Zip z;
    if (!zip_load(&z, zip_path, err, errlen)) {
        log_printf("Zip %s: %s", zip_path, err);
        return false;
    }
    if (total) *total = z.count;
    bool ok = fs_mkdirs(dest);
    if (!ok) snprintf(err, errlen, "Cannot create %s", dest);
    size_t pos = 0;
    for (unsigned i = 0; ok && i < z.count; i++) {
        if (cancel && *cancel) { snprintf(err, errlen, "Cancelled"); ok = false; break; }
        Entry en;
        if (!zip_entry(&z, &pos, &en, err, errlen)) { ok = false; break; }
        /* tolerate backslashes and a leading "./" from odd zip tools */
        char name[300];
        if (en.name_len >= sizeof name) { snprintf(err, errlen, "A path in the zip is too long"); ok = false; break; }
        memcpy(name, en.name, en.name_len);
        name[en.name_len] = 0;
        if (strlen(name) != en.name_len) { snprintf(err, errlen, "The zip contains an unsafe path"); ok = false; break; }
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
        if (!zip_extract(&z, &en, path, cancel, err, errlen)) {
            log_printf("Zip %s, %s: %s", zip_path, clean, err);
            ok = false; break;
        }
        if (done) *done = (size_t)i + 1;
    }
    zip_close(&z);
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
    Zip z;
    char why[96];
    if (!zip_load(&z, zip_path, why, sizeof why)) {
        log_printf("Zip %s: %s", zip_path, why);
        snprintf(err, errlen, "cannot open the inner zip");
        return false;
    }
    size_t pos = 0, ml = strlen(member);
    bool found = false, ok = false;
    for (unsigned i = 0; i < z.count; i++) {
        Entry en;
        if (!zip_entry(&z, &pos, &en, why, sizeof why)) break;
        if (en.name_len != ml || memcmp(en.name, member, ml)) continue;
        found = true;
        ok = zip_extract(&z, &en, out_path, NULL, why, sizeof why);
        if (!ok) snprintf(err, errlen, "cannot unpack %.60s: %.40s", member, why);
        break;
    }
    if (!found) snprintf(err, errlen, "%.60s is not in the inner zip", member);
    zip_close(&z);
    return ok;
}
