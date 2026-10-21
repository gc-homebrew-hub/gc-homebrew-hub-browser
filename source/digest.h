#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct { uint32_t h[8]; uint64_t len; unsigned char buf[64]; size_t buflen; } Sha256;

void sha256_init(Sha256 *s);
void sha256_update(Sha256 *s, const void *data, size_t len);
void sha256_final(Sha256 *s, unsigned char out[32]);
void sha256_hex(const unsigned char d[32], char out[65]);
/* Hashes a whole file; returns false when it cannot be read. */
bool sha256_file(const char *path, char hex_out[65], volatile size_t *progress);
