/* dol.c - the DOL file format and how we hand the console over to another
 * program. The file is read whole into memory, moved to the top of RAM (where
 * programs practically never load), the system is shut down, and a tiny stub
 * in low memory copies the sections into place and jumps. devkitPPC programs
 * get argv[0] = their own path, so they can find their files. */
#include "dol.h"
#include "fsutil.h"
#include <fat.h>
#include <gccore.h>
#include <malloc.h>
#include <ogc/machine/processor.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    u32 text_off[7], data_off[11];
    u32 text_addr[7], data_addr[11];
    u32 text_size[7], data_size[11];
    u32 bss_addr, bss_size, entry;
    u8 pad[28];
} DolHeader;

#define RAM_LO   0x80003100u
#define RAM_HI   0x81800000u
#define STUB_AT  0x80001800u          /* stub, copy table and command line live here */
#define STUB_MAX 0x1000u
#define MAX_DOL  (12u * 1024 * 1024)

extern const u8 dol_stub_begin[], dol_stub_end[];

static bool header_ok(const DolHeader *h, long long filesize, u32 *lo, u32 *hi, char *err, size_t errlen) {
    *lo = 0xffffffffu; *hi = 0;
    int nsec = 0;
    for (int i = 0; i < 18; i++) {
        u32 off = i < 7 ? h->text_off[i] : h->data_off[i - 7];
        u32 addr = i < 7 ? h->text_addr[i] : h->data_addr[i - 7];
        u32 size = i < 7 ? h->text_size[i] : h->data_size[i - 7];
        if (!size) continue;
        if (addr < 0x80000000u || addr >= RAM_HI || size > RAM_HI - addr || (unsigned long long)off + size > (unsigned long long)filesize) {
            snprintf(err, errlen, "The program's sections do not fit in memory");
            return false;
        }
        if (addr < *lo) *lo = addr;
        if (addr + size > *hi) *hi = addr + size;
        nsec++;
    }
    if (!nsec) { snprintf(err, errlen, "Not a program file (.dol)"); return false; }
    if (h->bss_size) {
        if (h->bss_addr < 0x80000000u || h->bss_addr >= RAM_HI || h->bss_size > RAM_HI - h->bss_addr) {
            snprintf(err, errlen, "The program's data does not fit in memory");
            return false;
        }
        if (h->bss_addr + h->bss_size > *hi) *hi = h->bss_addr + h->bss_size;
    }
    if (h->entry < RAM_LO || h->entry >= RAM_HI) { snprintf(err, errlen, "Not a program file (.dol)"); return false; }
    if (*lo < STUB_AT + STUB_MAX && *hi > STUB_AT) { snprintf(err, errlen, "This program loads too low in memory to start from here"); return false; }
    return true;
}

bool dol_check(const char *path, char *err, size_t errlen) {
    long long size = fs_file_size(path);
    if (size < (long long)sizeof(DolHeader)) { snprintf(err, errlen, "The program file is missing or empty"); return false; }
    if (size > MAX_DOL) { snprintf(err, errlen, "The program is too large to start from here (over 12 MB)"); return false; }
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errlen, "Cannot open the program file"); return false; }
    DolHeader h;
    size_t got = fread(&h, 1, sizeof h, f);
    fclose(f);
    if (got != sizeof h) { snprintf(err, errlen, "Cannot read the program file"); return false; }
    u32 lo, hi;
    return header_ok(&h, size, &lo, &hi, err, errlen);
}

bool dol_launch(const char *path, char *err, size_t errlen) {
    if (!dol_check(path, err, errlen)) return false;
    long long size = fs_file_size(path);
    u8 *buf = memalign(32, (size_t)size + 32);
    if (!buf) { snprintf(err, errlen, "Not enough memory to load the program"); return false; }
    FILE *f = fopen(path, "rb");
    if (!f || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        if (f) fclose(f);
        free(buf);
        snprintf(err, errlen, "Cannot read the program file");
        return false;
    }
    fclose(f);
    DolHeader *h = (DolHeader *)buf;
    u32 lo, hi;
    if (!header_ok(h, size, &lo, &hi, err, errlen)) { free(buf); return false; }

    /* where the file image will sit while the stub copies from it */
    u32 image = (RAM_HI - (u32)size) & ~31u;
    if (image < hi || image < RAM_LO) {
        free(buf);
        snprintf(err, errlen, "This program uses the top of memory; start it from Swiss instead");
        return false;
    }

    /* argv for devkitPPC programs: the six-word __argv block sits at entry+8
     * and is all zero in the file; the loader fills it in. */
    char cmdline[300];
    int cmdlen = snprintf(cmdline, sizeof cmdline, "%s", path) + 1;
    u32 cmd_at = STUB_AT + 0x800;
    for (int i = 0; i < 18; i++) {
        u32 off = i < 7 ? h->text_off[i] : h->data_off[i - 7];
        u32 addr = i < 7 ? h->text_addr[i] : h->data_addr[i - 7];
        u32 sz = i < 7 ? h->text_size[i] : h->data_size[i - 7];
        if (!sz || h->entry + 8 < addr || h->entry + 32 > addr + sz) continue;
        u32 *argv = (u32 *)(buf + off + (h->entry + 8 - addr));
        bool zero = true;
        for (int k = 0; k < 6; k++) if (argv[k]) zero = false;
        if (zero) {
            argv[0] = ARGV_MAGIC;
            argv[1] = cmd_at;
            argv[2] = (u32)cmdlen;
        }
        break;
    }

    /* build the copy table */
    u32 table[4 + 18 * 3];
    int n = 0;
    u32 *p = table + 1;
    for (int i = 0; i < 18; i++) {
        u32 off = i < 7 ? h->text_off[i] : h->data_off[i - 7];
        u32 addr = i < 7 ? h->text_addr[i] : h->data_addr[i - 7];
        u32 sz = i < 7 ? h->text_size[i] : h->data_size[i - 7];
        if (!sz) continue;
        *p++ = addr; *p++ = image + off; *p++ = (sz + 3) & ~3u;
        n++;
    }
    table[0] = (u32)n;
    *p++ = h->bss_addr; *p++ = h->bss_size & ~3u; *p++ = h->entry;
    size_t table_bytes = (size_t)((u8 *)p - (u8 *)table);
    size_t stub_bytes = (size_t)(dol_stub_end - dol_stub_begin);

    /* nothing below may touch the card, the network or the OS any more */
    fatUnmount("sd");
    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    u32 level;
    _CPU_ISR_Disable(level);
    (void)level;

    memmove((void *)image, buf, (size_t)size);
    memcpy((void *)STUB_AT, dol_stub_begin, stub_bytes);
    u32 table_at = (STUB_AT + (u32)stub_bytes + 31) & ~31u;
    memcpy((void *)table_at, table, table_bytes);
    memcpy((void *)cmd_at, cmdline, (size_t)cmdlen);
    DCFlushRange((void *)image, (u32)size);
    DCFlushRange((void *)STUB_AT, STUB_MAX);
    ICInvalidateRange((void *)STUB_AT, STUB_MAX);
    void (*stub)(u32 *) = (void (*)(u32 *))STUB_AT;
    stub((u32 *)table_at);
    return false;   /* never reached */
}
