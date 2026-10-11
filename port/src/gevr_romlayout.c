/*
 * Measured ROM layouts for Goldfinger 64 1.0 and Tomorrow Never Dies 64
 * Expanded 06-22.
 *
 * Rows are the g_Layouts table from perfect-dark-dabs-mod
 * (port/src/geconvert.c). MIT License, Copyright (c) 2022 Ryan Dwyer.
 * Measurements by Dab. This file does not include that converter.
 */

#include "gevr_romlayout.h"

#include <stdlib.h>
#include <string.h>

#define GEVR_LAYOUT_COUNT 3
#define GEVR_AI_OPS 253

static const struct gevrRomLayout g_Layouts[GEVR_LAYOUT_COUNT] = {
	{
		"GoldenEye 007 (US)", "ge", "GOLDENEYE", "NGEE",
		{ 0xdc, 0xbc, 0x50, 0xd1, 0x09, 0xfd, 0x1a, 0xa3 },
		0xc00000, 0,
		0x80049300, 0x8f7df0, 2698, 8, 0xffffff,
		0x8003de10, 80,
		{ { 0 } },
		{
			{ 0x2e63f0, 0x24b0 }, { 0x2e88a0, 0x3540 },
			{ 0x3b4450, 0x43a0 }, { 0x3b87f0, 0x60fa0 }, { 0x419790, 0x1eed0 },
			{ 0x2ebde0, 0x5bc0 }, { 0x2f19a0, 0xc2ab0 },
			{ 0x2a4d50, 107890 },
		},
		{ { 23, 75 }, { 5, 78 }, { 24, 76 }, { 22, 74 }, { 25, 77 }, { 5, 78 }, { 5, 78 }, { 5, 78 }, { 5, 78 } },
		{
			-1, -1, 186, 209, 191, 204, 205, 193, 184, 195, 189, 206, 194, 188, 197, 192,
			207, 210, 190, 208, 191, 191, 187, -1, 185, 211, 196, 201, 200, 199, -1, -1,
		},
		0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0,
		0x8003a228, 340, 0x8003b224,
		0,
	},
	{
		"Goldfinger 64", "gf64", "GOLDFINGER", "NGFE",
		{ 0xb2, 0x24, 0x27, 0x48, 0xff, 0xbd, 0x61, 0xda },
		0x1800000, 1,
		0x80049300, 0xeae714, 3915, 4, 0x7fffff,
		0x80700fc0, 126,
		{
			{ 0x80700000, 0x17fd000, 0x3000 },
			{ 0x80703000, 0x17f8800, 0x4800 },
			{ 0x80708000, 0x17f0000, 0x8800 },
		},
		{
			{ 0x3115e0, 0x24b0 }, { 0x313a90, 0x3540 },
			{ 0x40fd70, 0x43a0 }, { 0x414110, 0x60fa0 }, { 0x4750b0, 0x259c0 },
			{ 0x316fd0, 0x5bc0 }, { 0x31cb90, 0xf31e0 },
			{ 0x2a4d50, 174006 },
		},
		{ { 9, 74 }, { 8, 74 }, { 20, 74 }, { 22, 74 }, { 25, 74 }, { 37, 74 }, { 37, 74 }, { 37, 74 }, { 5, 74 } },
		{
			-1, -1, 237, 209, 191, 204, 205, 191, 191, 184, 195, 193, 189, 206, 188, 192,
			207, 194, 197, 208, 210, 231, 230, 190, 187, 185, 211, 196, 201, 200, 199, -1,
		},
		0, 0, 0,
		0, 0, 0, 0,
		1, 0, 0,
		0x8070b400, 416, 0x8070dd28,
		0x7f023668,
	},
	{
		"Tomorrow Never Dies 64", "tnd64", "GOLDENEYE", "NGEE",
		{ 0xd2, 0x24, 0x26, 0x04, 0xf7, 0xa5, 0x07, 0x84 },
		0x1000000, 1,
		0x80049300, 0x89e60a, 3045, 4, 0x7fffff,
		0x80702e00, 112,
		{
			{ 0x80702000, 0xffd000, 0x1c00 },
		},
		{
			{ 0x2df7c0, 0x24b0 }, { 0x2e1c70, 0x3540 },
			{ 0x474068, 0x43a0 }, { 0x478408, 0x1e5058 }, { 0x65d460, 0x20810 },
			{ 0x2e51b0, 0x5bc0 }, { 0x2ead70, 0x1892f8 },
			{ 0x29e120, 107890 },
		},
		{ { 23, 74 }, { 22, 74 }, { 24, 74 }, { 5, 74 }, { 25, 74 }, { 16, 76 }, { 79, 76 }, { 28, 76 }, { 40, 49 } },
		{
			-1, -1, 209, 186, 191, 204, 205, 193, 184, 195, 189, 206, 194, 188, 197, 192,
			207, 210, 190, 208, 230, 231, 187, -1, 185, 211, 196, 201, 200, 199, -1, -1,
		},
		0x2881b0, 0x296470, 0x14c0,
		14, 1, 9, 13,
		2, 8, 1,
		0x8003a228, 340, 0x8003b224,
		0,
	},
};

