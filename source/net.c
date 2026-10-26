/* net.c - adapter bring-up, SNTP, and a small HTTP/HTTPS GET client on top of
 * libogc2's lwIP sockets and mbedTLS. Everything here runs on the worker
 * thread; the UI thread only reads NetReq progress fields and net_is_up(). */
#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <errno.h>

#include <network.h>
#include <ogc/lwp_watchdog.h>

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/platform.h>
#include <mbedtls/platform_time.h>
#include <mbedtls/net_sockets.h>
#include <psa/crypto.h>

#define READ_TIMEOUT_MS   20000
#define POLL_SLICE_MS     250
#define SNTP_TIMEOUT_MS   3000
#define MAX_REDIRECTS     5
#define MAX_URL           512
#define MAX_HOST          256
#define READ_BUF_SIZE     (32 * 1024)
#define FILE_BUF_SIZE     (64 * 1024)
#define USER_AGENT        "GC-Homebrew-Hub-Browser/0.1"
#define NTP_UNIX_OFFSET   2208988800u

/* ------------------------------------------------------------------------- */
/* State                                                                      */

static bool s_up;
static char s_ip[16];
static time_t s_time_offset;      /* SNTP time minus the console clock */

static bool s_tls_ready;
static mbedtls_entropy_context s_entropy;
static mbedtls_ctr_drbg_context s_drbg;
static bool s_ca_loaded;
static mbedtls_x509_crt s_ca;

static void set_err(char *err, size_t errlen, const char *msg)
{
    if (err && errlen) {
        strncpy(err, msg, errlen - 1);
        err[errlen - 1] = '\0';
    }
}

static void req_err(NetReq *r, const char *msg)
{
    set_err(r->err, sizeof r->err, msg);
}

/* ------------------------------------------------------------------------- */
/* mbedTLS platform hooks                                                     */

/* Entropy source. The GameCube has no RNG, so this is weak entropy: it mixes
 * the time base (which runs at 40.5 MHz and differs on every boot) with the
 * jitter between repeated reads. Good enough for a client that only needs
 * unpredictable handshake nonces, but not for generating long-lived keys. */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    static uint32_t state = 0x9E3779B9u;
    (void) data;

    for (size_t i = 0; i < len; i++) {
        uint32_t mix = gettick();
        /* A few dependent reads so the loop timing itself feeds back. */
        for (int k = 0; k < 4 + (int) (mix & 7); k++) {
            mix = (mix << 5) ^ (mix >> 3) ^ gettick();
        }
        mix ^= (uint32_t) gettime();
        state ^= mix;
        state *= 0x01000193u;
        state ^= state >> 13;
        output[i] = (unsigned char) (state ^ (state >> 24));
    }
    *olen = len;
    return 0;
}

/* Registered through mbedtls_platform_set_time(): wall clock for X.509 dates. */
static mbedtls_time_t hb_time(mbedtls_time_t *t)
{
    mbedtls_time_t now = (mbedtls_time_t) net_time();
    if (t) {
        *t = now;
    }
    return now;
}

/* Monotonic milliseconds; mbedTLS uses this for TLS 1.3 ticket ages. */
mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t) ticks_to_millisecs(gettime());
}

static bool tls_global_init(char *err, size_t errlen)
{
    if (s_tls_ready) {
        return true;
    }
    mbedtls_platform_set_time(hb_time);
    if (psa_crypto_init() != PSA_SUCCESS) {
        set_err(err, errlen, "Crypto init failed");
        return false;
    }
    mbedtls_entropy_init(&s_entropy);
    mbedtls_ctr_drbg_init(&s_drbg);
    const char *pers = "gc-homebrew-hub-browser";
    if (mbedtls_ctr_drbg_seed(&s_drbg, mbedtls_entropy_func, &s_entropy,
                              (const unsigned char *) pers, strlen(pers)) != 0) {
        mbedtls_ctr_drbg_free(&s_drbg);
        mbedtls_entropy_free(&s_entropy);
        set_err(err, errlen, "RNG seed failed");
        return false;
    }
    s_tls_ready = true;
    return true;
}

/* ------------------------------------------------------------------------- */
/* Socket helpers                                                             */

enum { WAIT_READY, WAIT_TIMEOUT, WAIT_CANCELLED, WAIT_ERROR };

/* Blocks until sock is readable, in short select() slices so a cancel
 * request is noticed quickly. (libogc2's GameCube stack has no net_poll.) */
