/**
 * Loads the player's GoldenEye cartridge and points the engine's file table at it.
 *
 * How the engine finds a file
 * ---------------------------
 * ob.c looks a file up in file_resource_table, reads hw_address, and hands it
 * to romCopy(), which on this port is a memcpy. On the cartridge the linker
 * filled hw_address in; here the column is NULL (see
 * tools/gevr_make_runtime_file_table.py) and this file fills it at startup with
 * a pointer into the loaded ROM image, using the offsets in
 * port/src/gevr_rom_manifest.c.
 *
 * Nothing downstream changes. load_resource() still romCopy()s the bytes and
 * still hands them to decompressdata(), because the files are stored
 * 1172-compressed in the cartridge exactly as the engine expects.
 *
 * The ROM is kept resident. It is 12 MiB, which is nothing against the headset's
 * budget, and it means every load is a memcpy out of memory rather than a file
 * read mid-level.
 *
 * Replaces the Perfect Dark loader (port/src/romdata.c), which is written
 * against Perfect Dark's ROM ids, file table and rzip codec - see
 * CMakeLists.txt for why it is excluded.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>
#include "romdata.h"
#include "mod.h"
#include "fs.h"
#include "system.h"
#include "gevr_rom_manifest.h"
#include "gevr_romcheck.h"
#include "gevr_rom_segments.h"
#include "gevr_romlayout.h"
#include "gevr_texpack_preload.h"

extern int VrCampaign;
extern int VrGexGuns;

/* Mirrors of the engine's own table; ob.c defines both at file scope. */
struct gevrFileEntry {
	s32 index;
	char *filename;
	u8 *hw_address;
};

struct gevrLookupEntry {
	u32 rom_size;
	u32 poolRemaining;
	u32 pc_size;
	u32 rom_remaining;
	u8 loaded_bank;
	u8 unk_11;
	u16 reserved;
};

extern struct gevrFileEntry file_resource_table[];
extern struct gevrLookupEntry resource_lookup_data_array[];
extern s32 file_entry_max;   /* rows the compiler actually built */

u8 *g_RomFile = NULL;
u32 g_RomFileSize = 0;

#define GEVR_ROM_SIZE   12582912          /* 12 MiB, every retail GoldenEye cart */
#define GEVR_ROM_TITLE  "GOLDENEYE"       /* header name at 0x20                 */
#define GEVR_ROM_ID     "NGEE"            /* cartridge id at 0x3B, NTSC-U        */

/*
 * Where players actually put it. The first hit wins; fsFullPath() resolves
 * these against the platform's data directory, which on Android is the app's
 * external files dir.
 */
static const char *const gevrRomNames[] = {
	"ge.z64",
	"data/ge.z64",
	"goldeneye.z64",
	"007 - GoldenEye.z64",
	"GoldenEye 007 (U) [!].z64",
};

static s32 gevrRomValidate(const u8 *rom, u32 size)
{
	if (size != GEVR_ROM_SIZE) {
		sysLogPrintf(LOG_ERROR, "rom: wrong size: expected %u bytes, got %u.",
				(u32)GEVR_ROM_SIZE, size);
		return 0;
	}

	if (memcmp(rom + 0x3b, GEVR_ROM_ID, 4)) {
		sysLogPrintf(LOG_ERROR, "rom: cartridge id is '%.4s', expected '%s' (NTSC-U). "
				"PAL and JP cartridges are laid out differently.",
				(const char *)rom + 0x3b, GEVR_ROM_ID);
		return 0;
	}

	if (memcmp(rom + 0x20, GEVR_ROM_TITLE, sizeof(GEVR_ROM_TITLE) - 1)) {
		sysLogPrintf(LOG_ERROR, "rom: header name is not %s.", GEVR_ROM_TITLE);
		return 0;
	}

	return 1;
}

/*
 * Point every table entry at its bytes inside the loaded ROM.
 *
 * Both fields are written here rather than leaving obInit() to derive rom_size
 * from the gap to the next entry: the table is shared across regions, so a US
 * cartridge has no data for the PAL-only levels and those gaps would be
 * meaningless. Entries with no data keep a NULL hw_address, which is the state
 * ob.c already handles.
 */
