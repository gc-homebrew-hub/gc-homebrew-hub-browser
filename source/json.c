#include "json.h"
#include <stdlib.h>
#include <string.h>

typedef struct { const char *s; size_t len; size_t pos; JTok *t; int n, max; int depth; } P;

static void ws(P *p) {
    while (p->pos < p->len && (p->s[p->pos] == ' ' || p->s[p->pos] == '\t' ||
                               p->s[p->pos] == '\n' || p->s[p->pos] == '\r')) p->pos++;
}

static int add(P *p, JType type, int start, int end) {
    if (p->n >= p->max) return -1;
    p->t[p->n].type = type; p->t[p->n].start = start; p->t[p->n].end = end; p->t[p->n].size = 0;
    return p->n++;
}

static int value(P *p);

static int string(P *p) {
    size_t start = ++p->pos;
    while (p->pos < p->len && p->s[p->pos] != '"') {
        if (p->s[p->pos] == '\\') p->pos++;
        p->pos++;
    }
    if (p->pos >= p->len) return -1;
    int i = add(p, JT_STRING, (int)start, (int)p->pos);
    p->pos++;
    return i;
}

static int value(P *p) {
    ws(p);
    if (p->pos >= p->len) return -1;
    char c = p->s[p->pos];
    if (c == '"') return string(p);
    if (c == '{' || c == '[') {
        if (++p->depth > 24) return -1;
        bool obj = c == '{';
        int i = add(p, obj ? JT_OBJECT : JT_ARRAY, (int)p->pos, 0);
        if (i < 0) return -1;
        p->pos++;
        for (;;) {
            ws(p);
            if (p->pos >= p->len) return -1;
            if (p->s[p->pos] == (obj ? '}' : ']')) { p->pos++; break; }
            if (p->t[i].size > 0) {
                if (p->s[p->pos] != ',') return -1;
                p->pos++; ws(p);
            }
            if (obj) {
                if (p->pos >= p->len || p->s[p->pos] != '"') return -1;
                int k = string(p);
                if (k < 0) return -1;
                p->t[k].size = 1;
                ws(p);
                if (p->pos >= p->len || p->s[p->pos] != ':') return -1;
                p->pos++;
            }
            if (value(p) < 0) return -1;
            p->t[i].size++;
        }
        p->t[i].end = (int)p->pos;
        p->depth--;
        return i;
    }
    size_t start = p->pos;
    while (p->pos < p->len && strchr(",]} \t\r\n", p->s[p->pos]) == NULL) p->pos++;
    if (p->pos == start) return -1;
    return add(p, JT_PRIMITIVE, (int)start, (int)p->pos);
}

int json_parse(const char *text, size_t len, JTok *toks, int max) {
    P p = { text, len, 0, toks, 0, max, 0 };
    if (value(&p) < 0) return -1;
    ws(&p);
    if (p.pos != len) return -1;
    return p.n;
}

int json_next(const JTok *t, int i) {
    int j = i + 1;
    if (t[i].type == JT_STRING && t[i].size == 1) return json_next(t, j);
    if (t[i].type == JT_OBJECT || t[i].type == JT_ARRAY)
        for (int k = 0; k < t[i].size; k++) j = json_next(t, j);
    return j;
}

int json_first(const JTok *t, int i) {
    if ((t[i].type != JT_OBJECT && t[i].type != JT_ARRAY) || t[i].size == 0) return -1;
    return i + 1;
}

int json_find(const char *text, const JTok *t, int obj, const char *key) {
    if (obj < 0 || t[obj].type != JT_OBJECT) return -1;
    size_t kl = strlen(key);
    int k = obj + 1;
    for (int n = 0; n < t[obj].size; n++) {
        if ((size_t)(t[k].end - t[k].start) == kl && memcmp(text + t[k].start, key, kl) == 0)
            return k + 1;
        k = json_next(t, k);
    }
    return -1;
}

static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool json_string(const char *text, const JTok *t, int i, char *out, size_t cap) {
    if (!cap) return false;
    out[0] = 0;
    if (i < 0 || t[i].type != JT_STRING) return false;
    size_t o = 0;
    for (int p = t[i].start; p < t[i].end && o + 1 < cap; p++) {
        unsigned char c = (unsigned char)text[p];
        if (c == '\\' && p + 1 < t[i].end) {
            char e = text[++p];
            switch (e) {
            case 'n': c = '\n'; break;
            case 't': c = ' '; break;
            case 'r': continue;
            case 'u': {
                if (p + 4 >= t[i].end) return false;
                int v = 0;
                for (int k = 1; k <= 4; k++) { int h = hex(text[p + k]); if (h < 0) return false; v = v * 16 + h; }
                p += 4;
                c = (v >= 32 && v < 127) ? (unsigned char)v : '?';
                break;
            }
            default: c = (unsigned char)e; break;
            }
        } else if (c >= 0x80) {
            /* skip UTF-8 continuation bytes; one '?' per character */
            if ((c & 0xC0) == 0x80) continue;
            c = '?';
        } else if (c < 32 && c != '\n') continue;
        out[o++] = (char)c;
    }
    out[o] = 0;
    return true;
}

long long json_int(const char *text, const JTok *t, int i, long long def) {
    if (i < 0 || t[i].type != JT_PRIMITIVE) return def;
    char buf[24];
    int n = t[i].end - t[i].start;
    if (n <= 0 || n >= (int)sizeof buf) return def;
    memcpy(buf, text + t[i].start, n); buf[n] = 0;
    char *end;
    long long v = strtoll(buf, &end, 10);
    return (*end == 0 || *end == '.') ? v : def;
}

bool json_bool(const char *text, const JTok *t, int i, bool def) {
    if (i < 0 || t[i].type != JT_PRIMITIVE) return def;
    int n = t[i].end - t[i].start;
    if (n == 4 && !memcmp(text + t[i].start, "true", 4)) return true;
    if (n == 5 && !memcmp(text + t[i].start, "false", 5)) return false;
    return def;
}
