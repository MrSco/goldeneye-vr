#ifndef _IN_PREPROCESS_COMMON_H
#define _IN_PREPROCESS_COMMON_H

/*
 * Slimmed for GEVR: only what segaudio.c / common.c need to convert ALBank
 * files from cartridge endianness. Perfect Dark's full preprocess stack also
 * pulled types.h / constants.h / romdata.h, which this tree does not ship.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <PR/ultratypes.h>
#include "platform.h"
#include "system.h"
#include "preprocess.h"

#ifndef ARRAYCOUNT
#define ARRAYCOUNT(a) (s32)(sizeof(a) / sizeof((a)[0]))
#endif

#ifndef ALIGN16
#define ALIGN16(val) (((val) + 0xF) & ~0xF)
#endif

#define MAX_PTR_MARKERS (1024 * 8)

#define PD_ALIGN(val, size) (((val) + ((size) - 1)) & ~((size) - 1))

#define PD_PTR_BASE(x, b) (void *)((u8 *)b + (uintptr_t)x)

static inline u32 swapU32(u32 x) { return PD_BE32(x); }
static inline s32 swapS32(s32 x) { return PD_BE32(x); }
static inline u16 swapU16(u16 x) { return PD_BE16(x); }
static inline s16 swapS16(s16 x) { return PD_BE16(x); }

#define PD_SWAPPED_VAL(x) _Generic((x), \
	u32: swapU32, \
	s32: swapS32, \
	u16: swapU16, \
	s16: swapS16, \
	default: swapU32 \
)(x)

#define PD_SWAP_VAL(x) x = PD_SWAPPED_VAL(x)

struct ptrmarker {
	u32 ptr_src;
	uintptr_t ptr_host;
};

void ptrAdd(u32 ptr_src, uintptr_t ptr_host);
struct ptrmarker *ptrFind(uintptr_t ptr_src);
void ptrReset(void);

#endif
