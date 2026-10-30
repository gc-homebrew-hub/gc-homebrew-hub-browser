/* Runs net.c's HTTPS client on the host against real servers, with mbedTLS
 * debug output, to tell TLS problems in our code from emulator problems.
 *   nettest [-v level] [-ca cacert.pem] url... */
#include "net.h"
#include <mbedtls/error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int hb_tls_debug_level;

static char *read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t) n + 1);
    if (b && fread(b, 1, (size_t) n, f) != (size_t) n) { free(b); b = NULL; }
    if (b) { b[n] = 0; *len = (size_t) n + 1; }
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    char err[128];
    if (!net_start(err, sizeof err)) { printf("net_start: %s\n", err); return 1; }
    int failed = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v") && i + 1 < argc) { hb_tls_debug_level = atoi(argv[++i]); continue; }
        if (!strcmp(argv[i], "-ca") && i + 1 < argc) {
            size_t len = 0;
            char *pem = read_all(argv[++i], &len);
            bool ok = pem && net_set_ca(pem, len, err, sizeof err);
            printf("CA bundle %s: %s\n", argv[i], ok ? "loaded" : pem ? err : "not readable");
            free(pem);
            continue;
        }
        NetReq r;
        memset(&r, 0, sizeof r);
        r.url = argv[i];
        r.max_mem = 1 << 20;
        bool ok = net_get(&r);
        printf("== %s\n   %s  status %d  %zu bytes  verified %d  %s\n", argv[i], ok ? "OK" : "FAILED",
               r.status, r.len, r.verified, r.err);
        if (!ok && r.status == 0) failed++;   /* an HTTP error status still means TLS worked */
        free(r.mem);
    }
    return failed ? 1 : 0;
}