void gevrRomBindFileTable(void)
{
	u32 i;
	u32 bound = 0;

	if (!g_RomFile) {
		return;
	}

	/* A hack campaign's rows come from its own cartridge table, not the manifest. */
	if (gevrRomRebindVariantFiles()) {
		return;
	}

	/*
	 * The manifest carries file_resource_table's own #ifdef VERSION_EU guards,
	 * so the two arrays are the same length by construction. Bound the loop by
	 * the table anyway: when they did disagree this wrote 44 entries off the end
	 * of both arrays, and the damage surfaced far away as a name lookup failing.
	 */
	if (g_GevrRomFileCount != (u32)file_entry_max) {
		sysLogPrintf(LOG_ERROR, "rom: manifest has %u rows but the file table has %d - "
				"regenerate with tools/gevr_gen_rom_manifest.py",
				g_GevrRomFileCount, file_entry_max);
	}

	for (i = 0; i < g_GevrRomFileCount && i < (u32)file_entry_max; i++) {
		const struct gevrRomFile *f = &g_GevrRomFiles[i];

		if (!f->offset || !f->size) {
			continue;
		}

		if (f->offset + f->size > g_RomFileSize) {
			sysLogPrintf(LOG_ERROR, "rom: file %u runs past the end of the ROM "
					"(0x%08X + %u).", i, f->offset, f->size);
			continue;
		}

		file_resource_table[i].hw_address = g_RomFile + f->offset;
		resource_lookup_data_array[i].rom_size = f->size;
		bound++;
	}

	sysLogPrintf(LOG_NOTE, "rom: bound %u of %u files.", bound, g_GevrRomFileCount);
}

static const char *const gevrGoldfingerNames[] = {
	GEVR_ROM_GF_FILE,
	"data/" GEVR_ROM_GF_FILE,
};

static const char *const gevrTndNames[] = {
	GEVR_ROM_TND_FILE,
	"data/" GEVR_ROM_TND_FILE,
};

static const char *const gevrMirrorNames[] = {
	GEVR_ROM_MIRROR_FILE,
	"data/" GEVR_ROM_MIRROR_FILE,
};

static void *gevrLoadNamed(const char *const *names, u32 count, u32 *size)
{
	u32 i;

	for (i = 0; i < count; i++) {
		void *data = fsFileLoad(names[i], size);
		if (data) {
			sysLogPrintf(LOG_NOTE, "rom: loaded %s (%u bytes)", names[i], *size);
			return data;
		}
	}
	return NULL;
}