/* First field of g_GeAiCommands in perfect-dark-dabs-mod port/include/geaitable.h.
 * 0 means the command runs to the next NUL, inclusive (PRINT). */
static const uint8_t g_AiLen[GEVR_AI_OPS] = {
	  2,   2,   2,   1,   1,   4,   3,   1,   1,   1,   9,   2,   1,   1,   2,   2,
	  2,   2,   2,   2,   6,   6,   6,   6,   4,   4,   2,   5,   3,   1,   3,   3,
	  2,   1,   1,   2,   4,   1,   1,   2,   2,   2,   2,   2,   3,   3,   3,   2,
	  3,   3,   2,   1,   3,   3,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
	  3,   2,   2,   2,   4,   2,   2,   3,   3,   3,   3,   4,   4,   7,   7,   5,
	  5,   4,   6,   6,   5,   4,   3,   3,   4,   3,   3,   3,   3,   3,   2,   2,
	  2,   2,   2,   2,   3,   4,   2,   2,   4,   3,   3,   3,   4,   3,   3,   3,
	  3,   3,   4,   4,   4,   4,   3,   3,   3,   3,   3,   3,   4,   4,   3,   3,
	  3,   2,   2,   2,   3,   2,   2,   2,   2,   3,   2,   3,   2,   2,   2,   3,
	  3,   2,   2,   2,   2,   2,   3,   3,   3,   4,   5,   5,   6,   5,   5,   6,
	  6,   6,   7,   6,   6,   7,   6,   6,   7,   2,   3,   3,   4,   0,   1,   1,
	  1,   1,   2,   5,   5,   1,   1,   3,   1,   1,   2,   4,   4,  12,  11,   9,
	  8,   5,   3,   3,   4,   5,   6,   6,   6,   2,   5,   2,   5,   5,   2,   2,
	  4,   2,   1,   1,   3,   6,   4,   2,   1,   5,   1,   1,   2,   1,   1,   2,
	  3,   3,   4,   2,   2,   3,   5,   2,   2,   1,   1,   2,   1,   1,  13,   1,
	  2,   2,   3,   2,   4,   2,   1,   3,   3,   1,   1,   1,   2,
};

/* GoldenEye's hand-item files, ITEM_FIST through ITEM_TASER. */
static const char *const g_GeItemFiles[32] = {
	NULL, "GfistZ", "GknifeZ", "GthrowknifeZ", "GwppkZ", "GwppksilZ", "Gtt33Z", "GskorpionZ",
	"Gak47Z", "GuziZ", "Gmp5kZ", "Gmp5ksilZ", "GspectreZ", "Gm16Z", "Gfnp90Z", "GshotgunZ",
	"GautoshotZ", "GsniperrifleZ", "GrugerZ", "GgoldengunZ", "GsilverwppkZ", "GgoldwppkZ", "GlaserZ", "GwatchlaserZ",
	"GgrenadelaunchZ", "GrocketlaunchZ", "GgrenadeZ", "GtimedmineZ", "GproximitymineZ", "GremotemineZ", "GtriggerZ", "GtaserZ",
};

