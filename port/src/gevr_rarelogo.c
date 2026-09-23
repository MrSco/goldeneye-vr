/*
 * The spinning Rareware logo of the title sequence, read from the player's
 * cartridge (segment "rarewarelogo", mapped by gevr_romload.c).
 *
 * The decomp's assets/rarewarelogo.c holds the logo's textures and vertices
 * as C arrays; linking it would put Rare's artwork in the app, so the port
 * takes the same bytes out of the loaded ROM instead and puts them in host
 * form once:
 *   - vertices: big-endian 16-bit fields swapped (the Vtx layout is the same
 *     16 bytes on both sides);
 *   - display lists: rebuilt as host Gfx (a 64-bit second word), every
 *     segment-2 address turned into a pointer into the copy;
 *   - textures: left as they are - the renderer reads texels in cartridge
 *     byte order.
 * title.c draws with gevrRareLogo(<segment-2 offset>) in place of the
 * decomp's symbols (D_020043E8, DL_RAREWARETEXT, D_02004758, D_02004FE8,
 * D_02005FF0); the offsets are the symbols' own segment addresses, and
 * DL_RAREWARETEXT's (0x44B0) was measured by walking the cartridge lists.
 */

#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <PR/gbi.h>
#include "platform.h"
#include "system.h"

extern unsigned char *_rarewarelogoSegmentRomStart;
extern unsigned char *_rarewarelogoSegmentEnd;

#define LOGO_OP_VTX     0x04
#define LOGO_OP_DL      0x06
#define LOGO_OP_ENDDL   0xb8
#define LOGO_OP_SETTIMG 0xfd
#define LOGO_SEG        0x02
#define LOGO_MAX_DLS    8

static u8 *s_logo;              /* host copy of the segment */
static u32 s_logoSize;
static u8 *s_vtxDone;           /* one flag per 16-byte vertex slot */
static struct { u32 ofs; Gfx *host; } s_dls[LOGO_MAX_DLS];
static s32 s_numDls;

static Gfx *gevrRareLogoDl(u32 ofs);

static void *gevrRareLogoAddr(u32 w1)
{
	u32 ofs = w1 & 0x00ffffff;
	return ofs < s_logoSize ? (void *)(s_logo + ofs) : NULL;
}

static void gevrRareLogoVertices(u32 ofs, u32 count)
{
	u32 i;

	for (i = 0; i < count; i++) {
		u32 at = ofs + i * 16;
		u16 *h;
		s32 k;

		if (at + 16 > s_logoSize || s_vtxDone[at / 16]) {
			continue;
		}
		s_vtxDone[at / 16] = 1;
		/* ob[3], flag, tc[2]: six big-endian halves; the colour bytes stay */
		h = (u16 *)(s_logo + at);
		for (k = 0; k < 6; k++) {
			h[k] = PD_BE16(h[k]);
		}
	}
}

static Gfx *gevrRareLogoDl(u32 ofs)
{
	u32 n = 0, pos = ofs, i;
	Gfx *dl;
	s32 j;

	for (j = 0; j < s_numDls; j++) {
		if (s_dls[j].ofs == ofs) {
			return s_dls[j].host;
		}
	}
	while (pos + 8 <= s_logoSize) {
		n++;
		if (s_logo[pos] == LOGO_OP_ENDDL) {
			break;
		}
		pos += 8;
	}
	if (n == 0 || s_numDls == LOGO_MAX_DLS) {
		return NULL;
	}

	dl = (Gfx *)calloc(n, sizeof(Gfx));
	if (dl == NULL) {
		return NULL;
	}
	s_dls[s_numDls].ofs = ofs;
	s_dls[s_numDls].host = dl;
	s_numDls++;

	for (i = 0; i < n; i++) {
		const u8 *s = s_logo + ofs + i * 8;
		u32 w0 = PD_BE32(*(const u32 *)(s + 0));
		u32 w1 = PD_BE32(*(const u32 *)(s + 4));
		u8 op = (u8)(w0 >> 24);
		uintptr_t hw1 = w1;

		if ((w1 >> 24) == LOGO_SEG) {
			if (op == LOGO_OP_VTX) {
				gevrRareLogoVertices(w1 & 0x00ffffff, (w0 & 0xffff) / 16);
				hw1 = (uintptr_t)gevrRareLogoAddr(w1);
			} else if (op == LOGO_OP_SETTIMG) {
				hw1 = (uintptr_t)gevrRareLogoAddr(w1);
			} else if (op == LOGO_OP_DL) {
				hw1 = (uintptr_t)gevrRareLogoDl(w1 & 0x00ffffff);
			}
		}
		dl[i].words.w0 = w0;
		dl[i].words.w1 = hw1;
	}
	return dl;
}

/* Once, before the logo is first drawn (title.c setupRarewareLogoData). */
void gevrRareLogoLoad(void)
{
	u32 size;

	if (s_logo != NULL) {
		return;
	}
	size = (u32)(_rarewarelogoSegmentEnd - _rarewarelogoSegmentRomStart);
	if (_rarewarelogoSegmentRomStart == NULL || size == 0) {
		sysLogPrintf(LOG_ERROR, "rare logo: no cartridge segment");
		return;
	}
	s_logo = (u8 *)malloc(size);
	s_vtxDone = (u8 *)calloc(size / 16 + 1, 1);
	if (s_logo == NULL || s_vtxDone == NULL) {
		sysLogPrintf(LOG_ERROR, "rare logo: out of memory (%u bytes)", size);
		return;
	}
	memcpy(s_logo, _rarewarelogoSegmentRomStart, size);
	s_logoSize = size;
	sysLogPrintf(LOG_NOTE, "rare logo: %u bytes from the cartridge", size);
}

/*
 * The logo's display list or texture at a segment-2 offset. Display lists are
 * converted on first use; anything else is a pointer into the copy.
 */
void *gevrRareLogo(u32 ofs, s32 isdl)
{
	if (s_logo == NULL) {
		gevrRareLogoLoad();
		if (s_logo == NULL) {
			return NULL;
		}
	}
	if (isdl) {
		return gevrRareLogoDl(ofs);
	}
	return ofs < s_logoSize ? (void *)(s_logo + ofs) : NULL;
}