static int wait_readable(s32 sock, volatile int *cancel, u32 timeout_ms)
{
    u64 start = gettime();
    for (;;) {
        if (cancel && *cancel) {
            return WAIT_CANCELLED;
        }
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(sock, &rd);
        struct timeval tv = { .tv_sec = 0, .tv_usec = POLL_SLICE_MS * 1000 };
        s32 n = net_select(sock + 1, &rd, NULL, NULL, &tv);
        if (n > 0) {
            return WAIT_READY;
        }
        if (n < 0) {
            return WAIT_ERROR;
        }
        if (ticks_to_millisecs(diff_ticks(start, gettime())) >= timeout_ms) {
            return WAIT_TIMEOUT;
        }
    }
}

/* One UDP request/response. Returns the reply length or -1. */
static int udp_exchange(uint32_t addr, uint16_t port, const unsigned char *out, size_t outlen,
                        unsigned char *in, size_t inlen, u32 timeout_ms)
{
    struct sockaddr_in to;
    memset(&to, 0, sizeof to);
    to.sin_len = sizeof to;
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = addr;

    s32 sock = net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return -1;
    }
    int got = -1;
    if (net_sendto(sock, out, outlen, 0, (struct sockaddr *) &to, sizeof to) == (s32) outlen &&
        wait_readable(sock, NULL, timeout_ms) == WAIT_READY) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof from;
        s32 n = net_recvfrom(sock, in, inlen, 0, (struct sockaddr *) &from, &fromlen);
        if (n > 0) {
            got = (int) n;
        }
    }
    net_close(sock);
    return got;
}

/* ------------------------------------------------------------------------- */
/* DNS. libogc2's GameCube lwIP has no resolver, so this is a minimal one:
 * A-record queries over UDP, asked of the DHCP gateway first (home routers
 * forward DNS) and then of public resolvers. Results are cached for their
 * TTL, so a catalog fetch and its downloads share one lookup.              */

#define DNS_TIMEOUT_MS  2500
#define DNS_CACHE_SIZE  8
#define DNS_PACKET_MAX  512

typedef struct {
    char host[MAX_HOST];
    uint32_t addr;
    u64 expires;                /* in time base ticks */
} DnsEntry;

static DnsEntry s_dns_cache[DNS_CACHE_SIZE];
static unsigned s_dns_next;
static char s_gateway[16];

static uint16_t be16(const unsigned char *p)
{
    return (uint16_t) ((p[0] << 8) | p[1]);
}

static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

/* Advances *pos past a possibly compressed domain name. */
static bool dns_skip_name(const unsigned char *pkt, size_t len, size_t *pos)
{
    size_t p = *pos;
    for (;;) {
        if (p >= len) {
            return false;
        }
        unsigned char c = pkt[p];
        if (c == 0) {
            p++;
            break;
        }
        if ((c & 0xC0) == 0xC0) {
            p += 2;
            break;
        }
        p += 1 + c;
    }
    if (p > len) {
        return false;
    }
    *pos = p;
    return true;
}

static bool dns_query(uint32_t server, const char *host, uint32_t *addr, uint32_t *ttl)
{
    unsigned char pkt[DNS_PACKET_MAX];
    size_t n = 12;
    uint16_t id = (uint16_t) gettick();

    memset(pkt, 0, 12);
    pkt[0] = (unsigned char) (id >> 8);
    pkt[1] = (unsigned char) id;
    pkt[2] = 0x01;                      /* recursion desired */
    pkt[5] = 1;                         /* one question */
    for (const char *p = host; *p; ) {
        const char *dot = strchr(p, '.');
        size_t len = dot ? (size_t) (dot - p) : strlen(p);
        if (len == 0 || len > 63 || n + len + 6 > sizeof pkt) {
            return false;
        }
        pkt[n++] = (unsigned char) len;
        memcpy(pkt + n, p, len);
        n += len;
        p += len;
        if (*p == '.') {
            p++;
        }
    }
    pkt[n++] = 0;
    pkt[n++] = 0; pkt[n++] = 1;         /* QTYPE A */
    pkt[n++] = 0; pkt[n++] = 1;         /* QCLASS IN */

    int rlen = udp_exchange(server, 53, pkt, n, pkt, sizeof pkt, DNS_TIMEOUT_MS);
    if (rlen < 12 || be16(pkt) != id || !(pkt[2] & 0x80) || (pkt[3] & 0x0F) != 0) {
        return false;
    }
    size_t len = (size_t) rlen, pos = 12;
    unsigned qd = be16(pkt + 4), an = be16(pkt + 6);
    for (unsigned i = 0; i < qd; i++) {
        if (!dns_skip_name(pkt, len, &pos) || pos + 4 > len) {
            return false;
        }
        pos += 4;
    }
    /* Answers may start with CNAMEs; the A record for the target follows. */
    for (unsigned i = 0; i < an; i++) {
        if (!dns_skip_name(pkt, len, &pos) || pos + 10 > len) {
            return false;
        }
        unsigned type = be16(pkt + pos), cls = be16(pkt + pos + 2);
        uint32_t rttl = be32(pkt + pos + 4);
        size_t rdlen = be16(pkt + pos + 8);
        pos += 10;
        if (pos + rdlen > len) {
            return false;
        }
        if (type == 1 && cls == 1 && rdlen == 4) {
            memcpy(addr, pkt + pos, 4);
            *ttl = rttl;
            return true;
        }
        pos += rdlen;
    }
    return false;
}