static int g_Campaign;
int gevrGexCampaignOff;
static uint32_t g_ImageBankBytes;
static int g_MapOn;
static int g_Host[256];
static int16_t g_Held[32];


static uint32_t be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static const char *dataString(const uint8_t *data, uint32_t dataLen, uint32_t ptr)
{
	uint32_t off;
	uint32_t i;

	if (!data || ptr < GEVR_DATA_VRAM) {
		return "";
	}
	off = ptr - GEVR_DATA_VRAM;
	if (off >= dataLen) {
		return "";
	}
	for (i = off; i < dataLen; i++) {
		if (data[i] == 0) {
			return (const char *)(data + off);
		}
	}
	return "";
}

void gevrRomCampaignSet(int campaign)
{
	if (campaign < GEVR_CAMPAIGN_GE || campaign > GEVR_CAMPAIGN_TND64) {
		campaign = GEVR_CAMPAIGN_GE;
	}
	g_Campaign = campaign;
}

int gevrRomCampaign(void)
{
	return g_Campaign;
}

const struct gevrRomLayout *gevrRomActiveLayout(void)
{
	return &g_Layouts[g_Campaign];
}

int gevrRomIsVariant(void)
{
	return g_Layouts[g_Campaign].variant != 0;
}

const struct gevrRomLayout *gevrRomLayoutMatchHeader(const uint8_t *header, uint32_t size)
{
	int i;

	if (!header) {
		return NULL;
	}
	for (i = 0; i < GEVR_LAYOUT_COUNT; i++) {
		const struct gevrRomLayout *L = &g_Layouts[i];
		size_t title = strlen(L->title);

		if (size != L->romsize) {
			continue;
		}
		if (memcmp(header + 0x20, L->title, title) != 0) {
			continue;
		}
		if (memcmp(header + 0x3b, L->code, 4) != 0) {
			continue;
		}
		if (memcmp(header + 0x10, L->crc, 8) != 0) {
			continue;
		}
		return L;
	}
	return NULL;
}

int gevrRomDataMount(struct gevrDataWindow *win,
		const uint8_t *inflated, uint32_t inflatedLen,
		const uint8_t *rom, uint32_t romLen,
		const struct gevrRomLayout *layout)
{
	uint32_t need;
	uint8_t *buf;
	int i;

	if (!win || !layout) {
		return 0;
	}
	if (inflatedLen && !inflated) {
		return 0;
	}
	need = inflatedLen;
	for (i = 0; i < 3; i++) {
		const struct gevrRomPiece *p = &layout->pieces[i];
		uint32_t off;
		uint32_t end;

		if (!p->len) {
			continue;
		}
		if (!rom || p->vram < GEVR_DATA_VRAM) {
			return 0;
		}
		off = p->vram - GEVR_DATA_VRAM;
		if (off < inflatedLen) {
			return 0;
		}
		if ((uint64_t)p->rom + p->len > romLen) {
			return 0;
		}
		end = off + p->len;
		if (end < off) {
			return 0;
		}
		if (end > need) {
			need = end;
		}
	}

	buf = (uint8_t *)calloc(1, need ? need : 1);
	if (!buf) {
		return 0;
	}
	if (inflatedLen) {
		memcpy(buf, inflated, inflatedLen);
	}
	for (i = 0; i < 3; i++) {
		const struct gevrRomPiece *p = &layout->pieces[i];

		if (!p->len) {
			continue;
		}
		memcpy(buf + (p->vram - GEVR_DATA_VRAM), rom + p->rom, p->len);
	}
	win->bytes = buf;
	win->len = need;
	return 1;
}

void gevrRomDataFree(struct gevrDataWindow *win)
{
	if (!win) {
		return;
	}
	free(win->bytes);
	win->bytes = NULL;
	win->len = 0;
}

