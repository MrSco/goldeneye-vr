/**
 * Bind GoldenEye musicfiles from the player's ROM.
 *
 * music.c sizes banks from symbol deltas and romCopy's the ctl into an AL
 * heap before alBnkfNew. On this port the five segment symbols are ordinary
 * pointers: ctl banks are host-endian copies (via preprocessALBankFile),
 * sample tables stay mapped on the ROM image, and the music track table is a
 * host-endian copy of music.sbk.
 */

#include <stdint.h>
#include <string.h>
#include <ultra64.h>

#include "gevr_rom_segments.h"
#include "preprocess.h"
#include "system.h"

/* USA filelist.u.csv musicfiles layout */
#define GEVR_SFX_CTL_OFF           0x002EBDE0u
#define GEVR_SFX_CTL_SIZE              23488u
#define GEVR_SFX_TBL_OFF           0x002F19A0u
#define GEVR_SFX_TBL_SIZE             797360u
#define GEVR_INST_CTL_OFF          0x003B4450u
#define GEVR_INST_CTL_SIZE             17312u
#define GEVR_INST_TBL_OFF          0x003B87F0u
#define GEVR_INST_TBL_SIZE            397216u
#define GEVR_MUSIC_SBK_OFF         0x00419790u
#define GEVR_MUSIC_SBK_SIZE           126667u

unsigned char *_sfxctlSegmentRomStart;
unsigned char *_sfxctlSegmentRomEnd;
unsigned char *_sfxtblSegmentRomStart;
unsigned char *_sfxtblSegmentRomEnd;
unsigned char *_instrumentsctlSegmentRomStart;
unsigned char *_instrumentsctlSegmentRomEnd;
unsigned char *_instrumentstblSegmentRomStart;
unsigned char *_instrumentstblSegmentRomEnd;
unsigned char *_musicsampletblSegmentRomStart;
unsigned char *_musicsampletblSegmentRomEnd;

extern u8 *g_RomFile;
extern u32 g_RomFileSize;

static u8 *gevrDupRom(u32 offset, u32 size)
{
	u8 *dst;

	if (!g_RomFile || offset + size > g_RomFileSize) {
		sysFatalError("music bank 0x%08X+%u outside ROM (%u)", offset, size, g_RomFileSize);
	}
	dst = (u8 *)sysMemAlloc(size);
	if (!dst) {
		sysFatalError("out of memory for music bank 0x%08X (%u)", offset, size);
	}
	memcpy(dst, g_RomFile + offset, size);
	return dst;
}

static void gevrSwapMusicTable(u8 *sbk, u32 size)
{
	u16 count;
	u32 i;
	u8 *p;

	if (size < 4) {
		return;
	}
	count = (u16)((sbk[0] << 8) | sbk[1]);
	sbk[0] = (u8)(count & 0xff);
	sbk[1] = (u8)(count >> 8);
	p = sbk + 4;
	for (i = 0; i < count; i++) {
		u32 addr;
		u16 raw, packed;
		if ((u32)(p + 8 - sbk) > size) {
			break;
		}
		addr = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
		raw = (u16)((p[4] << 8) | p[5]);
		packed = (u16)((p[6] << 8) | p[7]);
		p[0] = (u8)(addr);
		p[1] = (u8)(addr >> 8);
		p[2] = (u8)(addr >> 16);
		p[3] = (u8)(addr >> 24);
		p[4] = (u8)(raw);
		p[5] = (u8)(raw >> 8);
		p[6] = (u8)(packed);
		p[7] = (u8)(packed >> 8);
		p += 8;
	}
}

void gevrRomBindMusic(void)
{
	u32 outSize;
	u8 *raw;
	u8 *host;

	raw = gevrDupRom(GEVR_SFX_CTL_OFF, GEVR_SFX_CTL_SIZE);
	host = preprocessALBankFile(raw, GEVR_SFX_CTL_SIZE, &outSize);
	sysMemFree(raw);
	_sfxctlSegmentRomStart = host;
	_sfxctlSegmentRomEnd = host + outSize;

	_sfxtblSegmentRomStart = g_RomFile + GEVR_SFX_TBL_OFF;
	_sfxtblSegmentRomEnd = _sfxtblSegmentRomStart + GEVR_SFX_TBL_SIZE;

	raw = gevrDupRom(GEVR_INST_CTL_OFF, GEVR_INST_CTL_SIZE);
	host = preprocessALBankFile(raw, GEVR_INST_CTL_SIZE, &outSize);
	sysMemFree(raw);
	_instrumentsctlSegmentRomStart = host;
	_instrumentsctlSegmentRomEnd = host + outSize;

	_instrumentstblSegmentRomStart = g_RomFile + GEVR_INST_TBL_OFF;
	_instrumentstblSegmentRomEnd = _instrumentstblSegmentRomStart + GEVR_INST_TBL_SIZE;

	raw = gevrDupRom(GEVR_MUSIC_SBK_OFF, GEVR_MUSIC_SBK_SIZE);
	gevrSwapMusicTable(raw, GEVR_MUSIC_SBK_SIZE);
	_musicsampletblSegmentRomStart = raw;
	_musicsampletblSegmentRomEnd = raw + GEVR_MUSIC_SBK_SIZE;

	sysLogPrintf(LOG_NOTE,
			"music: banks bound (sfx ctl host %u, instruments ctl host %u, tracks %u)",
			(u32)(_sfxctlSegmentRomEnd - _sfxctlSegmentRomStart),
			(u32)(_instrumentsctlSegmentRomEnd - _instrumentsctlSegmentRomStart),
			GEVR_MUSIC_SBK_SIZE);
}
