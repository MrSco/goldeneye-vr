/*
 * Runs gevr_romcampaign.c's binder on a real hack ROM, against stand-ins for
 * the engine tables it writes, and prints what it bound. test_romcampaign.py
 * decodes the same ROM on its own and compares. No ROM data is kept here.
 *
 * usage: romcampaign_native <hack.z64> <campaign 1|2>
 */
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "chrobjdata.h"
#include "image.h"
#include "front.h"
#include "gun.h"
#include "propobj.h"
#include "bg.h"
#include "bgfog.h"
#include "ob.h"
#include "gevr_romlayout.h"
#include "gevr_romcheck.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- what the binder writes, shaped as the game's ------------------------- */

fileentry file_resource_table[1024];
resource_lookup_data_entry resource_lookup_data_array[1024];
s32 file_entry_max = 727; /* GoldenEye (US)'s table */
u8 *g_RomFile;
u32 g_RomFileSize;

static struct ModelFileHeader s_itemHeaders[ITEM_IDS_MAX];
GunModelFileRecord gitem_structs[ITEM_IDS_MAX];

static struct ChrModelFileRecord s_retailChrs[2];
struct ChrModelFileRecord *c_item_entries = s_retailChrs;
static struct ItemModelFileRecord s_retailProps[2];
struct ItemModelFileRecord *PitemZ_entries = s_retailProps;
static ExplosionDetailsRecord s_retailBlast[2];
ExplosionDetailsRecord *object_explosion_details = s_retailBlast;
s32 g_GevrPropCount = 1;

static struct image_entry s_retailImages[2] = { { 0 }, { 0, 0, 0xffff } };
struct image_entry *g_Textures = s_retailImages;
static s32 s_textureCount = 1;
s32 gevrTextureCount(void) { return s_textureCount; }
void gevrTextureUse(struct image_entry *entries, s32 count) { g_Textures = entries; s_textureCount = count; }

struct mission_folder_setup mission_folder_setup_entries[48];
s16 solo_target_time_array[20][3];

/* bg.c's row order */
static const s32 s_levelIds[38] = {
	LEVELID_BUNKER1, LEVELID_SILO, LEVELID_STATUE, LEVELID_CONTROL, LEVELID_ARCHIVES, LEVELID_TRAIN,
	LEVELID_FRIGATE, LEVELID_BUNKER2, LEVELID_AZTEC, LEVELID_STREETS, LEVELID_DEPOT, LEVELID_COMPLEX,
	LEVELID_EGYPT, LEVELID_DAM, LEVELID_FACILITY, LEVELID_RUNWAY, LEVELID_SURFACE, LEVELID_JUNGLE,
	LEVELID_TEMPLE, LEVELID_CAVERNS, LEVELID_CITADEL, LEVELID_CRADLE, LEVELID_SHO, LEVELID_SURFACE2,
	LEVELID_ELD, LEVELID_BASEMENT, LEVELID_STACK, LEVELID_LUE, LEVELID_LIBRARY, LEVELID_RIT,
	LEVELID_CAVES, LEVELID_EAR, LEVELID_LEE, LEVELID_LIP, LEVELID_CUBA, LEVELID_WAX, LEVELID_PAM, LEVELID_MAX,
};
struct levelentry levelinfotable[38];
EnvironmentRecord fog_tables[48];          /* GoldenEye's 47 and the end row */
EnvironmentFoglessRecord fog_tables2[4];   /* 3 and the end row */
struct gevrTestMusicRow { s16 stage_id, main_music, bg_sound, xtrack; } music_setup_entries[25];
s16 random_tracks[52];

struct ModelSkeleton skeleton_cctv, skeleton_console_one_screen, skeleton_console_four_screen,
	skeleton_tv_holder, skeleton_rotating_stuff, skeleton_eyelid_door, skeleton_iris_door,
	skeleton_walletbond, skeleton_car, skeleton_flying, skeleton_door, skeleton_tank, skeleton_hat,
	skeleton_standard_object, skeleton_prop_weapon, skeleton_suit_lf_hand, skeleton_standard_gun,
	skeleton_gun_revolver, skeleton_gun_kf7, skeleton_g_weapon, skeleton_guard;

