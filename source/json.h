/* json.h - a small JSON tokenizer: fills an array of tokens that point back
 * into the text, then lets callers walk objects and arrays. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef enum { JT_NONE, JT_OBJECT, JT_ARRAY, JT_STRING, JT_PRIMITIVE } JType;

typedef struct {
    JType type;
    int start, end;   /* byte range in the text (string: without the quotes) */
    int size;         /* object: number of keys; array: elements; key string: 1 */
} JTok;

/* Returns the number of tokens, or -1 on malformed text / too many tokens. */
int json_parse(const char *text, size_t len, JTok *toks, int max);
/* Index of the token after token i's whole subtree (a key skips its value too). */
int json_next(const JTok *t, int i);
/* Value index of `key` in object `obj`, or -1. */
int json_find(const char *text, const JTok *t, int obj, const char *key);
/* First element of array/object `i` (iterate with json_next); -1 when empty. */
int json_first(const JTok *t, int i);
/* Copies the unescaped string (ASCII only; other characters become '?'). */
bool json_string(const char *text, const JTok *t, int i, char *out, size_t cap);
long long json_int(const char *text, const JTok *t, int i, long long def);
bool json_bool(const char *text, const JTok *t, int i, bool def);
