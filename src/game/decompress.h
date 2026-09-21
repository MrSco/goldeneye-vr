#ifndef _DECOMPRESS_H_
#define _DECOMPRESS_H_
#include <ultra64.h>
#include <inflate/inflate.h>


u32 decompressdata(u8 *src, u8 *dst, struct huft *hlist);
u8 *rzipGetSomething(void);

/* See zlib.c: set by a caller whose input and output are separate
   allocations, so the output-overran-input guard cannot apply. */
extern s32 rz_buffersAreDisjoint;

#endif
