/*
 * Several ROMs from one pick (gevr_romimport.h): loose files or a .zip of
 * them, told apart by their headers. Each ROM is read whole (32 MB at most),
 * put in .z64 order and written through a .part file, so the launcher's
 * checks never see half a ROM.
 */
#include "gevr_romimport.h"
#include "gevr_romcheck.h"
#include "gevr_romlayout.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#define ROM_MIN (8u << 20)     /* the smallest campaign ROM is 12 MB */
#define ROM_MAX (64u << 20)    /* the largest a pick may hold */
#define ZIP_MAX (256u << 20)   /* every campaign zipped is ~100 MB */

enum { KIND_NONE, KIND_GE, KIND_GE_MAYBE, KIND_GEX, KIND_HACK };

struct sorted {
	int filed, gotGe, geGood;
	char added[512];
	char unknown[256];
};

static void listAdd(char *list, size_t cap, const char *item)
{
	size_t len = strlen(list);
	if (strstr(list, item)) return;   /* one GoldenEye in two places is one line */
	if (len + strlen(item) + 3 >= cap) return;
	if (len) strcat(list, ", ");
	strcat(list, item);
}

static const char *baseName(const char *path)
{
	const char *s = strrchr(path, '/'), *b = strrchr(path, '\\');
	if (b && (!s || b > s)) s = b;
	return s ? s + 1 : path;
}

/* What a ROM is, from its .z64-order header and size. */
static int classify(const uint8_t *hdr, uint32_t size, const char **name, const char **file)
{
	const struct gevrRomLayout *L;
	if (size == 0x2000000 && memcmp(hdr + 0x20, "GoldenEye X", 11) == 0) {
		*name = "GoldenEye X";
		*file = "picked-gex.z64";
		return KIND_GEX;
	}
	L = gevrRomLayoutMatchHeader(hdr, size);
	if (L && strcmp(L->tag, "ge") == 0) {
		*name = L->name;
		*file = "picked.z64";
		return KIND_GE;
	}
	if (L) {
		*name = L->name;
		*file = !strcmp(L->tag, "gf64") ? GEVR_ROM_GF_FILE
		      : !strcmp(L->tag, "tnd64") ? GEVR_ROM_TND_FILE
		      : !strcmp(L->tag, "mirror") ? GEVR_ROM_MIRROR_FILE
		      : !strcmp(L->tag, "mario") ? GEVR_ROM_MARIO_FILE : NULL;
		return *file ? KIND_HACK : KIND_NONE;
	}
	/* a GoldenEye that is not the USA 1.0 (PAL, a bad dump): the launcher's
	 * probe says what is wrong with it */
	if (memcmp(hdr + 0x20, "GOLDENEYE", 9) == 0) {
		*name = "GoldenEye 007";
		*file = "picked.z64";
		return KIND_GE_MAYBE;
	}
	return KIND_NONE;
}

static int writeWhole(const char *dataDir, const char *file, const uint8_t *data, uint32_t size)
{
	char path[1024], part[1100];
	FILE *f;
	snprintf(path, sizeof(path), "%s/%s", dataDir, file);
	snprintf(part, sizeof(part), "%s.part", path);
	f = fopen(part, "wb");
	if (!f) return 0;
	if (fwrite(data, 1, size, f) != size) {
		fclose(f);
		remove(part);
		return 0;
	}
	if (fclose(f) != 0) {
		remove(part);
		return 0;
	}
	remove(path);   /* Windows' rename does not replace */
	if (rename(part, path) != 0) {
		remove(part);
		return 0;
	}
	return 1;
}

/* One ROM image in memory (any byte order); label names it in the summary. */
static void fileRom(const char *dataDir, uint8_t *data, uint32_t size, const char *label, struct sorted *s)
{
	const char *name = NULL, *file = NULL;
	int kind;
	if (!gevrRomNormaliseOrder(data, size)) {
		listAdd(s->unknown, sizeof(s->unknown), label);
		return;
	}
	kind = classify(data, size, &name, &file);
	if (kind == KIND_NONE) {
		listAdd(s->unknown, sizeof(s->unknown), label);
		return;
	}
	if (kind == KIND_GE_MAYBE && s->geGood) return;   /* a good one came first */
	if (!writeWhole(dataDir, file, data, size)) {
		listAdd(s->unknown, sizeof(s->unknown), label);
		return;
	}
	if (kind == KIND_GE || kind == KIND_GE_MAYBE) {
		s->gotGe = 1;
		if (kind == KIND_GE) s->geGood = 1;
	}
	if (kind != KIND_GE_MAYBE) {
		listAdd(s->added, sizeof(s->added), name);
		s->filed++;
	}
}