const uint8_t *gevrRomDataBytes(const struct gevrDataWindow *win, uint32_t vram, uint32_t need)
{
	uint32_t off;

	if (!win || !win->bytes || vram < GEVR_DATA_VRAM) {
		return NULL;
	}
	off = vram - GEVR_DATA_VRAM;
	if ((uint64_t)off + need > win->len) {
		return NULL;
	}
	return win->bytes + off;
}

int gevrRomParseFiles(const uint8_t *data, uint32_t dataLen,
		struct gevrRomFileRow *out, int maxOut)
{
	uint32_t rows = 0;
	uint32_t k;
	int n = 0;

	if (!data || !out || maxOut <= 0) {
		return 0;
	}
	while (GEVR_FILES_AT + 12u * (rows + 1u) <= dataLen
			&& be32(data + GEVR_FILES_AT + 12u * rows) == rows) {
		rows++;
		if (rows > 4096u) {
			break;
		}
	}
	if (rows < 3u) {
		return 0;
	}
	for (k = 1; k + 1u < rows && n < maxOut; k++) {
		uint32_t base = GEVR_FILES_AT + 12u * k;
		uint32_t addr = be32(data + base + 8);
		uint32_t next = be32(data + base + 12 + 8);
		struct gevrRomFileRow *row = &out[n++];

		row->index = be32(data + base);
		row->addr = addr;
		row->size = next >= addr ? next - addr : 0;
		row->name = dataString(data, dataLen, be32(data + base + 4));
	}
	return n;
}

void gevrRomDecodeImage(uint32_t word, uint32_t stride, uint32_t mask,
		uint32_t *hitSound, uint32_t *hitTexture, uint32_t *size)
{
	(void)stride;
	if (hitSound) {
		*hitSound = (word >> 28) & 0xfu;
	}
	if (hitTexture) {
		*hitTexture = (word >> 24) & 0xfu;
	}
	if (size) {
		*size = word & mask;
	}
}

void gevrRomClearItemMap(void)
{
	g_MapOn = 0;
}

static const char *itemStem(const char *name)
{
	const char *slash;

	if (!name || !name[0]) {
		return NULL;
	}
	slash = strrchr(name, '/');
	return slash ? slash + 1 : name;
}

void gevrRomBuildItemMap(const char *const *names, int count)
{
	const struct gevrRomLayout *L = gevrRomActiveLayout();
	int item;

	gevrRomClearItemMap();
	if (!L || !L->variant || !names) {
		return;
	}
	for (item = 0; item < 256; item++) {
		g_Host[item] = item;
	}
	for (item = 0; item < 32; item++) {
		g_Held[item] = -2;
	}
	for (item = 1; item < 32 && item < count; item++) {
		const char *stem = itemStem(names[item]);
		int j;

		if (!stem) {
			continue;
		}
		for (j = 1; j < 32; j++) {
			if (g_GeItemFiles[j] && strcmp(g_GeItemFiles[j], stem) == 0) {
				g_Host[item] = j;
				g_Held[j] = L->heldprops[item];
				break;
			}
		}
	}
	g_MapOn = 1;
}

int gevrRomItemHost(int item)
{
	if (!g_MapOn || item < 0 || item > 255) {
		return item;
	}
	return g_Host[item];
}

int gevrRomHeldProp(int usItem)
{
	if (!g_MapOn || usItem < 0 || usItem >= 32) {
		return -2;
	}
	return g_Held[usItem];
}

int gevrRomMissionLimit(void)
{
	const struct gevrRomLayout *L = gevrRomActiveLayout();

	if (!L->variant || !L->nummissions) {
		return 20;
	}
	return L->nummissions;
}

int gevrRomCreditsAfter(void)
{
	const struct gevrRomLayout *L = gevrRomActiveLayout();

	return L->variant ? L->creditsafter : 0;
}

int gevrRomAdvanceBelow(void)
{
	const struct gevrRomLayout *L = gevrRomActiveLayout();

	return L->variant ? L->advancebelow : 0;
}

