/* net.h - network bring-up and a small HTTP/HTTPS client (blocking; call it
 * from the worker thread only). Progress fields may be read by the UI thread. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* Bring up the adapter with DHCP and set the clock from SNTP (best effort).
 * Blocks for up to ~20 s. On failure fills err and returns false. */
bool net_start(char *err, size_t errlen);
bool net_is_up(void);
const char *net_ip_string(void);          /* "" until up */
time_t net_time(void);                    /* current time, corrected by SNTP when it worked */

/* Install the CA bundle (PEM text, nul-terminated). Without it HTTPS still
 * works but servers are not verified (net_get then sets verified = false). */
bool net_set_ca(const char *pem, size_t len, char *err, size_t errlen);

typedef struct {
    /* in */
    const char *url;          /* http:// or https:// */
    const char *path;         /* write the body to this file ... */
    size_t max_mem;           /* ... or, when path is NULL, keep at most this many bytes in memory */
    volatile int *cancel;     /* optional; set nonzero to abort */
    /* progress, written while the body arrives */
    volatile size_t received;
    volatile size_t total;    /* 0 when the server did not say */
    /* out */
    char *mem;                /* malloc'd, nul-terminated body (path == NULL); caller frees */
    size_t len;
    int status;               /* last HTTP status, 0 when the request never got that far */
    bool verified;            /* certificate chain checked against the CA bundle */
    char err[128];
} NetReq;

/* Blocking GET. Follows up to 5 redirects (http<->https). Returns true on a
 * 2xx response; otherwise err describes what happened. */
bool net_get(NetReq *r);