/* Resolves host (name or dotted quad) to a network-order IPv4 address. */
static bool resolve_host(const char *host, uint32_t *addr)
{
    struct in_addr in;
    if (inet_aton(host, &in)) {
        *addr = in.s_addr;
        return true;
    }
    u64 now = gettime();
    for (unsigned i = 0; i < DNS_CACHE_SIZE; i++) {
        DnsEntry *e = &s_dns_cache[i];
        if (e->host[0] && strcasecmp(e->host, host) == 0 && now < e->expires) {
            *addr = e->addr;
            return true;
        }
    }

    uint32_t servers[3];
    unsigned nservers = 0;
    if (inet_aton(s_gateway, &in) && in.s_addr != 0) {
        servers[nservers++] = in.s_addr;
    }
    inet_aton("1.1.1.1", &in);
    servers[nservers++] = in.s_addr;
    inet_aton("8.8.8.8", &in);
    servers[nservers++] = in.s_addr;

    for (unsigned i = 0; i < nservers; i++) {
        uint32_t ttl;
        if (dns_query(servers[i], host, addr, &ttl)) {
            if (ttl < 60) {
                ttl = 60;
            } else if (ttl > 3600) {
                ttl = 3600;
            }
            DnsEntry *e = &s_dns_cache[s_dns_next++ % DNS_CACHE_SIZE];
            strncpy(e->host, host, sizeof e->host - 1);
            e->host[sizeof e->host - 1] = '\0';
            e->addr = *addr;
            e->expires = gettime() + secs_to_ticks(ttl);
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------------- */
/* Adapter, SNTP, clock                                                       */

static void sntp_sync(void)
{
    uint32_t server;
    if (!resolve_host("pool.ntp.org", &server)) {
        return;
    }
    unsigned char pkt[48];
    memset(pkt, 0, sizeof pkt);
    pkt[0] = 0x1B;                      /* LI 0, version 3, mode 3 (client) */
    time_t sent_at = time(NULL);
    int n = udp_exchange(server, 123, pkt, sizeof pkt, pkt, sizeof pkt, SNTP_TIMEOUT_MS);
    if (n == (int) sizeof pkt && (pkt[0] & 0x07) == 4) {
        uint32_t secs = be32(pkt + 40);     /* transmit timestamp, seconds since 1900 */
        if (secs > NTP_UNIX_OFFSET) {
            s_time_offset = (time_t) (secs - NTP_UNIX_OFFSET) - sent_at;
        }
    }
}

bool net_start(char *err, size_t errlen)
{
    if (s_up) {
        return true;
    }
    char ip[16] = "", mask[16] = "", gw[16] = "";
    s32 ret = if_config(ip, mask, gw, true);
    if (ret < 0) {
        char msg[64];
        snprintf(msg, sizeof msg, "Network init failed (%d)", (int) ret);
        set_err(err, errlen, msg);
        return false;
    }
    strncpy(s_ip, ip, sizeof s_ip - 1);
    s_ip[sizeof s_ip - 1] = '\0';
    strncpy(s_gateway, gw, sizeof s_gateway - 1);
    s_gateway[sizeof s_gateway - 1] = '\0';
    s_up = true;
    sntp_sync();
    return true;
}

bool net_is_up(void)
{
    return s_up;
}

const char *net_ip_string(void)
{
    return s_up ? s_ip : "";
}

time_t net_time(void)
{
    return time(NULL) + s_time_offset;
}

bool net_set_ca(const char *pem, size_t len, char *err, size_t errlen)
{
    if (!pem || len == 0) {
        set_err(err, errlen, "Empty CA bundle");
        return false;
    }
    if (!tls_global_init(err, errlen)) {
        return false;
    }
    if (s_ca_loaded) {
        mbedtls_x509_crt_free(&s_ca);
        s_ca_loaded = false;
    }
    mbedtls_x509_crt_init(&s_ca);
    /* PEM input must include the terminating nul, hence len + 1. A positive
     * return means some certificates were skipped, which is fine. */
    int ret = mbedtls_x509_crt_parse(&s_ca, (const unsigned char *) pem, len + 1);
    if (ret < 0 || s_ca.version == 0) {
        mbedtls_x509_crt_free(&s_ca);
        set_err(err, errlen, "CA bundle not parsable");
        return false;
    }
    s_ca_loaded = true;
    return true;
}

/* ------------------------------------------------------------------------- */
/* URL                                                                        */

typedef struct {
    bool https;
    char host[MAX_HOST];
    uint16_t port;
    char path[MAX_URL];
} Url;

static bool url_parse(const char *s, Url *u)
{
    memset(u, 0, sizeof *u);
    if (strncasecmp(s, "https://", 8) == 0) {
        u->https = true;
        s += 8;
    } else if (strncasecmp(s, "http://", 7) == 0) {
        s += 7;
    } else {
        return false;
    }
    u->port = u->https ? 443 : 80;

    const char *path = strchr(s, '/');
    size_t authlen = path ? (size_t) (path - s) : strlen(s);
    if (authlen == 0 || authlen >= MAX_HOST) {
        return false;
    }
    char auth[MAX_HOST];
    memcpy(auth, s, authlen);
    auth[authlen] = '\0';

    char *colon = strchr(auth, ':');
    if (colon) {
        *colon = '\0';
        char *end;
        unsigned long port = strtoul(colon + 1, &end, 10);
        if (*end || port == 0 || port > 65535) {
            return false;
        }
        u->port = (uint16_t) port;
    }
    if (auth[0] == '\0') {
        return false;
    }
    strcpy(u->host, auth);
    if (path && *path) {
        strncpy(u->path, path, sizeof u->path - 1);
    } else {
        strcpy(u->path, "/");
    }
    return true;
}

/* Resolves a Location header against the request URL into out (MAX_URL). */
static bool url_resolve(const Url *base, const char *loc, char *out, size_t outlen)
{
    while (*loc == ' ' || *loc == '\t') {
        loc++;
    }
    if (strstr(loc, "://")) {
        if (strlen(loc) >= outlen) {
            return false;
        }
        strcpy(out, loc);
        return true;
    }
    int n;
    const char *scheme = base->https ? "https" : "http";
    bool default_port = base->port == (base->https ? 443 : 80);
    if (loc[0] == '/') {
        n = default_port
            ? snprintf(out, outlen, "%s://%s%s", scheme, base->host, loc)
            : snprintf(out, outlen, "%s://%s:%u%s", scheme, base->host, base->port, loc);
    } else {
        /* Relative to the directory of the current path. */
        const char *q = strchr(base->path, '?');
        size_t plen = q ? (size_t) (q - base->path) : strlen(base->path);
        while (plen > 0 && base->path[plen - 1] != '/') {
            plen--;
        }
        n = default_port
            ? snprintf(out, outlen, "%s://%s%.*s%s", scheme, base->host, (int) plen, base->path, loc)
            : snprintf(out, outlen, "%s://%s:%u%.*s%s", scheme, base->host, base->port,
                       (int) plen, base->path, loc);
    }
    return n > 0 && (size_t) n < outlen;
}

/* ------------------------------------------------------------------------- */
/* TCP connect                                                                */

static bool sock_connect(const Url *u, s32 *out, char *err, size_t errlen)
{
    uint32_t addr;
    if (!resolve_host(u->host, &addr)) {
        set_err(err, errlen, "Host not found");
        return false;
    }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_len = sizeof sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(u->port);
    sa.sin_addr.s_addr = addr;

    s32 sock = net_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        set_err(err, errlen, "No socket");
        return false;
    }
    if (net_connect(sock, (struct sockaddr *) &sa, sizeof sa) < 0) {
        net_close(sock);
        set_err(err, errlen, "Connection failed");
        return false;
    }
    *out = sock;
    return true;
}

/* ------------------------------------------------------------------------- */
/* Connection (plain or TLS)                                                  */

typedef struct {
    s32 sock;
    bool tls;
    volatile int *cancel;
    int wait_result;            /* last wait_readable() outcome, for messages */
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
} Conn;

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    Conn *c = ctx;
    s32 n = net_write(c->sock, buf, len);
    if (n < 0) {
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return (int) n;
}

/* The socket is blocking, so this never returns WANT_READ: it either has
 * data, or reports a timeout/cancel as a fatal error that ends the request. */
static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    Conn *c = ctx;
    c->wait_result = wait_readable(c->sock, c->cancel, READ_TIMEOUT_MS);
    if (c->wait_result != WAIT_READY) {
        return c->wait_result == WAIT_TIMEOUT ? MBEDTLS_ERR_SSL_TIMEOUT
                                              : MBEDTLS_ERR_NET_RECV_FAILED;
    }
    s32 n = net_read(c->sock, buf, len);
    if (n < 0) {
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return (int) n;
}

/* Explains a failed read; the caller has already decided it failed. */
static void read_fail_msg(const Conn *c, NetReq *r)
{
    if (c->cancel && *c->cancel) {
        req_err(r, "Cancelled");
    } else if (c->wait_result == WAIT_TIMEOUT) {
        req_err(r, "Timed out");
    } else {
        req_err(r, "Connection lost");
    }
}

static void conn_close(Conn *c)
{
    if (c->tls) {
        mbedtls_ssl_close_notify(&c->ssl);
        mbedtls_ssl_free(&c->ssl);
        mbedtls_ssl_config_free(&c->conf);
        c->tls = false;
    }
    if (c->sock >= 0) {
        net_close(c->sock);
        c->sock = -1;
    }
}

static bool conn_open(Conn *c, const Url *u, NetReq *r)
{
    c->sock = -1;
    c->tls = false;
    c->cancel = r->cancel;
    c->wait_result = WAIT_READY;

    if (!sock_connect(u, &c->sock, r->err, sizeof r->err)) {
        return false;
    }
    if (!u->https) {
        return true;
    }
    if (!tls_global_init(r->err, sizeof r->err)) {
        conn_close(c);
        return false;
    }

    mbedtls_ssl_config_init(&c->conf);
    mbedtls_ssl_init(&c->ssl);
    c->tls = true;
    int ret = mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        goto tls_fail;
    }
    mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &s_drbg);
    if (s_ca_loaded) {
        mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&c->conf, &s_ca, NULL);
    } else {
        mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_NONE);
    }
    if ((ret = mbedtls_ssl_setup(&c->ssl, &c->conf)) != 0 ||
        (ret = mbedtls_ssl_set_hostname(&c->ssl, u->host)) != 0) {
        goto tls_fail;
    }
    mbedtls_ssl_set_bio(&c->ssl, c, bio_send, bio_recv, NULL);

    while ((ret = mbedtls_ssl_handshake(&c->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            break;
        }
    }
    if (ret != 0) {
        if (c->cancel && *c->cancel) {
            req_err(r, "Cancelled");
        } else if (ret == MBEDTLS_ERR_SSL_TIMEOUT) {
            req_err(r, "Timed out");
        } else if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
            uint32_t flags = mbedtls_ssl_get_verify_result(&c->ssl);
            if (flags & (MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE)) {
                req_err(r, "Certificate date check failed (clock wrong?)");
            } else {
                req_err(r, "Certificate not trusted");
            }
        } else {
            goto tls_fail;
        }
        conn_close(c);
        return false;
    }
    r->verified = s_ca_loaded;
    return true;

