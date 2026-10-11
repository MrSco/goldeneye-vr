/*
 * Runs the Bean image repaints (gevr_bean_images.c) as texLoad would, on the
 * player's own XBLA files; test_bean_images.py writes the images and checks
 * what comes back. Run from a folder where added-content/xbla/files resolves.
 *
 * usage: images in.bin out.bin
 * in.bin: u32 count, then per image s32 id, u16 width, u16 height, u8 depth, 3 pad.
 * out.bin: per image u32 width, u32 height (0 when not repainted), RGBA8.
 */
#include <ultra64.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gevr_bean.h"
#include "gevr_bean_int.h"

void videoFreeCachedTexture(const void *addr) { (void)addr; }
int gevrBeanIsAvailable(void) { return 1; }
int gevrBeanIsEnabled(void) { return 1; }
int gevrRomIsVariant(void) { return 0; }

void sysLogPrintf(s32 level, const char *fmt, ...)
{
	va_list ap;

	(void)level;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static uint32_t le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

int main(int argc, char **argv)
{
	FILE *f;
	long size;
	uint8_t *in;
	uint32_t count;
	void **data;
	FILE *out;
	int bound = 0;

	if (argc < 3 || !(f = fopen(argv[1], "rb"))) {
		return 2;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	rewind(f);
	in = malloc((size_t)size);
	if (!in || fread(in, 1, (size_t)size, f) != (size_t)size) {
		return 2;
	}
	fclose(f);
	count = le32(in);
	data = calloc(count, sizeof(*data));
	for (uint32_t i = 0; i < count; i++) {
		const uint8_t *r = in + 4 + 12 * i;

		data[i] = malloc(16);   /* stands for the image's loaded texels */
		gevrBeanImageLoaded((int)le32(r), data[i], r[4] | r[5] << 8, r[6] | r[7] << 8, r[8]);
	}
	/* an address reused by another image loses its paint */
	if (count) {
		const uint8_t *r = in + 4 + 12 * (count - 1);

		gevrBeanImageLoaded(4000, data[count - 1], r[4] | r[5] << 8, r[6] | r[7] << 8, r[8]);
	}
	out = fopen(argv[2], "wb");
	for (uint32_t i = 0; i < count; i++) {
		const uint8_t *rgba = NULL;
		int w = 0, h = 0;
		uint32_t dims[2] = { 0, 0 };

		if (gevrBeanRepaint(data[i], &rgba, &w, &h)) {
			dims[0] = (uint32_t)w;
			dims[1] = (uint32_t)h;
			bound++;
		}
		fwrite(dims, 4, 2, out);
		if (dims[0]) {
			fwrite(rgba, 4, (size_t)w * h, out);
		}
	}
	fclose(out);
	gevrBeanRepaintForget();
	printf("bound %d\n", bound);
	return 0;
}
