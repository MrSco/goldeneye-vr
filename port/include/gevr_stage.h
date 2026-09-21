#ifndef GEVR_STAGE_H
#define GEVR_STAGE_H
#include <stddef.h>
#include <stdint.h>

/* Host records. File offsets remain segment-tagged until the game's normal
 * relocation pass; room payload offsets always refer to the original ROM. */
struct gevrBgRoom { void *points, *primary, *secondary; float pos[3]; };
struct gevrBgPortal { void *points; uint8_t room1, room2, flags1, flags2; };
struct gevrStanPrefix { int32_t reserved; void *firstroom; };

uint32_t gevrBgHeaderSize(const uint8_t header[64]);
/* Return new byte length, or zero on invalid data/insufficient capacity. */
size_t gevrConvertBg(uint8_t *data, size_t size, size_t capacity);
size_t gevrConvertStan(uint8_t *data, size_t size, size_t capacity);
size_t gevrConvertSetup(uint8_t *data, size_t size, size_t capacity);
/* Host prop-def size in u32 words after gevrConvertSetup (for sizepropdef). */
int gevrSetupPropWords(unsigned type);
#endif