tls_fail:
    snprintf(r->err, sizeof r->err, "TLS handshake failed (-0x%04X)", (unsigned) -ret);
    conn_close(c);
    return false;
}

static bool conn_write_all(Conn *c, const char *data, size_t len, NetReq *r)
{
    while (len > 0) {
        int n;
        if (c->tls) {
            n = mbedtls_ssl_write(&c->ssl, (const unsigned char *) data, len);
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }
        } else {
            n = (int) net_write(c->sock, data, len);
        }
        if (n <= 0) {
            req_err(r, "Send failed");
            return false;
        }
        data += n;
        len -= (size_t) n;
    }
    return true;
}

/* Returns bytes read, 0 at end of stream, -1 on error (err filled in). */
static int conn_read(Conn *c, unsigned char *buf, size_t len, NetReq *r)
{
    if (c->tls) {
        for (;;) {
            int n = mbedtls_ssl_read(&c->ssl, buf, len);
            if (n >= 0) {
                return n;
            }
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE ||
                n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
                continue;
            }
            /* CONN_EOF is a TCP close without close_notify; many servers do
             * that after Connection: close, so it counts as end of stream. */
            if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == MBEDTLS_ERR_SSL_CONN_EOF) {
                return 0;
            }
            read_fail_msg(c, r);
            return -1;
        }
    }
    c->wait_result = wait_readable(c->sock, c->cancel, READ_TIMEOUT_MS);
    if (c->wait_result != WAIT_READY) {
        read_fail_msg(c, r);
        return -1;
    }
    s32 n = net_read(c->sock, buf, len);
    if (n < 0) {
        read_fail_msg(c, r);
        return -1;
    }
    return (int) n;
}

