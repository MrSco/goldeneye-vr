#ifndef GEVR_ROMCHECK_H
#define GEVR_ROMCHECK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Normalise a complete cartridge image, including v64/n64 byte order. */
int gevrRomNormaliseOrder(unsigned char *rom, unsigned int size);

/* Check the manifest's compressed files before enabling Start/binding them. */
int gevrRomCheckFiles(const unsigned char *rom, unsigned int size,
                     char *error, size_t errorSize);

/* Bounded raw DEFLATE after Rare's 1172 header. dst == NULL validates only.
 * In-place expansion is supported; failure never returns partial output as a
 * successful file. Sizes include the two-byte header and ROM padding. */
int gevrInflate1172(const unsigned char *src, unsigned int srcSize,
                    unsigned char *dst, unsigned int capacity,
                    unsigned int *written);

#ifdef __cplusplus
}
#endif
#endif