/* ---- .zip ---------------------------------------------------------------- */

static uint32_t u16le(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t u32le(const uint8_t *p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static int inflateRaw(const uint8_t *in, uint32_t inLen, uint8_t *out, uint32_t outLen)
{
	z_stream zs;
	int res;
	memset(&zs, 0, sizeof(zs));
	if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return 0;
	zs.next_in = (Bytef *)in;
	zs.avail_in = inLen;
	zs.next_out = out;
	zs.avail_out = outLen;
	res = inflate(&zs, Z_FINISH);
	inflateEnd(&zs);
	return res == Z_STREAM_END && zs.total_out == outLen;
}

static void sortZip(const char *dataDir, const uint8_t *zip, uint32_t size, const char *label, struct sorted *s)
{
	uint32_t eocd = 0, cd, count, i, at;
	int found = 0;
	const uint32_t back = size < 0x10000 + 22 ? size : 0x10000 + 22;
	for (i = 22; i <= back; i++) {
		if (u32le(zip + size - i) == 0x06054b50) {
			eocd = size - i;
			found = 1;
			break;
		}
	}
	if (!found) {
		listAdd(s->unknown, sizeof(s->unknown), label);
		return;
	}
	count = u16le(zip + eocd + 10);
	cd = u32le(zip + eocd + 16);
	at = cd;
	for (i = 0; i < count; i++) {
		uint32_t method, crc, csize, usize, nameLen, extraLen, commentLen, local, dataOfs;
		char name[256];
		uint8_t *rom;
		if (at > size || size - at < 46 || u32le(zip + at) != 0x02014b50) break;
		method = u16le(zip + at + 10);
		crc = u32le(zip + at + 16);
		csize = u32le(zip + at + 20);
		usize = u32le(zip + at + 24);
		nameLen = u16le(zip + at + 28);
		extraLen = u16le(zip + at + 30);
		commentLen = u16le(zip + at + 32);
		local = u32le(zip + at + 42);
		if (size - at - 46 < nameLen) break;
		snprintf(name, sizeof(name), "%.*s", (int)(nameLen < 255 ? nameLen : 255), (const char *)zip + at + 46);
		at += 46 + nameLen + extraLen + commentLen;
		/* folders, readmes, patches, macOS's ._ shadows: not ROM-sized */
		if (usize < ROM_MIN || usize > ROM_MAX) continue;
		if (local > size || size - local < 30 || u32le(zip + local) != 0x04034b50) {
			listAdd(s->unknown, sizeof(s->unknown), baseName(name));
			continue;
		}
		dataOfs = local + 30 + u16le(zip + local + 26) + u16le(zip + local + 28);
		if (dataOfs > size || size - dataOfs < csize || (method != 0 && method != 8)
				|| (method == 0 && csize != usize)) {
			listAdd(s->unknown, sizeof(s->unknown), baseName(name));
			continue;
		}
		rom = (uint8_t *)malloc(usize);
		if (!rom) break;
		if (method == 0) memcpy(rom, zip + dataOfs, usize);
		if ((method == 0 || inflateRaw(zip + dataOfs, csize, rom, usize))
				&& crc32(0, rom, usize) == crc) {
			fileRom(dataDir, rom, usize, baseName(name), s);
		} else {
			listAdd(s->unknown, sizeof(s->unknown), baseName(name));
		}
		free(rom);
	}
}

/* ---- the picks ------------------------------------------------------------ */

static uint8_t *readWhole(const char *path, uint32_t *size)
{
	FILE *f = fopen(path, "rb");
	long len;
	uint8_t *buf;
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) <= 0 || (unsigned long)len > ZIP_MAX
			|| fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}
	buf = (uint8_t *)malloc((size_t)len);
	if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	*size = (uint32_t)len;
	return buf;
}

