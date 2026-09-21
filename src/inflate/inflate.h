#ifndef _INFLATE_H_
#define _INFLATE_H_
struct huft {
	u8 e;                /* number of extra bits or operation */
	u8 b;                /* number of bits in this code or subcode */
	union {
		u16 n;            /* literal, length base, or distance base */
		struct huft *t;   /* pointer to next level of table */
	} v;
};

/*
 * Callers hand decompressdata() a scratch buffer for the Huffman tables it
 * builds. The N64 sized that at 0x2100 bytes, which is 1056 entries of the
 * 8-byte struct huft it had there. Here the union carries a 64-bit pointer, so
 * the struct is twice as wide and the same number of entries needs twice the
 * room - sizing it in bytes overflowed the buffer and tripped the stack guard.
 */
#define HUFT_SCRATCH_ENTRIES 1056
#define HUFT_SCRATCH_BYTES   (HUFT_SCRATCH_ENTRIES * sizeof(struct huft))

u32 decompress_entry(void *src, void *dst, struct huft *hlist);

#endif
