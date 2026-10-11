#ifndef GEVR_ROMLAYOUT_H
#define GEVR_ROMLAYOUT_H

/*
 * Measured layouts for Goldfinger 64 1.0 and Tomorrow Never Dies 64
 * Expanded 06-22. The numbers come from perfect-dark-dabs-mod's g_Layouts
 * (port/src/geconvert.c), MIT License, Copyright (c) 2022 Ryan Dwyer,
 * measurements by Dab. This header does not pull that converter in.
 *
 * Retail ge.z64 stays where it is. The two hacks live beside it
 * (goldfinger64.z64, tnd64.z64). The launcher campaign picks which file
 * romdataInit binds.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_DATA_VRAM 0x80020d90u
#define GEVR_DATA_ROM  0x21990u
#define GEVR_FILES_AT  0x252c4u

#define GEVR_CAMPAIGN_GE   0
#define GEVR_CAMPAIGN_GF64 1
#define GEVR_CAMPAIGN_TND64 2

#define GEVR_ROM_GF_FILE  "goldfinger64.z64"
#define GEVR_ROM_TND_FILE "tnd64.z64"
/* Written when a chosen campaign did not load; the launcher shows it and deletes it. */
#define GEVR_CAMPAIGN_FAILED_FILE "gevr_campaign_failed.txt"

struct gevrRomPiece {
	uint32_t vram;
	uint32_t rom;
	uint32_t len;
};

struct gevrRomLayout {
	const char *name;
	const char *tag;
	const char *title;
	const char *code;
	uint8_t crc[8];
	uint32_t romsize;
	int variant; /* 0 = retail US manifest */
	uint32_t images;
	uint32_t imagesrom;
	uint32_t numimages;
	uint32_t imagestride;
	uint32_t imagemask;
	uint32_t chrs;
	uint32_t numchrs;
	struct gevrRomPiece pieces[3];
	/* fonts, sound, music, gunbarrel: the eight banks g_MenuRaw copies */
	uint32_t raw[8][2];
	uint8_t bond[9][2]; /* body, head by CUFF_TYPES */
	int16_t heldprops[32];
	uint32_t animdata;
	uint32_t globalimages;
	uint32_t globalbytes;
	uint8_t nummissions; /* 0 = GoldenEye's twenty */
	uint8_t aihooks;
	uint8_t creditsafter;
	uint8_t advancebelow;
	uint8_t watchtint;
	uint8_t maleheads;
	uint8_t nolaser;
	/* PitemZ_entries and object_explosion_details (14-byte rows), a row a prop */
	uint32_t props, numprops;
	uint32_t explosions;
	/* bodyChooseHead()'s pool lengths in the game code (eight bytes, the lists' addresses after); 0 = GoldenEye's */
	uint32_t headpools;
	/* headHat_array: where each head wears each of the six hats, from head hatfirst, hatcount heads */
	uint32_t hats, hatfirst, hatcount;
};

struct gevrDataWindow {
	uint8_t *bytes;
	uint32_t len;
};

struct gevrRomFileRow {
	uint32_t index;
	uint32_t addr;
	uint32_t size;
	const char *name; /* points into the data window, or "" */
};

void gevrRomCampaignSet(int campaign);
int gevrRomCampaign(void);
int gevrRomIsVariant(void);
/* 1 when GexGuns was on but a hack campaign turned it off for this run; it is still saved as on. */
extern int gevrGexCampaignOff;
const struct gevrRomLayout *gevrRomActiveLayout(void);

/* Title, code, size and the eight CRC bytes. NULL when nothing matches. */
const struct gevrRomLayout *gevrRomLayoutMatchHeader(const uint8_t *header, uint32_t size);

/* Inflated data segment, then the layout's Expansion Pak pieces. */
int gevrRomDataMount(struct gevrDataWindow *win,
		const uint8_t *inflated, uint32_t inflatedLen,
		const uint8_t *rom, uint32_t romLen,
		const struct gevrRomLayout *layout);
void gevrRomDataFree(struct gevrDataWindow *win);
const uint8_t *gevrRomDataBytes(const struct gevrDataWindow *win, uint32_t vram, uint32_t need);

/* File table at 0x252c4. Returns the number of files (rows minus the two sentinels). */
int gevrRomParseFiles(const uint8_t *data, uint32_t dataLen,
		struct gevrRomFileRow *out, int maxOut);

/* Top byte is hit sound / hit texture. Size is the word under the layout mask. */
void gevrRomDecodeImage(uint32_t word, uint32_t stride, uint32_t mask,
		uint32_t *hitSound, uint32_t *hitTexture, uint32_t *size);

/* Put the hack's file table back in file_resource_table (obInit rebinds it). */
int gevrRomRebindVariantFiles(void);

/* names[item] is the hack's gun file. Held props land on the US item that file is. */
void gevrRomClearItemMap(void);
void gevrRomBuildItemMap(const char *const *names, int count);
int gevrRomItemHost(int item);
int gevrRomHeldProp(int usItem); /* -2 = no override, -1 = nothing in the hand */

int gevrRomMissionLimit(void);
int gevrRomCreditsAfter(void);
int gevrRomAdvanceBelow(void);
int gevrRomAiHooks(void);
int gevrRomBondOutfit(int cuff, int *body, int *head);
/* A hack's own random-head pools (Goldfinger): level init, and the head for a record's -1 or -2.
 * Both return 0 / -1 where the campaign has none, and GoldenEye's code runs. */
int gevrRomHeadPoolsLevelInit(void);
int gevrRomChooseHead(int male, int head);
/* The hack's hat fits (struct headHat [count][6]) and the first head they cover; NULL for GoldenEye's. */
struct headHat *gevrRomHeadHats(int *first, int *count);

/* Sum of the image-row sizes, the span of the images bank. 0 when the table does not fit. */
uint32_t gevrRomImageBankBytes(const uint8_t *data, uint32_t dataLen,
		const struct gevrRomLayout *layout);
void gevrRomSetImageBankBytes(uint32_t bytes);
uint32_t gevrRomGetImageBankBytes(void);

/*
 * Walk one AI list. When dropHooks is set, TRYFindCover labels 0xfb-0xfe and
 * the PRINT after them are removed. TRYGiveMeItem's item byte is rewritten to
 * the US host. Returns the live length; dropped bytes are cleared.
 */
uint32_t gevrRomAiPrepare(uint8_t *code, uint32_t len, int dropHooks);

/* Bind a matched hack: data window, files, images, characters, mission folder. */
int gevrRomApplyVariant(const uint8_t *rom, uint32_t romLen);

#ifdef __cplusplus
}
#endif

#endif
