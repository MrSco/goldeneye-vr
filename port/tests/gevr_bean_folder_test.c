/*
 * Runs the briefing folder repaint (gevr_bean_folder.c) on the player's own
 * PwalletbondZ texture table and XBLA walletbond file; test_bean_folder.py
 * writes the textures and checks what comes back.
 *
 * usage: folder textures.bin walletbond/default.bin out.bin
 * textures.bin: u32 count, then per texture u16 width, u16 height, u8 depth,
 * 3 pad bytes, u32 byte count and the pool bytes (little-endian).
 * out.bin: per texture u32 width, u32 height (0 when not repainted), RGBA8.
 */
#include <ultra64.h>
#include <PR/gbi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bondtypes.h>
#include "gevr_bean.h"
#include "gevr_bean_int.h"

void videoFreeCachedTexture(const void *addr)
{
	(void)addr;
}

void sysLogPrintf(s32 level, const char *fmt, ...)
{
	va_list ap;

	(void)level;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static uint8_t *slurp(const char *path, long *size)
{
	FILE *f = fopen(path, "rb");
	uint8_t *d;

	if (!f || fseek(f, 0, SEEK_END) != 0 || (*size = ftell(f)) <= 0) {
		return NULL;
	}
	rewind(f);
	d = malloc((size_t)*size);
	if (!d || fread(d, 1, (size_t)*size, f) != (size_t)*size) {
		return NULL;
	}
	fclose(f);
	return d;
}

static uint32_t le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

int main(int argc, char **argv)
{
	long tsize = 0, csize = 0;
	uint8_t *tin, *cin, *at;
	struct ModelFileHeader header;
	struct gevrCaff caff;
	uint32_t count;
	uint8_t **texels;
	FILE *out;
	int bound;

	if (argc < 4 || !(tin = slurp(argv[1], &tsize)) || !(cin = slurp(argv[2], &csize))) {
		fprintf(stderr, "usage: folder textures.bin walletbond.bin out.bin\n");
		return 2;
	}
	count = le32(tin);
	memset(&header, 0, sizeof(header));
	header.numtextures = (s16)count;
	header.Textures = calloc(count, sizeof(ModelFileTextures));
	texels = calloc(count, sizeof(*texels));
	at = tin + 4;
	for (uint32_t i = 0; i < count; i++) {
		uint32_t n = le32(at + 8);

		header.Textures[i].Width = (u8)(at[0] | at[1] << 8);
		header.Textures[i].Height = (u8)(at[2] | at[3] << 8);
		header.Textures[i].RenderDepth = at[4];
		texels[i] = malloc(n ? n : 1);
		memcpy(texels[i], at + 12, n);
		header.Textures[i].TextureID = (uintptr_t)texels[i];
		at += 12 + n;
	}
	memset(&caff, 0, sizeof(caff));
	if (!gevrCaffOpen(&caff, cin, (uint32_t)csize)) {
		fprintf(stderr, "walletbond: not a CAFF\n");
		return 1;
	}
	bound = gevrBeanFolderRepaint(&header, &caff);
	out = fopen(argv[3], "wb");
	for (uint32_t i = 0; i < count; i++) {
		const uint8_t *rgba = NULL;
		int w = 0, h = 0;
		uint32_t dims[2] = { 0, 0 };

		if (gevrBeanRepaint(texels[i], &rgba, &w, &h)) {
			dims[0] = (uint32_t)w;
			dims[1] = (uint32_t)h;
		}
		fwrite(dims, 4, 2, out);
		if (dims[0]) {
			fwrite(rgba, 4, (size_t)w * h, out);
		}
	}
	fclose(out);
	gevrBeanRepaintForget();
	gevrCaffClose(&caff);
	printf("bound %d\n", bound);
	return 0;
}
