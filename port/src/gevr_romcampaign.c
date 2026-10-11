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
#include "gun.h"
#include "propobj.h"
#include "bg.h"
#include "bgfog.h"
#include "system.h"

extern fileentry file_resource_table[];
extern resource_lookup_data_entry resource_lookup_data_array[];
extern s32 file_entry_max;
extern u8 *g_RomFile;
extern GunModelFileRecord gitem_structs[];

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

/*
 * The skeletons a model header may name, by where GoldenEye's data segment
 * keeps them. The skeletons themselves are compiled into the port; a hack
 * reuses GoldenEye's (Goldfinger names no other), so its headers point at
 * these addresses. 0 is a head, which has none.
 */
extern struct ModelSkeleton skeleton_cctv, skeleton_console_one_screen, skeleton_console_four_screen,
	skeleton_tv_holder, skeleton_rotating_stuff, skeleton_eyelid_door, skeleton_iris_door,
	skeleton_walletbond, skeleton_car, skeleton_flying, skeleton_door, skeleton_tank, skeleton_hat,
	skeleton_standard_object, skeleton_prop_weapon, skeleton_suit_lf_hand, skeleton_standard_gun,
	skeleton_gun_revolver, skeleton_gun_kf7, skeleton_g_weapon, skeleton_guard;

static const struct {
	uint32_t vram;
	struct ModelSkeleton *skeleton;
} g_Skeletons[] = {
	{ 0x8003a05c, &skeleton_cctv },
	{ 0x8003a070, &skeleton_console_one_screen },
	{ 0x8003a084, &skeleton_console_four_screen },
	{ 0x8003a0b0, &skeleton_tv_holder },
	{ 0x8003a0e0, &skeleton_rotating_stuff },
	{ 0x8003a100, &skeleton_eyelid_door },
	{ 0x8003a15c, &skeleton_iris_door },
	{ 0x8003a170, &skeleton_walletbond },
	{ 0x8003a19c, &skeleton_car },
	{ 0x8003a1c8, &skeleton_flying },
	{ 0x8003a1dc, &skeleton_door },
	{ 0x8003a208, &skeleton_tank },
	{ 0x8003a21c, &skeleton_hat },
	{ 0x8003c4d8, &skeleton_standard_object },
	{ 0x8003c4fc, &skeleton_prop_weapon },
	{ 0x8003c6e4, &skeleton_suit_lf_hand },
	{ 0x8003c714, &skeleton_standard_gun },
	{ 0x8003c76c, &skeleton_gun_revolver },
	{ 0x8003c7ac, &skeleton_gun_kf7 },
	{ 0x8003d390, &skeleton_g_weapon },
	{ 0x8003d400, &skeleton_guard },
};

/*
 * The fields of the N64 ModelFileHeader at vram that a load needs before the
 * file lands: its skeleton, switch, matrix, record and texture counts and its
 * radius. The model converter (gevr_model.c) lays the file out from these
 * counts, so a header from another cartridge's model mis-converts it.
 */