s32 romdataInit(void)
{
	u32 size = 0;
	void *data = NULL;
	u32 i;

	if (g_RomFile) {
		return 0;
	}

	if (VrCampaign < GEVR_CAMPAIGN_GE || VrCampaign >= GEVR_CAMPAIGN_COUNT) {
		VrCampaign = GEVR_CAMPAIGN_GE;
	}
	gevrRomCampaignSet(VrCampaign);
	gevrRomClearItemMap();
	gevrRomSetImageBankBytes(0);

	if (gevrRomIsVariant()) {
		const char *const *names;
		u32 nameCount;
		switch (gevrRomCampaign()) {
		case GEVR_CAMPAIGN_GF64:
			names = gevrGoldfingerNames;
			nameCount = sizeof(gevrGoldfingerNames) / sizeof(gevrGoldfingerNames[0]);
			break;
		case GEVR_CAMPAIGN_TND64:
			names = gevrTndNames;
			nameCount = sizeof(gevrTndNames) / sizeof(gevrTndNames[0]);
			break;
		default: /* GEVR_CAMPAIGN_MIRROR */
			names = gevrMirrorNames;
			nameCount = sizeof(gevrMirrorNames) / sizeof(gevrMirrorNames[0]);
			break;
		}
		const struct gevrRomLayout *want = gevrRomActiveLayout();

		data = gevrLoadNamed(names, nameCount, &size);
		if (data && gevrRomNormaliseOrder((u8 *)data, size)) {
			const struct gevrRomLayout *got = gevrRomLayoutMatchHeader((const u8 *)data, size);

			g_RomFile = (u8 *)data;
			g_RomFileSize = size;
			if (got == want && gevrRomApplyVariant((const u8 *)data, size)) {
				/*
				 * GoldenEye X's models replace GoldenEye's guns by item and
				 * its textures take ids from 3100, inside Goldfinger's 3915:
				 * off for this run. The saved setting stays (vr_settings.cpp).
				 */
				if (VrGexGuns) {
					VrGexGuns = 0;
					gevrGexCampaignOff = 1;
					sysLogPrintf(LOG_NOTE, "rom: GoldenEye X models are off in %s", want->name);
				}
				gevrRomBindSegments();
				return 0;
			}
			g_RomFile = NULL;
			g_RomFileSize = 0;
		}
		sysLogPrintf(LOG_ERROR, "rom: %s is not installed or did not bind. Starting GoldenEye.",
				want ? want->name : "hack");
		{
			/* The launcher says so next time (vr_launcher.cpp). */
			FILE *f = fsFileOpenWrite(GEVR_CAMPAIGN_FAILED_FILE);

			if (f) {
				fprintf(f, "%s did not load, so GoldenEye started instead.", want ? want->name : "The campaign");
				fclose(f);
			}
		}
		free(data);
		data = NULL;
		gevrRomCampaignSet(GEVR_CAMPAIGN_GE);
		VrCampaign = GEVR_CAMPAIGN_GE;
		gevrRomClearItemMap();
		gevrRomSetImageBankBytes(0);
	}

	for (i = 0; i < sizeof(gevrRomNames) / sizeof(gevrRomNames[0]); i++) {
		data = fsFileLoad(gevrRomNames[i], &size);
		if (data) {
			sysLogPrintf(LOG_NOTE, "rom: loaded %s (%u bytes)", gevrRomNames[i], size);
			break;
		}
	}

	if (!data) {
		sysLogPrintf(LOG_ERROR,
				"rom: no GoldenEye cartridge found. Put an NTSC-U dump you own "
				"next to the game as 'ge.z64'.");
		return -1;
	}

	if (!gevrRomNormaliseOrder((u8 *)data, size)) {
		sysLogPrintf(LOG_ERROR, "rom: not an N64 cartridge image (bad magic).");
		free(data);
		return -1;
	}

	if (!gevrRomValidate((const u8 *)data, size)) {
		free(data);
		return -1;
	}

	char error[192];
	if (!gevrRomCheckFiles(data, size, error, sizeof(error))) {
		sysLogPrintf(LOG_ERROR, "rom: %s", error);
		free(data);
		return -1;
	}

	g_RomFile = (u8 *)data;
	g_RomFileSize = size;

	gevrRomBindFileTable();
	gevrRomBindSegments();

	return 0;
}

const char *romdataFileGetName(s32 fileNum)
{
	if (fileNum < 0 || (u32)fileNum >= g_GevrRomFileCount) {
		return NULL;
	}

	return file_resource_table[fileNum].filename;
}

s32 romdataFileGetNumForName(const char *name)
{
	u32 i;

	if (!name) {
		return -1;
	}

	for (i = 0; i < g_GevrRomFileCount; i++) {
		const char *f = file_resource_table[i].filename;
		if (f && !strcmp(f, name)) {
			return (s32)i;
		}
	}

	return -1;
}

s32 romdataCheckGbcRom(void)
{
	/* The GB pak / Cheat cartridge check is Perfect Dark's; GoldenEye has none. */
	return 0;
}

s32 modConfigLoad(const char *path)
{
	/* The mod loader is indexed by Perfect Dark's stage table. */
	(void)path;
	return 0;
}

/* ---------------------------------------------------- cartridge segments */

extern const struct gevrRomFile g_GevrRomSegments[];

unsigned char *_animation_dataSegmentRomStart;
unsigned char *_animation_dataSegmentStart;
unsigned char *_animation_dataSegmentEnd;
unsigned char *_animation_entriesSegmentRomStart;
unsigned char *_GlobalimagetableSegmentRomStart;
unsigned char *_GlobalimagetableSegmentStart;
unsigned char *_GlobalimagetableSegmentEnd;
unsigned char *_rarewarelogoSegmentRomStart;
unsigned char *_rarewarelogoSegmentStart;
unsigned char *_rarewarelogoSegmentEnd;
unsigned char *_fontdlSegmentRomStart;
unsigned char *_fontdlSegmentRomEnd;
unsigned char *_jfontchardataSegmentRomStart;
unsigned char *_efontchardataSegmentRomStart;
unsigned char *_fontbankgothicSegmentRomStart;
unsigned char *_fontbankgothicSegmentStart;
unsigned char *_fontbankgothicSegmentEnd;
unsigned char *_fontzurichboldSegmentRomStart;
unsigned char *_fontzurichboldSegmentStart;
unsigned char *_fontzurichboldSegmentEnd;
unsigned char *_imagesSegmentRomStart;
unsigned char *_bssSegmentEnd;
void *unknown2 = NULL;
void *unknown2_end = NULL;