static const struct { struct ModelSkeleton *s; const char *name; } s_skeletonNames[] = {
	{ &skeleton_cctv, "cctv" }, { &skeleton_console_one_screen, "console_one_screen" },
	{ &skeleton_console_four_screen, "console_four_screen" }, { &skeleton_tv_holder, "tv_holder" },
	{ &skeleton_rotating_stuff, "rotating_stuff" }, { &skeleton_eyelid_door, "eyelid_door" },
	{ &skeleton_iris_door, "iris_door" }, { &skeleton_walletbond, "walletbond" }, { &skeleton_car, "car" },
	{ &skeleton_flying, "flying" }, { &skeleton_door, "door" }, { &skeleton_tank, "tank" },
	{ &skeleton_hat, "hat" }, { &skeleton_standard_object, "standard_object" },
	{ &skeleton_prop_weapon, "prop_weapon" }, { &skeleton_suit_lf_hand, "suit_lf_hand" },
	{ &skeleton_standard_gun, "standard_gun" }, { &skeleton_gun_revolver, "gun_revolver" },
	{ &skeleton_gun_kf7, "gun_kf7" }, { &skeleton_g_weapon, "g_weapon" }, { &skeleton_guard, "guard" },
};

static const char *skeletonName(const struct ModelSkeleton *s)
{
	size_t i;

	if (!s) {
		return "none";
	}
	for (i = 0; i < sizeof(s_skeletonNames) / sizeof(s_skeletonNames[0]); i++) {
		if (s_skeletonNames[i].s == s) {
			return s_skeletonNames[i].name;
		}
	}
	return "?";
}

void sysLogPrintf(s32 level, const char *fmt, ...)
{
	va_list ap;

	(void)level;
	va_start(ap, fmt);
	fputs("log ", stdout);
	vprintf(fmt, ap);
	fputc('\n', stdout);
	va_end(ap);
}

/* GoldenEye's gun file names the test checks, at their US items */
static const struct { int item; const char *name; } s_items[] = {
	{ ITEM_FIST, "GfistZ" }, { ITEM_WPPK, "GwppkZ" }, { ITEM_TASER, "GtaserZ" },
	{ ITEM_SUIT_LF_HAND, "Csuit_lf_handZ" },
};

static void header(const char *what, int i, const struct ModelFileHeader *h)
{
	printf("%s %d %s %d %d %d %d\n", what, i, skeletonName(h->Skeleton),
			h->numSwitches, h->numMatrices, h->numRecords, h->numtextures);
}