static int readHeader(uint32_t vram, struct ModelFileHeader *dst)
{
	const uint8_t *h = atVram(vram, 0x18);
	uint32_t skel;
	size_t i;

	if (!h) {
		return 0;
	}
	skel = be32at(h + 4);
	dst->Skeleton = NULL;
	if (skel) {
		for (i = 0; i < sizeof(g_Skeletons) / sizeof(g_Skeletons[0]); i++) {
			if (g_Skeletons[i].vram == skel) {
				dst->Skeleton = g_Skeletons[i].skeleton;
				break;
			}
		}
		if (!dst->Skeleton) {
			sysLogPrintf(LOG_ERROR, "rom: header at %08x names skeleton %08x, not one of GoldenEye's",
					vram, skel);
			return 0;
		}
	}
	dst->numSwitches = (s16)be16at(h + 12);
	dst->numMatrices = (s16)be16at(h + 14);
	dst->BoundingVolumeRadius = bef32(h + 16);
	dst->numRecords = (s16)be16at(h + 20);
	dst->numtextures = (s16)be16at(h + 22);
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

		if (!readHeader(be32at(row), &headers[i])) {
			sysLogPrintf(LOG_ERROR, "rom: character %u (%s) has no usable header", i, name);
			free(records);
			free(headers);
			return 0;
		}
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

/*
 * PitemZ_entries and object_explosion_details, both by prop number. A hack's
 * own table may be longer than GoldenEye's 340 (Goldfinger's is 416, most of
 * them renamed) and every row's header is its own.
 */
static int installProps(const struct gevrRomLayout *L)
{
	struct ItemModelFileRecord *records;
	struct ModelFileHeader *headers;
	ExplosionDetailsRecord *explosions;
	const uint8_t *table;
	const uint8_t *blast;
	uint32_t i;

	if (!L->numprops || L->props < GEVR_DATA_VRAM) {
		return 0;
	}
	table = atVram(L->props, 12u * L->numprops);
	blast = atVram(L->explosions, 14u * L->numprops);
	if (!table || !blast) {
		return 0;
	}
	records = (struct ItemModelFileRecord *)calloc((size_t)L->numprops + 1, sizeof(*records));
	headers = (struct ModelFileHeader *)calloc(L->numprops, sizeof(*headers));
	explosions = (ExplosionDetailsRecord *)calloc(L->numprops, sizeof(*explosions));
	if (!records || !headers || !explosions) {
		free(records);
		free(headers);
		free(explosions);
		return 0;
	}
	for (i = 0; i < L->numprops; i++) {
		const uint8_t *row = table + 12u * i;
		const uint8_t *seeds = blast + 14u * i;
		int k;

		if (!readHeader(be32at(row), &headers[i])) {
			sysLogPrintf(LOG_ERROR, "rom: prop %u (%s) has no usable header", i, vramString(be32at(row + 4)));
			free(records);
			free(headers);
			free(explosions);
			return 0;
		}
		records[i].header = &headers[i];
		records[i].filename = (char *)vramString(be32at(row + 4));
		records[i].scale = bef32(row + 8);
		explosions[i].TypeID = (s16)be16at(seeds);
		for (k = 0; k < 6; k++) {
			explosions[i].Seed[k] = (s16)be16at(seeds + 2 + 2 * k);
		}
	}
	records[L->numprops].filename = "";
	records[L->numprops].scale = 1.0f;
	PitemZ_entries = records;
	object_explosion_details = explosions;
	g_GevrPropCount = (s32)L->numprops;
	sysLogPrintf(LOG_NOTE, "rom: %s props %u", L->tag, L->numprops);
	return 1;
}

/*
 * The hand items' headers. A hack keeps GoldenEye's gun file names but puts
 * its own models in them (Goldfinger's GtaserZ is its Luger), so each
 * compiled header takes the counts of the hack's row that names the same
 * file. Every loader copies these (gun.c, the watch arm, the weapon wheel,
 * held guns), so they all lay the hack's model out right.
 */
static int installItemHeaders(void)
{
	static struct ModelFileHeader hdrs[ITEM_IDS_MAX];
	static const char *names[ITEM_IDS_MAX];
	int k;
	int done = 0;

	/* Read every row first: the compiled headers are written only when all of them are good. */
	for (k = 1; k < ITEM_IDS_MAX; k++) {
		const uint8_t *row = atVram(GEVR_DATA_VRAM + GEVR_ITEMS_AT + GEVR_ITEM_ROW * (uint32_t)k, 8);

		names[k] = NULL;
		if (!row || !be32at(row) || !be32at(row + 4) || !vramString(be32at(row + 4))[0]) {
			continue;
		}
		memset(&hdrs[k], 0, sizeof(hdrs[k]));
		if (!readHeader(be32at(row), &hdrs[k])) {
			sysLogPrintf(LOG_ERROR, "rom: item %d (%s) has no usable header", k, vramString(be32at(row + 4)));
			return 0;
		}
		names[k] = vramString(be32at(row + 4));
	}
	for (k = 1; k < ITEM_IDS_MAX; k++) {
		int j;

		if (!names[k]) {
			continue;
		}
		for (j = 1; j < ITEM_IDS_MAX; j++) {
			ModelFileHeader *dst = gitem_structs[j].item_header;

			if (!dst || !gitem_structs[j].item_file_name
					|| strcmp(gitem_structs[j].item_file_name, names[k]) != 0) {
				continue;
			}
			dst->Skeleton = hdrs[k].Skeleton;
			dst->numSwitches = hdrs[k].numSwitches;
			dst->numMatrices = hdrs[k].numMatrices;
			dst->BoundingVolumeRadius = hdrs[k].BoundingVolumeRadius;
			dst->numRecords = hdrs[k].numRecords;
			dst->numtextures = hdrs[k].numtextures;
			done++;
		}
	}
	sysLogPrintf(LOG_NOTE, "rom: %d hand item headers from the hack", done);
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
		/* GEVR_ITEMS_AT is an offset into the data segment, not an address */
		const uint8_t *row = atVram(GEVR_DATA_VRAM + GEVR_ITEMS_AT + GEVR_ITEM_ROW * (uint32_t)item, 8);
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

/* the four casing models a gun's stats may name, by their data-segment address */
extern struct ModelFileHeader cartridge_header, cartrifle_header, cartblue_header, cartshell_header;

/*
 * The hand guns' stats. A hack keeps GoldenEye's rows in place and edits
 * them, and its item k is bound to the US item on k's gun file (installItems),
 * so row k's stats go into that item's: Goldfinger's Luger, on GtaserZ, fires
 * with the Luger's numbers rather than the taser's. 112 bytes on the
 * cartridge, one pointer (the casing it ejects).
 */
static void installItemStats(void)
{
	int k;
	int done = 0;

	for (k = 1; k < 32 && k < ITEM_IDS_MAX; k++) {
		const uint8_t *row = atVram(GEVR_DATA_VRAM + GEVR_ITEMS_AT + GEVR_ITEM_ROW * (uint32_t)k, 16);
		const int host = gevrRomItemHost(k);
		const uint8_t *s;
		WeaponStats *dst;
		uint32_t casing;

		if (!row || host <= 0 || host >= ITEM_IDS_MAX || !gitem_structs[host].item_weapon_stats) {
			continue;
		}
		s = atVram(be32at(row + 12), 0x70);
		if (!s) {
			continue;
		}
		dst = gitem_structs[host].item_weapon_stats;
		casing = be32at(s + 40);
		dst->MuzzleFlashExtension = bef32(s + 0);
		dst->PosX = bef32(s + 4);
		dst->PosY = bef32(s + 8);
		dst->PosZ = bef32(s + 12);
		dst->PlayX = bef32(s + 16);
		dst->PlayY = bef32(s + 20);
		dst->PlayZ = bef32(s + 24);
		dst->AmmoType = (s32)be32at(s + 28);
		dst->MagSize = (s16)be16at(s + 32);
		dst->AutomaticFiringRate = s[34];
		dst->SingleFiringRate = (s8)s[35];
		dst->ObjectsShootThrough = s[36];
		dst->SoundTriggerRate = s[37];
		dst->Sound = be16at(s + 38);
		dst->ptr_cartridge_struct = casing == 0x8003cb60 ? &cartridge_header
				: casing == 0x8003cb80 ? &cartrifle_header
				: casing == 0x8003cba0 ? &cartblue_header
				: casing == 0x8003cbc0 ? &cartshell_header : NULL;
		dst->DestructionAmount = bef32(s + 44);
		dst->Inaccuracy = bef32(s + 48);
		dst->Zoom = bef32(s + 52);
		dst->CrosshairSpeed = bef32(s + 56);
		dst->AimLockSpeed = bef32(s + 60);
		dst->Sway = bef32(s + 64);
		dst->RecoilSpeed = (s32)be32at(s + 68);
		dst->RecoilBack = bef32(s + 72);
		dst->RecoilUp = bef32(s + 76);
		dst->BoltRecoilBack = bef32(s + 80);
		dst->LoudnessMin = bef32(s + 84);
		dst->LoudnessMax = bef32(s + 88);
		dst->NoiseIncreasePerShot = bef32(s + 92);
		dst->NoiseDecayLinearTime = bef32(s + 96);
		dst->NoiseDecayScaledTime = bef32(s + 100);
		dst->ForceOfImpact = bef32(s + 104);
		dst->BitFlags = be32at(s + 108);
		done++;
	}
	sysLogPrintf(LOG_NOTE, "rom: %d gun stats rows from the hack", done);
}

/*
 * A row of the hack's data segment into a host row of the same layout, field
 * by field: '4' a word, '2' a halfword, '1' a byte (bgfog.h's records hold no
 * pointers, so the N64 and host layouts match).
 */
static void swapRow(uint8_t *dst, const uint8_t *src, const char *fields)
{
	for (; *fields; fields++) {
		if (*fields == '4') {
			uint32_t w = be32at(src);

			memcpy(dst, &w, 4);
			dst += 4;
			src += 4;
		} else if (*fields == '2') {
			uint16_t h = be16at(src);

			memcpy(dst, &h, 2);
			dst += 2;
			src += 2;
		} else {
			*dst++ = *src++;
		}
	}
}

/* rows up to and including the one whose first word is 0 (after the first row) */
static uint32_t hackRows(uint32_t at, uint32_t row, uint32_t cap)
{
	uint32_t n;

	for (n = 0; n < cap; n++) {
		const uint8_t *p = atVram(at + row * n, row);

		if (!p) {
			return 0;
		}
		if (n > 0 && be32at(p) == 0) {
			return n + 1;
		}
	}
	return 0;
}

#define GEVR_LEVELINFO_AT  0x8004448cu
#define GEVR_LEVELINFO_ROWS 38
#define GEVR_FOG_AT        0x80044e10u
#define GEVR_FOG_ROW       92u
#define GEVR_FOGLESS_ROW   56u
#define GEVR_MUSIC_AT      0x8004eb10u
#define GEVR_TARGETTIMES_AT 0x8002b564u
/* bgfog.h: SkyBoxRecord; EnvironmentRecord is Id, VisibilityRecord (7 words), FogRecord (2), the sky */
#define GEVR_SKY_FIELDS     "1111" "4" "22" "444" "1111" "4" "22" "444" "4"
#define GEVR_FOG_FIELDS     "4" "4444444" "44" GEVR_SKY_FIELDS
#define GEVR_FOGLESS_FIELDS "4" GEVR_SKY_FIELDS

static uint32_t fieldBytes(const char *fields)
{
	uint32_t n = 0;

	for (; *fields; fields++) {
		n += (uint32_t)(*fields - '0');
	}
	return n;
}

struct gevrMusicRow { s16 stage_id, main_music, bg_sound, xtrack; }; /* music_0D2720.c's struct music_setup */
extern struct levelentry levelinfotable[];
extern EnvironmentRecord fog_tables[];
extern EnvironmentFoglessRecord fog_tables2[];
extern struct gevrMusicRow music_setup_entries[];
extern s16 random_tracks[];

/*
 * The per-level tables GoldenEye compiles in, which a hack edits in its data
 * segment: each level's render scale, visibility and BG/clipping files
 * (Goldfinger's Dam is drawn at 1.0, GoldenEye's at 0.23), the fog and
 * fogless skies, the music and the cheats' target times. Same row counts as
 * GoldenEye's, so they are written in place; a table that would not fit is
 * left as GoldenEye's and logged.
 */
static void installLevelTables(void)
{
	const uint8_t *p;
	uint32_t i, rows, cap;

	for (i = 0; i < GEVR_LEVELINFO_ROWS; i++) {
		p = atVram(GEVR_LEVELINFO_AT + 24u * i, 24);
		if (!p || (s32)be32at(p) != levelinfotable[i].levelID) {
			sysLogPrintf(LOG_ERROR, "rom: level table row %u is not level %d; left as GoldenEye's",
					i, levelinfotable[i].levelID);
			continue;
		}
		if (vramString(be32at(p + 4))[0]) {
			levelinfotable[i].bg_seg_filename = (void *)vramString(be32at(p + 4));
		}
		if (vramString(be32at(p + 8))[0]) {
			levelinfotable[i].bg_stan_filename = (void *)vramString(be32at(p + 8));
		}
		levelinfotable[i].levelscale = bef32(p + 12);
		levelinfotable[i].visibility = bef32(p + 16);
		levelinfotable[i].unknownfloat = bef32(p + 20);
	}

	for (cap = 1; fog_tables[cap - 1].Id != 0 || cap == 1; cap++) {
	}
	rows = hackRows(GEVR_FOG_AT, GEVR_FOG_ROW, 256);
	if (fieldBytes(GEVR_FOG_FIELDS) != GEVR_FOG_ROW || sizeof(EnvironmentRecord) != GEVR_FOG_ROW
			|| fieldBytes(GEVR_FOGLESS_FIELDS) != GEVR_FOGLESS_ROW
			|| sizeof(EnvironmentFoglessRecord) != GEVR_FOGLESS_ROW) {
		sysLogPrintf(LOG_ERROR, "rom: fog record layout is not the N64's; fog left as GoldenEye's");
	} else if (rows && rows <= cap) {
		const uint32_t fogless = GEVR_FOG_AT + GEVR_FOG_ROW * rows;
		uint32_t cap2, rows2;

		for (i = 0; i < rows; i++) {
			swapRow((uint8_t *)&fog_tables[i], atVram(GEVR_FOG_AT + GEVR_FOG_ROW * i, GEVR_FOG_ROW), GEVR_FOG_FIELDS);
		}
		for (cap2 = 1; fog_tables2[cap2 - 1].Id != 0 || cap2 == 1; cap2++) {
		}
		rows2 = hackRows(fogless, GEVR_FOGLESS_ROW, 64);
		if (rows2 && rows2 <= cap2) {
			for (i = 0; i < rows2; i++) {
				swapRow((uint8_t *)&fog_tables2[i], atVram(fogless + GEVR_FOGLESS_ROW * i, GEVR_FOGLESS_ROW),
						GEVR_FOGLESS_FIELDS);
			}
		} else {
			sysLogPrintf(LOG_ERROR, "rom: fogless table has %u rows, room for %u", rows2, cap2);
		}
	} else {
		sysLogPrintf(LOG_ERROR, "rom: fog table has %u rows, room for %u", rows, cap);
	}

	for (cap = 0; music_setup_entries[cap].stage_id != 0; cap++) {
	}
	for (rows = 0; rows <= cap; rows++) {
		p = atVram(GEVR_MUSIC_AT + 8u * rows, 8);
		if (!p || be16at(p) == 0) {
			break;
		}
	}
	if (p && rows <= cap) {
		for (i = 0; i <= rows; i++) {
			p = atVram(GEVR_MUSIC_AT + 8u * i, 8);
			music_setup_entries[i].stage_id = (s16)be16at(p);
			music_setup_entries[i].main_music = (s16)be16at(p + 2);
			music_setup_entries[i].bg_sound = (s16)be16at(p + 4);
			music_setup_entries[i].xtrack = (s16)be16at(p + 6);
		}
		/* random_tracks follow the end row, ended by -1 */
		for (cap = 0; random_tracks[cap] != -1; cap++) {
		}
		for (i = 0; i <= cap; i++) {
			p = atVram(GEVR_MUSIC_AT + 8u * (rows + 1) + 2u * i, 2);
			if (!p) {
				break;
			}
			random_tracks[i] = (s16)be16at(p);
			if (random_tracks[i] == -1) {
				break;
			}
		}
		random_tracks[cap] = -1;
	} else {
		sysLogPrintf(LOG_ERROR, "rom: music table does not fit GoldenEye's %u rows", cap);
	}

	p = atVram(GEVR_TARGETTIMES_AT, sizeof(solo_target_time_array));
	if (p) {
		for (i = 0; i < 20 * 3; i++) {
			solo_target_time_array[i / 3][i % 3] = (s16)be16at(p + 2 * i);
		}
	}
	sysLogPrintf(LOG_NOTE, "rom: level scales, fog, music and target times from the hack");
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
		struct ItemModelFileRecord *savedProps = PitemZ_entries;
		ExplosionDetailsRecord *savedBlast = object_explosion_details;
		s32 savedPropCount = g_GevrPropCount;

		g_FileRowCount = gevrRomParseFiles(g_Window.bytes, g_Window.len, g_FileRows, GEVR_FILE_ROWS);
		g_FileRomLen = romLen;
		/*
		 * The file table and the hand items' compiled headers are written
		 * last, so a failure here leaves both retail.
		 */
		if (g_FileRowCount < 3 || !installImages(L) || !installCharacters(L) || !installProps(L)
				|| !installItemHeaders()) {
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
			if (PitemZ_entries != savedProps) {
				free(PitemZ_entries[0].header);
				free(PitemZ_entries);
				free(object_explosion_details);
				PitemZ_entries = savedProps;
				object_explosion_details = savedBlast;
				g_GevrPropCount = savedPropCount;
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
	installItemStats();
	installLevelTables();
	ok = 1;
	sysLogPrintf(LOG_NOTE, "rom: %s ready (%u bytes)", L->name, romLen);
	return ok;
}