/* ------------------------------------------------------------------------- */
/* Body sink: file or memory                                                  */

typedef struct {
    NetReq *r;
    FILE *f;
    unsigned char *fbuf;
    size_t fbuf_used;
    size_t cap;
} Sink;

static bool sink_open(Sink *s, NetReq *r)
{
    memset(s, 0, sizeof *s);
    s->r = r;
    if (r->path) {
        s->fbuf = malloc(FILE_BUF_SIZE);
        if (!s->fbuf) {
            req_err(r, "Out of memory");
            return false;
        }
        s->f = fopen(r->path, "wb");
        if (!s->f) {
            req_err(r, "Cannot create file");
            return false;
        }
        return true;
    }
    s->cap = 4096;
    r->mem = malloc(s->cap);
    if (!r->mem) {
        req_err(r, "Out of memory");
        return false;
    }
    r->mem[0] = '\0';
    r->len = 0;
    return true;
}

static bool sink_flush(Sink *s)
{
    if (s->fbuf_used && fwrite(s->fbuf, 1, s->fbuf_used, s->f) != s->fbuf_used) {
        req_err(s->r, "Write failed");
        return false;
    }
    s->fbuf_used = 0;
    return true;
}

static bool sink_write(Sink *s, const unsigned char *p, size_t n)
{
    NetReq *r = s->r;
    if (s->f) {
        while (n > 0) {
            size_t room = FILE_BUF_SIZE - s->fbuf_used;
            size_t take = n < room ? n : room;
            memcpy(s->fbuf + s->fbuf_used, p, take);
            s->fbuf_used += take;
            p += take;
            n -= take;
            if (s->fbuf_used == FILE_BUF_SIZE && !sink_flush(s)) {
                return false;
            }
        }
    } else {
        if (r->max_mem && r->len + n > r->max_mem) {
            req_err(r, "Too big");
            return false;
        }
        if (r->len + n + 1 > s->cap) {
            size_t cap = s->cap;
            while (r->len + n + 1 > cap) {
                cap *= 2;
            }
            char *grown = realloc(r->mem, cap);
            if (!grown) {
                req_err(r, "Out of memory");
                return false;
            }
            r->mem = grown;
            s->cap = cap;
        }
        memcpy(r->mem + r->len, p, n);
        r->len += n;
        r->mem[r->len] = '\0';
    }
    r->received += n;
    return true;
}

