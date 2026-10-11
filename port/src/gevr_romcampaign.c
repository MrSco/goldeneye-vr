/*
 * Bind a Goldfinger 64 or Tomorrow Never Dies cartridge into the engine
 * tables. Retail US keeps the generated manifest; this runs only after
 * gevrRomLayoutMatchHeader has accepted the file.
 */

#include "gevr_romlayout.h"
#include "gevr_romcheck.h"

#include <stdlib.h>
#include <string.h>

#include "ob.h"
#include "image.h"
#include "chrobjdata.h"
#include "front.h"
#include "system.h"

extern fileentry file_resource_table[];
extern resource_lookup_data_entry resource_lookup_data_array[];
extern s32 file_entry_max;
extern u8 *g_RomFile;

#define GEVR_ITEMS_AT  0x12b94u
#define GEVR_ITEM_ROW  56u
#define GEVR_FOLDER_AT 0x8002abe4u
#define GEVR_FOLDER_ROW 28u
#define GEVR_FOLDER_CAP 48

static struct gevrDataWindow g_Window;
static char g_FolderLabel[GEVR_FOLDER_CAP][8];
static char g_FolderBrief[GEVR_FOLDER_CAP][24];

static uint32_t be32at(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t be16at(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static float bef32(const uint8_t *p)
{
	uint32_t bits = be32at(p);
	float f;

	memcpy(&f, &bits, 4);
	return f;
}

static const uint8_t *atVram(uint32_t vram, uint32_t need)
{
	return gevrRomDataBytes(&g_Window, vram, need);
}

static const char *vramString(uint32_t ptr)
{
	const uint8_t *s;
	uint32_t i;

	s = ptr >= GEVR_DATA_VRAM ? atVram(ptr, 1) : NULL;
	if (!s) {
		return "";
	}
	for (i = 0; i < 96; i++) {
		if (!atVram(ptr + i, 1)) {
			return "";
		}
		if (s[i] == 0) {
			return (const char *)s;
		}
	}
	return "";
}

static void copyText(char *dst, size_t dstLen, const char *src)
{
	size_t i;

	if (!dstLen) {
		return;
	}
	if (!src) {
		dst[0] = 0;
		return;
	}
	for (i = 0; i + 1 < dstLen && src[i]; i++) {
		dst[i] = src[i];
	}
	dst[i] = 0;
}

/*
 * The hack's own file table, kept: obInit() rebinds the table after
 * romdataInit has run, and must put these rows back rather than the retail
 * manifest's offsets (gevrRomBindFileTable).
 */
#define GEVR_FILE_ROWS 1024
static struct gevrRomFileRow g_FileRows[GEVR_FILE_ROWS];
static int g_FileRowCount;
static uint32_t g_FileRomLen;

/*
 * Each slot takes the hack's row whole: its address, its size and its name,
 * as the cartridge's own table gives them. Goldfinger renames 487 of 725, and
 * its code and data look files up by those names, so fileGetIndex finds them
 * in the table itself and a retail name the hack dropped is not found.
 */
static int bindFiles(void)
{
	int i;
	int bound = 0;

	for (i = 0; i < g_FileRowCount; i++) {
		const struct gevrRomFileRow *row = &g_FileRows[i];
		fileentry *slot;

		if (row->index == 0 || (s32)row->index >= file_entry_max) {
			continue;
		}
		slot = &file_resource_table[row->index];
		if (!row->size || (uint64_t)row->addr + row->size > g_FileRomLen || !g_RomFile) {
			slot->hw_address = NULL;
			resource_lookup_data_array[row->index].rom_size = 0;
			continue;
		}
		slot->hw_address = g_RomFile + row->addr;
		resource_lookup_data_array[row->index].rom_size = row->size;
		if (row->name && row->name[0]) {
			slot->filename = (char *)row->name;
		}
		bound++;
	}
	return bound;
}

int gevrRomRebindVariantFiles(void)
{
	int bound;

	if (!gevrRomIsVariant() || !g_FileRowCount) {
		return 0;
	}
	bound = bindFiles();
	sysLogPrintf(LOG_NOTE, "rom: %s file table bound, %d of %d rows",
			gevrRomActiveLayout()->tag, bound, g_FileRowCount);
	return 1;
}

static int installImages(const struct gevrRomLayout *L)
{
	struct image_entry *entries;
	const uint8_t *table;
	uint32_t i;
	uint32_t bytes;

	bytes = gevrRomImageBankBytes(g_Window.bytes, g_Window.len, L);
	if (!bytes) {
		return 0;
	}
	table = atVram(L->images, L->imagestride * L->numimages);
	if (!table) {
		return 0;
	}
	entries = (struct image_entry *)calloc((size_t)L->numimages + 1, sizeof(*entries));
	if (!entries) {
		return 0;
	}
	for (i = 0; i < L->numimages; i++) {
		uint32_t word = be32at(table + L->imagestride * i);
		uint32_t hitSound = 0;
		uint32_t hitTexture = 0;
		uint32_t size = 0;

		gevrRomDecodeImage(word, L->imagestride, L->imagemask, &hitSound, &hitTexture, &size);
		entries[i].hitSound = (u8)hitSound;
		entries[i].hitTexture = hitTexture;
		entries[i].dataoffset = size;
	}
	entries[L->numimages].dataoffset = 0xffff;
	gevrRomSetImageBankBytes(bytes);
	gevrTextureUse(entries, (s32)L->numimages);
	sysLogPrintf(LOG_NOTE, "rom: %s images %u, bank %u bytes", L->tag, L->numimages, bytes);
	return 1;
}

static int installCharacters(const struct gevrRomLayout *L)
{
	struct ChrModelFileRecord *records;
	struct ModelFileHeader *headers;
	const uint8_t *table;
	uint32_t i;

	if (!L->numchrs || L->chrs < GEVR_DATA_VRAM) {
		return 0;
	}
	table = atVram(L->chrs, 20u * L->numchrs);
	if (!table) {
		return 0;
	}
	records = (struct ChrModelFileRecord *)calloc((size_t)L->numchrs + 1, sizeof(*records));
	headers = (struct ModelFileHeader *)calloc(L->numchrs, sizeof(*headers));
	if (!records || !headers) {
		free(records);
		free(headers);
		return 0;
	}
	for (i = 0; i < L->numchrs; i++) {
		const uint8_t *row = table + 20u * i;
		uint32_t flags = be32at(row + 16);
		const char *name = vramString(be32at(row + 4));

		records[i].header = &headers[i];
		records[i].filename = (char *)name;
		records[i].scale = bef32(row + 8);
		records[i].pov = bef32(row + 12);
		records[i].isMale = (u8)((flags >> 24) & 1u);
		records[i].hasHead = (u8)((flags >> 16) & 1u);
	}
	records[L->numchrs].scale = 1.0f;
	c_item_entries = records;
	sysLogPrintf(LOG_NOTE, "rom: %s characters %u", L->tag, L->numchrs);
	return 1;
}

static void installFolder(void)
{
	const int limit = gevrRomMissionLimit();
	int n = 0;
	uint32_t row;

	memset(mission_folder_setup_entries, 0, sizeof(mission_folder_setup_entries));
	for (row = 0; n < GEVR_FOLDER_CAP - 1; row++) {
		const uint8_t *p = atVram(GEVR_FOLDER_AT + GEVR_FOLDER_ROW * row, GEVR_FOLDER_ROW);
		uint32_t strPtr;
		uint16_t folderText;
		int32_t type;
		int32_t mission;
		uint32_t briefPtr;
		const char *label;
		const char *brief;
		struct mission_folder_setup *dst;

		if (!p) {
			break;
		}
		strPtr = be32at(p);
		if (!strPtr) {
			break;
		}
		folderText = be16at(p + 4);
		type = (int32_t)be32at(p + 16);
		mission = (int32_t)be32at(p + 20);
		if (type == MISSION_PART && mission >= limit) {
			continue;
		}
		if (!folderText) {
			continue;
		}
		if (type != MISSION_PART && n > 0
				&& mission_folder_setup_entries[n - 1].type != MISSION_PART) {
			n--;
		}
		label = vramString(strPtr);
		briefPtr = be32at(p + 24);
		brief = NULL;
		if (type == MISSION_PART && briefPtr) {
			brief = vramString(briefPtr);
		}
		copyText(g_FolderLabel[n], sizeof(g_FolderLabel[n]), label);
		dst = &mission_folder_setup_entries[n];
		dst->string_ptr = g_FolderLabel[n];
		dst->folder_text_preset = folderText;
		dst->icon_text_preset = be16at(p + 6);
		dst->stage_id = (s32)be32at(p + 8);
		dst->unknown = (s32)be32at(p + 12);
		dst->type = type;
		dst->mission_num = type == MISSION_PART ? mission : -1;
		if (brief && brief[0]) {
			copyText(g_FolderBrief[n], sizeof(g_FolderBrief[n]), brief);
			dst->briefing_name_ptr = g_FolderBrief[n];
		}
		n++;
	}
	if (n > 0 && mission_folder_setup_entries[n - 1].type != MISSION_PART) {
		memset(&mission_folder_setup_entries[n - 1], 0, sizeof(mission_folder_setup_entries[0]));
		n--;
	}
	sysLogPrintf(LOG_NOTE, "rom: mission folder %d rows, limit %d", n, limit);
}

static void installItems(void)
{
	const char *names[32];
	int item;

	for (item = 0; item < 32; item++) {
		const uint8_t *row = atVram(GEVR_ITEMS_AT + GEVR_ITEM_ROW * (uint32_t)item, 8);
		uint32_t namePtr;
		const uint8_t *text;

		names[item] = NULL;
		if (!row) {
			continue;
		}
		namePtr = be32at(row + 4);
		text = (const uint8_t *)vramString(namePtr);
		if (text && text[0]) {
			names[item] = (const char *)text;
		}
	}
	gevrRomBuildItemMap(names, 32);
}

int gevrRomApplyVariant(const uint8_t *rom, uint32_t romLen)
{
	const struct gevrRomLayout *L = gevrRomActiveLayout();
	unsigned char *inflated = NULL;
	unsigned int inflatedLen = 0;
	int ok = 0;
	unsigned int cap;

	g_FileRowCount = 0;
	gevrRomClearItemMap();
	if (!rom || !L || !L->variant || romLen < GEVR_DATA_ROM + 3) {
		return 0;
	}
	for (cap = 2u * 1024u * 1024u; cap <= 8u * 1024u * 1024u; cap *= 2u) {
		free(inflated);
		inflated = (unsigned char *)malloc(cap);
		if (!inflated) {
			return 0;
		}
		if (gevrInflate1172(rom + GEVR_DATA_ROM, romLen - GEVR_DATA_ROM,
				inflated, cap, &inflatedLen) == 0) {
			break;
		}
		inflatedLen = 0;
	}
	if (!inflatedLen) {
		sysLogPrintf(LOG_ERROR, "rom: %s data segment did not inflate", L->name);
		free(inflated);
		return 0;
	}
	gevrRomDataFree(&g_Window);
	if (!gevrRomDataMount(&g_Window, inflated, inflatedLen, rom, romLen, L)) {
		sysLogPrintf(LOG_ERROR, "rom: %s expansion pieces did not fit", L->name);
		free(inflated);
		return 0;
	}
	free(inflated);
	inflated = NULL;

	{
		struct image_entry *savedTex = g_Textures;
		s32 savedCount = gevrTextureCount();
		struct ChrModelFileRecord *savedChr = c_item_entries;

		g_FileRowCount = gevrRomParseFiles(g_Window.bytes, g_Window.len, g_FileRows, GEVR_FILE_ROWS);
		g_FileRomLen = romLen;
		/* The file table is bound last, so a failure here leaves it retail. */
		if (g_FileRowCount < 3 || !installImages(L) || !installCharacters(L)) {
			if (g_Textures != savedTex) {
				struct image_entry *hack = g_Textures;

				gevrTextureUse(savedTex, savedCount);
				free(hack);
			}
			if (c_item_entries != savedChr && c_item_entries) {
				struct ModelFileHeader *headers = c_item_entries[0].header;

				free(c_item_entries);
				free(headers);
				c_item_entries = savedChr;
			}
			sysLogPrintf(LOG_ERROR, "rom: %s tables did not bind", L->name);
			g_FileRowCount = 0;
			gevrRomDataFree(&g_Window);
			gevrRomClearItemMap();
			return 0;
		}
	}
	gevrRomRebindVariantFiles();
	installFolder();
	installItems();
	ok = 1;
	sysLogPrintf(LOG_NOTE, "rom: %s ready (%u bytes)", L->name, romLen);
	return ok;
}
