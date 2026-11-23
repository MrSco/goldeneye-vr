/*
 * SHA-256 (FIPS 180-4) in plain C, for every build: the mod packs' checksums
 * and content ID (gevr_modpack.c) and the PC updater's download check
 * (port/pc/pc_http.cpp sha256File).
 */
#ifndef GEVR_SHA256_H
#define GEVR_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gevrSha256 {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} gevrSha256;

void gevrSha256Init(gevrSha256 *s);
void gevrSha256Update(gevrSha256 *s, const void *data, size_t len);
void gevrSha256Final(gevrSha256 *s, uint8_t digest[32]);
/* lower-case hex, 64 characters and a terminator */
void gevrSha256Hex(const uint8_t digest[32], char hex[65]);

#ifdef __cplusplus
}
#endif

#endif