static void sortFile(const char *dataDir, const char *path, const char *label, struct sorted *s)
{
	uint32_t size = 0;
	uint8_t *data = readWhole(path, &size);
	if (!data) {
		listAdd(s->unknown, sizeof(s->unknown), label);
		return;
	}
	if (size >= 4 && u32le(data) == 0x04034b50) sortZip(dataDir, data, size, label, s);
	else if (size >= ROM_MIN && size <= ROM_MAX) fileRom(dataDir, data, size, label, s);
	else listAdd(s->unknown, sizeof(s->unknown), label);
	free(data);
}

/* picked.z64 is sorted when it is a zip, GoldenEye X or another campaign. */
static int pickedNeedsSort(const char *dataDir)
{
	char path[1024];
	uint8_t hdr[0x40];
	FILE *f;
	long size;
	const char *name, *file;
	size_t n;
	snprintf(path, sizeof(path), "%s/picked.z64", dataDir);
	f = fopen(path, "rb");
	if (!f) return 0;
	n = fread(hdr, 1, sizeof(hdr), f);
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fclose(f);
	if (n < 4) return 0;
	if (u32le(hdr) == 0x04034b50) return 1;
	if (n != sizeof(hdr) || size <= 0 || !gevrRomNormaliseOrder(hdr, sizeof(hdr))) return 0;
	switch (classify(hdr, (uint32_t)size, &name, &file)) {
	case KIND_GEX:
	case KIND_HACK:
		return 1;
	default:
		return 0;
	}
}

static int isPart(const char *name)
{
	size_t l = strlen(name);
	return l >= 5 && strcmp(name + l - 5, ".part") == 0;
}

int gevrRomImportPending(const char *dataDir)
{
	char dir[1024];
	DIR *d;
	struct dirent *e;
	int any = 0;
	if (!dataDir) return 0;
	if (pickedNeedsSort(dataDir)) return 1;
	snprintf(dir, sizeof(dir), "%s/" GEVR_ROM_IMPORT_DIR, dataDir);
	d = opendir(dir);
	if (!d) return 0;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.' || isPart(e->d_name)) continue;
		any = 1;
		break;
	}
	closedir(d);
	return any;
}

int gevrRomImportRun(const char *dataDir, char *summary, int summaryLen, int *gotGe)
{
	struct sorted s;
	char dir[1024], path[1400];
	DIR *d;
	struct dirent *e;
	memset(&s, 0, sizeof(s));
	if (gotGe) *gotGe = 0;
	if (summary && summaryLen > 0) summary[0] = 0;
	if (!dataDir) return 0;

	snprintf(dir, sizeof(dir), "%s/" GEVR_ROM_IMPORT_DIR, dataDir);
	d = opendir(dir);
	if (d) {
		while ((e = readdir(d)) != NULL) {
			if (e->d_name[0] == '.' || isPart(e->d_name)) continue;
			snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
			sortFile(dataDir, path, e->d_name, &s);
			remove(path);
		}
		closedir(d);
	}
	/* after the folder: a lone pick of a zip is in picked.z64, and a GoldenEye
	 * from the folder may just have been written there */
	if (pickedNeedsSort(dataDir)) {
		char picked[1100], moved[1100];
		snprintf(picked, sizeof(picked), "%s/picked.z64", dataDir);
		snprintf(moved, sizeof(moved), "%s/picked-sorting.bin", dataDir);
		remove(moved);
		if (rename(picked, moved) == 0) {
			sortFile(dataDir, moved, "the file chosen", &s);
			remove(moved);
		}
	}

	if (gotGe) *gotGe = s.gotGe;
	if (summary && summaryLen > 0) {
		if (s.added[0] && s.unknown[0])
			snprintf(summary, (size_t)summaryLen, "Added %s. Not recognised: %s.", s.added, s.unknown);
		else if (s.added[0])
			snprintf(summary, (size_t)summaryLen, "Added %s.", s.added);
		else if (s.unknown[0])
			snprintf(summary, (size_t)summaryLen, "No ROM this knows in %s.", s.unknown);
	}
	return s.filed;
}
