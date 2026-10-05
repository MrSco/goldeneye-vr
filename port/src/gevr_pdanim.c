/**
 * Perfect Dark's animation reader, ported from pdvr src/lib/anim.c
 * (animGetRotTranslateScale, animReadBits, animRemapFrameForLoad) and
 * src/lib/mtx.c (mtx4LoadRotation), reading GoldenEye X's ROM in place.
 *
 * GE-X moved the animations segment's data to ROM 0x157810 (Perfect Dark's
 * is at 0x1a15c0; animation 1, at offset 0, is byte for byte PD's there).
 * The table, at 0x7cd1a0, is a count and then 12-byte rows: frames, bytes per frame, the data's offset
 * into the segment, the header's length, the frame bit length and flags. An
 * animation's data is its header, then its frames. The header holds, per
 * model part in order, a flags byte and the base values and bit lengths of
 * the fields present; a frame packs those fields' bits for every part.
 */

#include <math.h>
#include <string.h>
#include <PR/ultratypes.h>
#include "gevr_gex.h"
#include "gevr_pdanim.h"

#define ANIM_SEGMENT 0x157810
#define ANIM_TABLE   0x7cd1a0

#define ANIMFLAG_HASREPEATFRAMES 0x04

#define ANIMFIELD_S16_ROTATE    0x01
#define ANIMFIELD_S16_TRANSLATE 0x02
#define ANIMFIELD_08            0x08
#define ANIMFIELD_F32_ROTATE    0x10
#define ANIMFIELD_S32_TRANSLATE 0x20
#define ANIMFIELD_CAMERA        0x40
#define ANIMFIELD_F32_SCALE     0x80

/* Perfect Dark's own tau, a little short of 2 pi */
#define M_BADTAU (3.141092641f * 2)

struct pdAnim {
	u16 numframes, bytesperframe;
	u32 data;
	u16 headerlen;
	u8 framelen, flags;
};

static u16 be16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

/* the ROM and a row of its table, checked against the segment's bounds */
static const u8 *animRow(s32 animnum, struct pdAnim *a)
{
	u32 size = 0;
	const u8 *rom = gevrGexRom(&size);
	const u8 *row;

	if (rom == NULL || size < ANIM_TABLE + 4 || animnum <= 0 || (u32)animnum >= be32(rom + ANIM_TABLE)) {
		return NULL;
	}
	row = rom + ANIM_TABLE + 4 + 12 * animnum;
	a->numframes = be16(row);
	a->bytesperframe = be16(row + 2);
	a->data = be32(row + 4);
	a->headerlen = be16(row + 8);
	a->framelen = row[10];
	a->flags = row[11];
	if (ANIM_SEGMENT + (u64)a->data + a->headerlen + (u64)a->bytesperframe * a->numframes > ANIM_TABLE) {
		return NULL;
	}
	return rom;
}

s32 gevrPdAnimNumFrames(s32 animnum)
{
	struct pdAnim a;

	return animRow(animnum, &a) != NULL ? a.numframes : 0;
}

static s32 animReadBits(const u8 *ptr, u8 remainingbits, u32 bitoffset)
{
	u32 result = 0;
	u32 mask;
	u8 numbitsthisbyte;

	ptr += bitoffset / 8;
	bitoffset %= 8;
	numbitsthisbyte = 8 - bitoffset;

	while (remainingbits >= numbitsthisbyte) {
		remainingbits -= numbitsthisbyte;
		mask = (1 << numbitsthisbyte) - 1;
		result |= (*ptr & mask) << remainingbits;
		ptr++;
		numbitsthisbyte = 8;
	}

	if (remainingbits > 0) {
		mask = (1 << remainingbits) - 1;
		result |= (*ptr >> (numbitsthisbyte - remainingbits)) & mask;
	}

	return result;
}