/* Finishes the sink. On failure the file is removed and memory freed. */
static void sink_close(Sink *s, bool *ok)
{
    NetReq *r = s->r;
    if (s->f) {
        if (*ok && !sink_flush(s)) {
            *ok = false;
        }
        if (fclose(s->f) != 0 && *ok) {
            req_err(r, "Write failed");
            *ok = false;
        }
        s->f = NULL;
        if (!*ok) {
            remove(r->path);
        }
    } else if (!*ok) {
        free(r->mem);
        r->mem = NULL;
        r->len = 0;
    }
    free(s->fbuf);
    s->fbuf = NULL;
}

/* ------------------------------------------------------------------------- */
/* HTTP response framing                                                      */

enum { BODY_LENGTH, BODY_CHUNKED, BODY_UNTIL_CLOSE };
enum { CH_SIZE, CH_DATA, CH_DATA_CRLF, CH_TRAILER };

typedef struct {
    int mode;
    size_t remaining;           /* BODY_LENGTH: bytes still expected */
    int cstate;                 /* chunked decoder */
    size_t chunk_left;
    char line[64];
    size_t linelen;
} Body;

enum { FEED_MORE, FEED_DONE, FEED_ERROR };

static int chunked_feed(Body *b, Sink *s, const unsigned char *p, size_t n)
{
    while (n > 0) {
        switch (b->cstate) {
        case CH_SIZE:
        case CH_DATA_CRLF:
        case CH_TRAILER: {
            unsigned char ch = *p++;
            n--;
            if (ch != '\n') {
                if (b->linelen < sizeof b->line - 1) {
                    b->line[b->linelen++] = (char) ch;
                }
                break;
            }
            b->line[b->linelen] = '\0';
            size_t len = b->linelen;
            b->linelen = 0;
            while (len > 0 && (b->line[len - 1] == '\r' || b->line[len - 1] == ' ')) {
                b->line[--len] = '\0';
            }
            if (b->cstate == CH_DATA_CRLF) {
                if (len != 0) {
                    req_err(s->r, "Bad chunked encoding");
                    return FEED_ERROR;
                }
                b->cstate = CH_SIZE;
            } else if (b->cstate == CH_TRAILER) {
                if (len == 0) {
                    return FEED_DONE;
                }
            } else {
                if (len == 0) {
                    break;      /* tolerate a stray blank line */
                }
                char *end;
                unsigned long size = strtoul(b->line, &end, 16);
                if (end == b->line || (*end && *end != ';' && *end != ' ')) {
                    req_err(s->r, "Bad chunked encoding");
                    return FEED_ERROR;
                }
                if (size == 0) {
                    b->cstate = CH_TRAILER;
                } else {
                    b->chunk_left = size;
                    b->cstate = CH_DATA;
                }
            }
            break;
        }
        case CH_DATA: {
            size_t take = n < b->chunk_left ? n : b->chunk_left;
            if (!sink_write(s, p, take)) {
                return FEED_ERROR;
            }
            p += take;
            n -= take;
            b->chunk_left -= take;
            if (b->chunk_left == 0) {
                b->cstate = CH_DATA_CRLF;
            }
            break;
        }
        }
    }
    return FEED_MORE;
}