int gevrRomAiHooks(void)
{
	return gevrRomActiveLayout()->aihooks && gevrRomIsVariant();
}

int gevrRomBondOutfit(int cuff, int *body, int *head)
{
	const struct gevrRomLayout *L = gevrRomActiveLayout();

	if (!L->variant || cuff < 0 || cuff > 8 || !body || !head) {
		return 0;
	}
	*body = L->bond[cuff][0];
	*head = L->bond[cuff][1];
	return 1;
}

void gevrRomSetImageBankBytes(uint32_t bytes)
{
	g_ImageBankBytes = bytes;
}

uint32_t gevrRomGetImageBankBytes(void)
{
	return g_ImageBankBytes;
}

uint32_t gevrRomImageBankBytes(const uint8_t *data, uint32_t dataLen,
		const struct gevrRomLayout *layout)
{
	uint32_t sum = 0;
	uint32_t i;
	uint32_t at;

	if (!data || !layout || !layout->numimages || !layout->imagestride) {
		return 0;
	}
	if (layout->images < GEVR_DATA_VRAM) {
		return 0;
	}
	at = layout->images - GEVR_DATA_VRAM;
	if ((uint64_t)at + (uint64_t)layout->imagestride * layout->numimages > dataLen) {
		return 0;
	}
	for (i = 0; i < layout->numimages; i++) {
		uint32_t size = 0;

		gevrRomDecodeImage(be32(data + at + layout->imagestride * i),
				layout->imagestride, layout->imagemask, NULL, NULL, &size);
		sum += size;
	}
	return sum;
}

static uint32_t gevrAiLen(const uint8_t *code, uint32_t len, uint32_t at)
{
	uint32_t op;
	uint32_t end;
	uint8_t fixed;

	if (at >= len) {
		return 0;
	}
	op = code[at];
	if (op >= GEVR_AI_OPS) {
		return 0;
	}
	fixed = g_AiLen[op];
	if (fixed) {
		return fixed;
	}
	for (end = at + 1; end < len && code[end]; end++) {
	}
	if (end >= len) {
		return 0;
	}
	return end - at + 1;
}

uint32_t gevrRomAiPrepare(uint8_t *code, uint32_t len, int dropHooks)
{
	uint32_t r = 0;
	uint32_t w = 0;

	if (!code) {
		return 0;
	}
	while (r < len) {
		uint32_t n = gevrAiLen(code, len, r);
		int drop = 0;

		if (!n || r + n > len) {
			break;
		}
		if (dropHooks && code[r] == 0x2b && n >= 2
				&& code[r + 1] >= 0xfb && code[r + 1] <= 0xfe) {
			drop = 1;
		}
		if (!drop) {
			int at = 0;

			if (w != r) {
				memmove(code + w, code + r, n);
			}
			/*
			 * The commands that name a hand item (geaitable.h): the item byte
			 * goes to the US item on the same gun file, as setups' are.
			 * TRYDroppingItem and TRYGiveMeItem carry a prop first.
			 */
			switch (code[w]) {
			case 0x1b: /* TRYDroppingItem prop16 item label */
			case 0xbf: /* TRYGiveMeItem prop16 item flags32 label */
				at = 3;
				break;
			case 0x57: /* IFKeyDropped item label */
			case 0x58: /* IFItemIsAttachedToObject item obj label */
			case 0x59: /* IFBondHasItemEquipped item label */
			case 0xe3: /* BondEquipItem item */
			case 0xe4: /* BondEquipItemCinema item */
				at = 1;
				break;
			}
			if (at && (uint32_t)at < n) {
				code[w + at] = (uint8_t)gevrRomItemHost(code[w + at]);
			}
			w += n;
		}
		r += n;
		if (drop && r < len && code[r] == 0xad) {
			uint32_t pn = gevrAiLen(code, len, r);

			if (pn && r + pn <= len) {
				r += pn;
			}
		}
	}
	if (dropHooks && w < len) {
		memset(code + w, 0, len - w);
	}
	return dropHooks ? w : len;
}