int main(int argc, char **argv)
{
	FILE *f;
	long size;
	u8 *rom;
	int i;
	size_t k;

	if (argc < 3 || !(f = fopen(argv[1], "rb"))) {
		return 2;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	rom = (u8 *)malloc((size_t)size);
	if (!rom || fread(rom, 1, (size_t)size, f) != (size_t)size) {
		return 2;
	}
	fclose(f);

	for (i = 0; i < 38; i++) {
		levelinfotable[i].levelID = s_levelIds[i];
		levelinfotable[i].bg_seg_filename = (void *)"retail";
		levelinfotable[i].bg_stan_filename = (void *)"retail";
	}
	for (i = 0; i < 47; i++) {
		fog_tables[i].Id = 1;
	}
	for (i = 0; i < 3; i++) {
		fog_tables2[i].Id = 1;
	}
	for (i = 0; i < 24; i++) {
		music_setup_entries[i].stage_id = 1;
	}
	for (i = 0; i < 51; i++) {
		random_tracks[i] = 1;
	}
	random_tracks[51] = -1;
	for (k = 0; k < sizeof(s_items) / sizeof(s_items[0]); k++) {
		gitem_structs[s_items[k].item].item_header = &s_itemHeaders[s_items[k].item];
		gitem_structs[s_items[k].item].item_file_name = (char *)s_items[k].name;
	}

	gevrRomNormaliseOrder(rom, (unsigned int)size);
	gevrRomCampaignSet(atoi(argv[2]));
	printf("match %s\n", gevrRomLayoutMatchHeader(rom, (uint32_t)size) == gevrRomActiveLayout() ? "yes" : "no");
	g_RomFile = rom;
	g_RomFileSize = (u32)size;
	printf("apply %d\n", gevrRomApplyVariant(rom, (uint32_t)size));
	/* obInit's rebind (ob.c) must keep the hack's rows */
	printf("rebind %d\n", gevrRomRebindVariantFiles());

	printf("images %d\n", gevrTextureCount());
	for (i = 0; c_item_entries[i].header; i++) {
		header("chr", i, c_item_entries[i].header);
		printf("chrname %d %s\n", i, c_item_entries[i].filename);
	}
	printf("props %d\n", g_GevrPropCount);
	for (i = 0; i < g_GevrPropCount; i++) {
		header("prop", i, PitemZ_entries[i].header);
		printf("propname %d %s\n", i, PitemZ_entries[i].filename);
		printf("blast %d %d %d %d\n", i, object_explosion_details[i].TypeID,
				object_explosion_details[i].Seed[0], object_explosion_details[i].Seed[5]);
	}
	for (k = 0; k < sizeof(s_items) / sizeof(s_items[0]); k++) {
		header("item", s_items[k].item, &s_itemHeaders[s_items[k].item]);
		printf("itemname %d %s\n", s_items[k].item, s_items[k].name);
	}
	for (i = 1; i < 727; i++) {
		if (file_resource_table[i].hw_address) {
			printf("file %d %s %ld %u\n", i, file_resource_table[i].filename,
					(long)(file_resource_table[i].hw_address - rom), resource_lookup_data_array[i].rom_size);
		}
	}
	for (i = 0; i < 38; i++) {
		printf("level %d %s %s %.6f %.6f %.6f\n", levelinfotable[i].levelID,
				(const char *)levelinfotable[i].bg_seg_filename, (const char *)levelinfotable[i].bg_stan_filename,
				levelinfotable[i].levelscale, levelinfotable[i].visibility, levelinfotable[i].unknownfloat);
	}
	for (i = 0; i < 48; i++) {
		const EnvironmentRecord *r = &fog_tables[i];

		printf("fog %d %u %.3f %.3f %.3f %.3f %u %d %d %d %d %d %.3f %d %d %.3f %d %.3f\n", i, (unsigned)r->Id,
				r->Visibility.BlendMultiplier, r->Visibility.FarFog, r->Visibility.Nfd.NearFog,
				r->Visibility.MinVisrange, (unsigned)r->Visibility.Intensity, r->Fog.FarIntensity,
				r->Sky.Red, r->Sky.Green, r->Sky.Blue, r->Sky.Clouds, r->Sky.CloudRepeat, r->Sky.SkyImageId,
				r->Sky.IsWater, r->Sky.WaterRepeat, r->Sky.WaterImageId, r->Sky.WaterConcavity);
	}
	for (i = 0; i < 4; i++) {
		const EnvironmentFoglessRecord *r = &fog_tables2[i];

		printf("fogless %d %u %d %d %d %d %.3f %d %.3f\n", i, (unsigned)r->Id, r->Red, r->Green, r->Blue,
				r->Clouds, r->CloudRepeat, r->IsWater, r->WaterRepeat);
	}
	for (i = 0; i < 25; i++) {
		printf("music %d %d %d %d %d\n", i, music_setup_entries[i].stage_id, music_setup_entries[i].main_music,
				music_setup_entries[i].bg_sound, music_setup_entries[i].xtrack);
	}
	for (i = 0; i < 52; i++) {
		printf("random %d %d\n", i, random_tracks[i]);
	}
	for (i = 0; i < 20; i++) {
		printf("times %d %d %d %d\n", i, solo_target_time_array[i][0], solo_target_time_array[i][1],
				solo_target_time_array[i][2]);
	}
	for (i = 0; mission_folder_setup_entries[i].folder_text_preset; i++) {
		printf("folder %d %s %d %d %d\n", i, mission_folder_setup_entries[i].string_ptr,
				mission_folder_setup_entries[i].stage_id, mission_folder_setup_entries[i].type,
				mission_folder_setup_entries[i].mission_num);
	}
	return 0;
}