static int body_feed(Body *b, Sink *s, const unsigned char *p, size_t n)
{
    switch (b->mode) {
    case BODY_CHUNKED:
        return chunked_feed(b, s, p, n);
    case BODY_LENGTH:
        if (n > b->remaining) {
            n = b->remaining;
        }
        if (!sink_write(s, p, n)) {
            return FEED_ERROR;
        }
        b->remaining -= n;
        return b->remaining == 0 ? FEED_DONE : FEED_MORE;
    default:
        return sink_write(s, p, n) ? FEED_MORE : FEED_ERROR;
    }
}

typedef struct {
    int status;
    bool chunked;
    bool has_length;
    size_t length;
    char location[MAX_URL];
} Head;

static bool contains_ci(const char *s, const char *needle)
{
    size_t n = strlen(needle);
    for (; *s; s++) {
        if (strncasecmp(s, needle, n) == 0) {
            return true;
        }
    }
    return false;
}

/* Parses the header block in place (it is nul-terminated by the caller). */
static bool head_parse(char *hdr, Head *h)
{
    memset(h, 0, sizeof *h);
    char *line = hdr;
    char *nl = strstr(line, "\r\n");
    if (!nl || strncasecmp(line, "HTTP/", 5) != 0) {
        return false;
    }
    *nl = '\0';
    char *sp = strchr(line, ' ');
    if (!sp) {
        return false;
    }
    h->status = (int) strtol(sp + 1, NULL, 10);
    if (h->status < 100 || h->status > 999) {
        return false;
    }
    line = nl + 2;

    while (*line) {
        nl = strstr(line, "\r\n");
        if (nl) {
            *nl = '\0';
        }
        char *colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            char *val = colon + 1;
            while (*val == ' ' || *val == '\t') {
                val++;
            }
            size_t vlen = strlen(val);
            while (vlen > 0 && (val[vlen - 1] == ' ' || val[vlen - 1] == '\t')) {
                val[--vlen] = '\0';
            }
            if (strcasecmp(line, "Content-Length") == 0) {
                char *end;
                unsigned long long v = strtoull(val, &end, 10);
                if (end != val && *end == '\0') {
                    h->has_length = true;
                    h->length = (size_t) v;
                }
            } else if (strcasecmp(line, "Transfer-Encoding") == 0) {
                if (contains_ci(val, "chunked")) {
                    h->chunked = true;
                }
            } else if (strcasecmp(line, "Location") == 0) {
                strncpy(h->location, val, sizeof h->location - 1);
            }
        }
        if (!nl) {
            break;
        }
        line = nl + 2;
    }
    return true;
}

static bool is_redirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

/* Sends the request and reads the response head into buf. On return the
 * bytes between *body_off and *got are body data that arrived with the head. */
static bool fetch_head(Conn *c, const Url *u, unsigned char *buf, Head *h,
                       size_t *body_off, size_t *got, NetReq *r)
{
    /* buf is free until the request has gone out, so format it there. */
    char *req = (char *) buf;
    bool default_port = u->port == (u->https ? 443 : 80);
    int n = default_port
        ? snprintf(req, READ_BUF_SIZE,
                   "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: " USER_AGENT "\r\n"
                   "Accept: */*\r\nConnection: close\r\n\r\n", u->path, u->host)
        : snprintf(req, READ_BUF_SIZE,
                   "GET %s HTTP/1.1\r\nHost: %s:%u\r\nUser-Agent: " USER_AGENT "\r\n"
                   "Accept: */*\r\nConnection: close\r\n\r\n", u->path, u->host, u->port);
    if (n <= 0 || (size_t) n >= READ_BUF_SIZE) {
        req_err(r, "URL too long");
        return false;
    }
    if (!conn_write_all(c, req, (size_t) n, r)) {
        return false;
    }

    size_t used = 0;
    char *end = NULL;
    while (!end) {
        if (used >= READ_BUF_SIZE - 1) {
            req_err(r, "Response headers too long");
            return false;
        }
        int rd = conn_read(c, buf + used, READ_BUF_SIZE - 1 - used, r);
        if (rd < 0) {
            return false;
        }
        if (rd == 0) {
            req_err(r, "Empty response");
            return false;
        }
        size_t scan_from = used >= 3 ? used - 3 : 0;
        used += (size_t) rd;
        buf[used] = '\0';
        end = strstr((char *) buf + scan_from, "\r\n\r\n");
    }
    /* Terminate the head after its final CRLF so head_parse sees every line. */
    end[2] = '\0';
    if (!head_parse((char *) buf, h)) {
        req_err(r, "Bad HTTP response");
        return false;
    }
    *body_off = (size_t) (end - (char *) buf) + 4;
    *got = used;
    return true;
}