static s32 animReadSignedShort(const u8 *ptr, u8 readbitlen, s32 bitoffset)
{
	u16 result = animReadBits(ptr, readbitlen, bitoffset);

	if (readbitlen < 16 && (result & (1 << (readbitlen - 1)))) {
		result |= ((1 << (16 - readbitlen)) - 1) << readbitlen;
	}

	return result;
}

/* a frame that repeats an earlier one is stored once (the header's tail) */
static s32 animRemapFrame(const u8 *header, const struct pdAnim *a, s32 frame)
{
	const u8 *ptr = header + a->headerlen - 2;
	s32 result = frame;

	while (ptr >= header + 2) {
		s16 repeatfrom = (s16)be16(ptr);
		s16 repeatto;

		if (repeatfrom < 0) {
			break;
		}

		repeatto = (s16)be16(ptr - 2);
		ptr -= 4;

		if (repeatfrom <= frame) {
			if (repeatto < frame) {
				result = result - repeatto + repeatfrom - 1;
			} else {
				result = result - frame + repeatfrom;
				break;
			}
		}
	}

	return result;
}

s32 gevrPdAnimPart(s32 animnum, s32 frame, s32 part, f32 rot[3], f32 trans[3], f32 scale[3])
{
	struct pdAnim a;
	const u8 *rom = animRow(animnum, &a);
	const u8 *ptr, *end, *framebytes;
	u8 readbitlen;
	s32 bitoffset = 0, i;

	rot[0] = rot[1] = rot[2] = 0.0f;
	trans[0] = trans[1] = trans[2] = 0.0f;
	scale[0] = scale[1] = scale[2] = 1.0f;

	if (rom == NULL || a.numframes == 0) {
		return 0;
	}
	if (frame < 0) {
		frame = 0;
	}
	if (frame >= a.numframes) {
		frame = a.numframes - 1;
	}

	ptr = rom + ANIM_SEGMENT + a.data;
	end = ptr + a.headerlen;
	if (a.flags & ANIMFLAG_HASREPEATFRAMES) {
		frame = animRemapFrame(ptr, &a, frame);
	}
	framebytes = end + (u32)a.bytesperframe * frame;

	/* skip the parts before this one */
	for (i = 0; i < part && ptr < end; i++) {
		u8 flags = *ptr++;

		if (flags & ANIMFIELD_08) {
			bitoffset += ptr[2] + ptr[5] + ptr[8] + ptr[11];
			ptr += 12;
		} else if (flags & ANIMFIELD_S16_TRANSLATE) {
			bitoffset += ptr[2] + ptr[5] + ptr[8];
			ptr += 9;
		} else if (flags & ANIMFIELD_S32_TRANSLATE) {
			bitoffset += ptr[0] + ptr[5] + ptr[10];
			ptr += 15;
		}

		if (flags & ANIMFIELD_S16_ROTATE) {
			bitoffset += ptr[2] + ptr[5] + ptr[8];
			ptr += 9;
		} else if (flags & ANIMFIELD_F32_ROTATE) {
			bitoffset += 96;
		}

		if (flags & ANIMFIELD_CAMERA) {
			bitoffset += ptr[0];
			ptr += 5;
		}

		if (flags & ANIMFIELD_F32_SCALE) {
			bitoffset += 0x60;
		}
	}

	if (ptr >= end) {
		return 0;
	}

	{
		u8 flags = *ptr++;

		if (flags & ANIMFIELD_S16_TRANSLATE) {
			readbitlen = ptr[2];
			trans[0] = (s16)(animReadSignedShort(framebytes, readbitlen, bitoffset) + (ptr[0] << 8) + ptr[1]);
			bitoffset += readbitlen;

			readbitlen = ptr[5];
			trans[1] = (s16)(animReadSignedShort(framebytes, readbitlen, bitoffset) + (ptr[3] << 8) + ptr[4]);
			bitoffset += readbitlen;

			readbitlen = ptr[8];
			trans[2] = (s16)(animReadSignedShort(framebytes, readbitlen, bitoffset) + (ptr[6] << 8) + ptr[7]);
			bitoffset += readbitlen;

			ptr += 9;
		} else if (flags & ANIMFIELD_S32_TRANSLATE) {
			readbitlen = ptr[0];
			trans[0] = (animReadBits(framebytes, readbitlen, bitoffset) + (s32)be32(ptr + 1)) * 0.001f;
			bitoffset += readbitlen;

			readbitlen = ptr[5];
			trans[1] = (animReadBits(framebytes, readbitlen, bitoffset) + (s32)be32(ptr + 6)) * 0.001f;
			bitoffset += readbitlen;

			readbitlen = ptr[10];
			trans[2] = (animReadBits(framebytes, readbitlen, bitoffset) + (s32)be32(ptr + 11)) * 0.001f;
			bitoffset += readbitlen;

			ptr += 15;
		} else if (flags & ANIMFIELD_08) {
			bitoffset += ptr[2] + ptr[5] + ptr[8] + ptr[11];
			ptr += 12;
		}

		if (flags & ANIMFIELD_S16_ROTATE) {
			u16 introt[3];

			readbitlen = ptr[2];
			introt[0] = animReadBits(framebytes, readbitlen, bitoffset);
			introt[0] += (ptr[0] << 8) + ptr[1];
			introt[0] <<= 16 - a.framelen;
			bitoffset += readbitlen;

			readbitlen = ptr[5];
			introt[1] = animReadBits(framebytes, readbitlen, bitoffset);
			introt[1] += (ptr[3] << 8) + ptr[4];
			introt[1] <<= 16 - a.framelen;
			bitoffset += readbitlen;

			readbitlen = ptr[8];
			introt[2] = animReadBits(framebytes, readbitlen, bitoffset);
			introt[2] += (ptr[6] << 8) + ptr[7];
			introt[2] <<= 16 - a.framelen;
			bitoffset += readbitlen;

			rot[0] = introt[0] * M_BADTAU / 65536.0f;
			rot[1] = introt[1] * M_BADTAU / 65536.0f;
			rot[2] = introt[2] * M_BADTAU / 65536.0f;
		} else if (flags & ANIMFIELD_F32_ROTATE) {
			for (i = 0; i < 3; i++) {
				u32 word = animReadBits(framebytes, 32, bitoffset);

				memcpy(&rot[i], &word, 4);
				bitoffset += 32;
			}
		}

		if (flags & ANIMFIELD_F32_SCALE) {
			for (i = 0; i < 3; i++) {
				u32 word = animReadBits(framebytes, 32, bitoffset);

				memcpy(&scale[i], &word, 4);
				bitoffset += 32;
			}
		}
	}

	return 1;
}

void gevrPdMtxRotTrans(const f32 rot[3], const f32 pos[3], f32 m[4][4])
{
	f32 xcos = cosf(rot[0]);
	f32 xsin = sinf(rot[0]);
	f32 ycos = cosf(rot[1]);
	f32 ysin = sinf(rot[1]);
	f32 zcos = cosf(rot[2]);
	f32 zsin = sinf(rot[2]);
	f32 a = xsin * zsin;
	f32 b = xcos * zsin;
	f32 c = xsin * zcos;
	f32 d = xcos * zcos;

	m[0][0] = ycos * zcos;
	m[0][1] = ycos * zsin;
	m[0][2] = -ysin;
	m[0][3] = 0;

	m[1][0] = c * ysin - xcos * zsin;
	m[1][1] = a * ysin + xcos * zcos;
	m[1][2] = xsin * ycos;
	m[1][3] = 0;

	m[2][0] = d * ysin + xsin * zsin;
	m[2][1] = b * ysin - xsin * zcos;
	m[2][2] = xcos * ycos;
	m[2][3] = 0;

	m[3][0] = pos[0];
	m[3][1] = pos[1];
	m[3][2] = pos[2];
	m[3][3] = 1;
}