/*
 * The game's heap. On hardware it ran from the end of BSS up to the TLB scratch
 * block near the top of RAM, and boss.c still measures it that way:
 *
 *     start = &_bssSegmentEnd;
 *     mempCheckMemflagTokens(start, tlbmanageGetTlbAllocatedBlock() - start);
 *
 * So one region is allocated with the scratch block carved off the top, which
 * keeps that subtraction meaning what it meant on the cartridge.
 */
#define GEVR_HEAP_SIZE     (16 * 1024 * 1024)
#define GEVR_TLB_BLOCK_SIZE 0x2000

static unsigned char *gevrHeap;

unsigned char *gevrHeapTlbBlock(void)
{
	return gevrHeap ? gevrHeap + GEVR_HEAP_SIZE : NULL;
}

static int gevrVariantSeg(s32 seg, u32 *off, u32 *size)
{
	const struct gevrRomLayout *L = gevrRomActiveLayout();

	if (!gevrRomIsVariant() || !L) {
		return 0;
	}
	switch (seg) {
	case GEVR_SEG_ANIMATION_DATA:
		if (!L->animdata || !L->globalimages || L->globalimages <= L->animdata) {
			return 0;
		}
		*off = L->animdata;
		*size = L->globalimages - L->animdata;
		return 1;
	case GEVR_SEG_GLOBALIMAGETABLE:
		if (!L->globalimages || !L->globalbytes) {
			return 0;
		}
		*off = L->globalimages;
		*size = L->globalbytes;
		return 1;
	case GEVR_SEG_FONTBANKGOTHIC:
		*off = L->raw[0][0];
		*size = L->raw[0][1];
		return *size != 0;
	case GEVR_SEG_FONTZURICHBOLD:
		*off = L->raw[1][0];
		*size = L->raw[1][1];
		return *size != 0;
	case GEVR_SEG_IMAGES:
		*off = L->imagesrom;
		*size = gevrRomGetImageBankBytes();
		return *off != 0 && *size != 0;
	case GEVR_SEG_GUNBARREL:
		*off = L->raw[7][0];
		*size = L->raw[7][1];
		return *size != 0;
	case GEVR_SEG_RAREWARELOGO:
		/* GoldenEye's logo, where the hack moved it (TND64); its size is GoldenEye's */
		*off = L->rarelogo;
		*size = g_GevrRomSegments[seg].size;
		return *off != 0;
	default:
		return 0;
	}
}

static u8 *gevrSegPtr(s32 seg)
{
	u32 off = 0;
	u32 size = 0;
	const struct gevrRomFile *f = &g_GevrRomSegments[seg];

	if (gevrVariantSeg(seg, &off, &size)) {
		if (!g_RomFile || off + size > g_RomFileSize) {
			return NULL;
		}
		return g_RomFile + off;
	}
	if (!g_RomFile || !f->size || f->offset + f->size > g_RomFileSize) {
		return NULL;
	}
	return g_RomFile + f->offset;
}

static u32 gevrSegSize(s32 seg)
{
	u32 off = 0;
	u32 size = 0;

	if (gevrVariantSeg(seg, &off, &size)) {
		return size;
	}
	return g_GevrRomSegments[seg].size;
}