/* Per-request scratch, heap-allocated to keep the worker's stack small. */
typedef struct {
    Conn conn;
    Url url;
    Head head;
    Body body;
    Sink sink;
    char cur[MAX_URL];
    char next[MAX_URL];
} Work;

bool net_get(NetReq *r)
{
    bool ok = false;
    bool sink_opened = false;
    unsigned char *buf = NULL;
    Work *w = NULL;

    r->received = 0;
    r->total = 0;
    r->mem = NULL;
    r->len = 0;
    r->status = 0;
    r->verified = false;
    r->err[0] = '\0';

    if (!s_up) {
        req_err(r, "Network is down");
        return false;
    }
    if (!r->url || strlen(r->url) >= MAX_URL) {
        req_err(r, "URL too long");
        return false;
    }

    buf = malloc(READ_BUF_SIZE);
    w = malloc(sizeof *w);
    if (!buf || !w) {
        req_err(r, "Out of memory");
        goto out;
    }
    memset(w, 0, sizeof *w);
    w->conn.sock = -1;
    strcpy(w->cur, r->url);

    Conn *c = &w->conn;
    Url *u = &w->url;
    Head *h = &w->head;
    Body *body = &w->body;

    for (int hops = 0; ; hops++) {
        size_t body_off, got;

        if (!url_parse(w->cur, u)) {
            req_err(r, "Bad URL");
            goto out;
        }
        if (!conn_open(c, u, r)) {
            goto out;
        }
        if (!fetch_head(c, u, buf, h, &body_off, &got, r)) {
            goto out;
        }
        r->status = h->status;

        if (is_redirect(h->status) && h->location[0]) {
            conn_close(c);
            if (hops >= MAX_REDIRECTS) {
                req_err(r, "Too many redirects");
                goto out;
            }
            if (!url_resolve(u, h->location, w->next, sizeof w->next)) {
                req_err(r, "Bad redirect");
                goto out;
            }
            strcpy(w->cur, w->next);
            r->verified = false;
            continue;
        }
        if (h->status < 200 || h->status > 299) {
            snprintf(r->err, sizeof r->err, "HTTP %d", h->status);
            goto out;
        }

        memset(body, 0, sizeof *body);
        if (h->chunked) {
            body->mode = BODY_CHUNKED;
        } else if (h->has_length) {
            body->mode = BODY_LENGTH;
            body->remaining = h->length;
            r->total = h->length;
        } else {
            body->mode = BODY_UNTIL_CLOSE;
        }

        /* sink_open zeroes the sink first, so sink_close is safe after a
         * partial failure too. */
        sink_opened = true;
        if (!sink_open(&w->sink, r)) {
            goto out;
        }

        int st = FEED_MORE;
        if (body->mode == BODY_LENGTH && body->remaining == 0) {
            st = FEED_DONE;
        } else if (got > body_off) {
            st = body_feed(body, &w->sink, buf + body_off, got - body_off);
        }
        while (st == FEED_MORE) {
            if (r->cancel && *r->cancel) {
                req_err(r, "Cancelled");
                goto out;
            }
            int rd = conn_read(c, buf, READ_BUF_SIZE, r);
            if (rd < 0) {
                goto out;
            }
            if (rd == 0) {
                if (body->mode == BODY_UNTIL_CLOSE) {
                    st = FEED_DONE;
                } else {
                    req_err(r, "Connection closed early");
                    goto out;
                }
                break;
            }
            st = body_feed(body, &w->sink, buf, (size_t) rd);
        }
        if (st == FEED_ERROR) {
            goto out;
        }
        ok = true;
        break;
    }

out:
    if (w) {
        if (sink_opened) {
            sink_close(&w->sink, &ok);
        }
        conn_close(&w->conn);
        free(w);
    }
    free(buf);
    return ok;
}
