/**
 * GoldenEye's random number generator.
 *
 * The decomp carries this as MIPS assembly in src/random.s, which cannot be
 * assembled for any other target, so this is a straight transcription of that
 * file. It is a 64-bit xorshift over g_randomSeed; the sequence and the seed
 * are the same as the ROM's, so replays and anything else that depends on the
 * exact stream still line up.
 *
 * Mapping from the assembly, with s as the 64-bit seed:
 *
 *   dsll32 a2, a0, 0x1f ; dsrl a2, a2, 0x1f   ->  (s << 63) >> 31
 *   dsll   a1, a0, 0x1f ; dsrl32 a1, a1, 0    ->  (s << 31) >> 32
 *   dsll32 a0, a0, 0xc  ; dsrl32 a0, a0, 0    ->  (s << 44) >> 32
 *
 * every shift being logical, which is what the unsigned C shifts below do.
 * The return is dsra32 of the new seed shifted up by 32, i.e. its low word.
 */

#include <ultra64.h>
#include "random.h"

/* src/random.s: .word 0xAB8D9F77, .word 0x81280783, big-endian, so high word first. */
u64 g_randomSeed = 0xAB8D9F7781280783ULL;

static inline u64 randomAdvance(u64 seed)
{
	u64 v;

	v  = ((seed << 63) >> 31) | ((seed << 31) >> 32);
	v ^= (seed << 44) >> 32;
	v ^= (v >> 20) & 0xfff;

	return v;
}

u32 randomGetNext(void)
{
	g_randomSeed = randomAdvance(g_randomSeed);

	return (u32)g_randomSeed;
}

u32 randomGetNextFrom(u64 *param_1)
{
	*param_1 = randomAdvance(*param_1);

	return (u32)*param_1;
}

void randomSetSeed(u32 param_1)
{
	/*
	 * daddiu adds to the full 64-bit register, and the N64 ABI hands a 32-bit
	 * argument over sign-extended, so the increment happens on the sign-extended
	 * value rather than on the bare unsigned one.
	 */
	g_randomSeed = (u64)((s64)(s32)param_1 + 1);
}