void gevrRomBindSegments(void)
{
	if (!gevrHeap) {
		gevrHeap = (unsigned char *)calloc(1, GEVR_HEAP_SIZE + GEVR_TLB_BLOCK_SIZE);
		if (!gevrHeap) {
			sysFatalError("Could not allocate the %u byte game heap.",
					(u32)(GEVR_HEAP_SIZE + GEVR_TLB_BLOCK_SIZE));
		}
	}

	_bssSegmentEnd = gevrHeap;
	sysLogPrintf(LOG_NOTE, "game heap at %p - %p (memp banks live here, not in the main.c heap)", (void *)gevrHeap, (void *)(gevrHeap + GEVR_HEAP_SIZE));

	_animation_dataSegmentRomStart = gevrSegPtr(GEVR_SEG_ANIMATION_DATA);
	_animation_dataSegmentStart    = _animation_dataSegmentRomStart;
	_animation_dataSegmentEnd      = _animation_dataSegmentStart
			+ gevrSegSize(GEVR_SEG_ANIMATION_DATA);

	_animation_entriesSegmentRomStart = gevrSegPtr(GEVR_SEG_ANIMATION_ENTRIES);

	_GlobalimagetableSegmentRomStart = gevrSegPtr(GEVR_SEG_GLOBALIMAGETABLE);
	_GlobalimagetableSegmentStart    = _GlobalimagetableSegmentRomStart;
	_GlobalimagetableSegmentEnd      = _GlobalimagetableSegmentStart
			+ gevrSegSize(GEVR_SEG_GLOBALIMAGETABLE);

	_rarewarelogoSegmentRomStart = gevrSegPtr(GEVR_SEG_RAREWARELOGO);
	_rarewarelogoSegmentStart    = _rarewarelogoSegmentRomStart;
	_rarewarelogoSegmentEnd      = _rarewarelogoSegmentStart
			+ gevrSegSize(GEVR_SEG_RAREWARELOGO);

	_fontdlSegmentRomStart = gevrSegPtr(GEVR_SEG_FONTDL);
	_fontdlSegmentRomEnd   = _fontdlSegmentRomStart + gevrSegSize(GEVR_SEG_FONTDL);

	_jfontchardataSegmentRomStart = gevrSegPtr(GEVR_SEG_JFONTCHARDATA);
	_efontchardataSegmentRomStart = gevrSegPtr(GEVR_SEG_EFONTCHARDATA);

	_fontbankgothicSegmentRomStart = gevrSegPtr(GEVR_SEG_FONTBANKGOTHIC);
	_fontbankgothicSegmentStart    = _fontbankgothicSegmentRomStart;
	_fontbankgothicSegmentEnd      = _fontbankgothicSegmentStart
			+ gevrSegSize(GEVR_SEG_FONTBANKGOTHIC);

	_fontzurichboldSegmentRomStart = gevrSegPtr(GEVR_SEG_FONTZURICHBOLD);
	_fontzurichboldSegmentStart    = _fontzurichboldSegmentRomStart;
	_fontzurichboldSegmentEnd      = _fontzurichboldSegmentStart
			+ gevrSegSize(GEVR_SEG_FONTZURICHBOLD);

	/* Start warming legal/menu text as soon as the ROM is resident, before
	 * music binding and stage initialization. Legal-page font goes first. */
	gevrTexpackPreloadFont(_fontzurichboldSegmentStart, gevrSegSize(GEVR_SEG_FONTZURICHBOLD));
	gevrTexpackPreloadFont(_fontbankgothicSegmentStart, gevrSegSize(GEVR_SEG_FONTBANKGOTHIC));

	_imagesSegmentRomStart = gevrSegPtr(GEVR_SEG_IMAGES);
	unknown2 = gevrSegPtr(GEVR_SEG_GUNBARREL);
	unknown2_end = (u8 *)unknown2 + gevrSegSize(GEVR_SEG_GUNBARREL);
	gevrTexpackPreloadBackground((const u8 *)unknown2, gevrSegSize(GEVR_SEG_GUNBARREL));

	gevrRomBindMusic();
}

/*
 * Called when the engine asks for a file the loader never bound. Says which
 * entry it was and what the manifest thinks of it, so the gap can be traced
 * back to either a missing row or an index that does not line up.
 */
void gevrReportUnboundFile(void *entry, s32 index)
{
	struct gevrFileEntry *e = (struct gevrFileEntry *)entry;
	const char *name = (e && e->filename) ? e->filename : "(null)";

	if (index >= 0 && (u32)index < g_GevrRomFileCount) {
		const struct gevrRomFile *f = &g_GevrRomFiles[index];
		sysLogPrintf(LOG_ERROR,
				"rom: '%s' unbound at index %d - manifest says offset 0x%08X size %u, "
				"table hw_address %p",
				name, index, f->offset, f->size, (void *)e->hw_address);
	} else {
		sysLogPrintf(LOG_ERROR, "rom: '%s' unbound at index %d, which is outside the "
				"manifest (%u rows)", name, index, g_GevrRomFileCount);
	}
}
