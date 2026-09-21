/**
 * The random stream used for chr objects.
 *
 * Transcribed from src/game/chrObjRandom.s, which is instruction-for-instruction
 * the same generator as src/random.s - the game keeps two independent streams so
 * that object behaviour and everything else do not draw from each other, which
 * is what lets a recorded demo replay identically.
 *
 * See src/random.c for the derivation of the shift sequence from the assembly.
 */

#include <ultra64.h>
#include "chrObjRandom.h"

/* src/game/chrObjRandom.s: .word 0xAB8D9F77, .word 0x81280783, big-endian. */
u64 g_chrObjRandomSeed = 0xAB8D9F7781280783ULL;

u32 chrObjRandomGetNext(void)
{
	u64 seed = g_chrObjRandomSeed;
	u64 v;

	v  = ((seed << 63) >> 31) | ((seed << 31) >> 32);
	v ^= (seed << 44) >> 32;
	v ^= (v >> 20) & 0xfff;

	g_chrObjRandomSeed = v;

	return (u32)v;
}

void chrObjRandomSetSeed(u32 param_1)
{
	/* daddiu adds to the full 64-bit register; see randomSetSeed in random.c. */
	g_chrObjRandomSeed = (u64)((s64)(s32)param_1 + 1);
}
