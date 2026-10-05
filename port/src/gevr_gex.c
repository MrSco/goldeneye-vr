/**
 * The player's GoldenEye X ROM (docs/gex-weapons.md; tools/gex/pdrom.py is the
 * same reader in Python).
 *
 * GE-X 6a keeps Perfect Dark NTSC 1.1's layout (pdvr port/src/romdata.c): the
 * data segment at 0x39850 is 1173-compressed (0x11 0x73, a 24-bit length, raw
 * deflate) and holds the file offset table at 0x28080, file 0 unused, ending
 * at a zero; the last offset is the name table's. Files are 1173-compressed.
 * Its texture table moved: the list is at 0x1e77400 (8-byte rows, the low 24
 * bits of the first word an offset into the data) and the data starts at
 * 0x1b449a2. Both are checked before use.
 *
 * The ROM stays resident once opened (32 MiB), only when a GE-X gun is in use.
 */

#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>
#include "fs.h"
#include "system.h"
#include "gevr_gex.h"
#include "../vr/miniz/miniz.h"

#define GEX_ROM_SIZE    0x2000000
#define GEX_DATA_OFS    0x39850
#define GEX_FILES_OFS   0x28080
#define GEX_TEXLIST_OFS 0x1e77400
#define GEX_TEXDATA_OFS 0x1b449a2
#define GEX_NUM_TEXTURES 3504
#define GEX_MAX_FILES   2048

static u8 *s_rom;
static u32 s_romSize;
static s32 s_tried;
static u32 s_numFiles;
static u32 s_fileOfs[GEX_MAX_FILES + 1];
static const char *s_fileName[GEX_MAX_FILES];

static u32 be32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static u32 be24(const u8 *p)
{
	return ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
}

s32 gevrGexInflate(const u8 *src, u32 srclen, u8 *dst, u32 dstlen)
{
	size_t got = tinfl_decompress_mem_to_mem(dst, dstlen, src, srclen, 0);

	return got == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED ? -1 : (s32)got;
}

const u8 *gevrGexInflateRzip(const u8 *src, u8 *dst, u32 dstlen)
{
	tinfl_decompressor inf;
	size_t inlen = 0x4000, outlen = dstlen;
	u32 len;

	if (src[0] != 0x11 || src[1] != 0x73) {
		return NULL;
	}
	len = be24(src + 2);
	if (len > dstlen) {
		return NULL;
	}
	tinfl_init(&inf);
	/* without TINFL_FLAG_HAS_MORE_INPUT tinfl hands back the bytes it read
	 * past the stream's end, so inlen is where the next field starts */
	if (tinfl_decompress(&inf, src + 5, &inlen, dst, dst, &outlen, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF) != TINFL_STATUS_DONE
			|| outlen != len) {
		return NULL;
	}
	return src + 5 + inlen;
}

/* a 1173 blob: the 24-bit length, then raw deflate */
static u8 *gexInflate1173(const u8 *src, u32 srclen, u32 *outSize)
{
	u32 len;
	u8 *out;

	if (srclen < 5 || src[0] != 0x11 || src[1] != 0x73) {
		return NULL;
	}
	len = be24(src + 2);
	out = (u8 *)malloc(len ? len : 1);
	if (out == NULL || gevrGexInflate(src + 5, srclen - 5, out, len) != (s32)len) {
		free(out);
		return NULL;
	}
	*outSize = len;
	return out;
}

static s32 gexReadFileTable(void)
{
	u32 segsize = 0, n, names;
	u8 *seg = gexInflate1173(s_rom + GEX_DATA_OFS, s_romSize - GEX_DATA_OFS, &segsize);

	if (seg == NULL || segsize < GEX_FILES_OFS + 8) {
		sysLogPrintf(LOG_ERROR, "gex: data segment is not 1173-compressed");
		free(seg);
		return 0;
	}
	for (n = 1; n <= GEX_MAX_FILES && GEX_FILES_OFS + 4 * n + 4 <= segsize; n++) {
		u32 ofs = be32(seg + GEX_FILES_OFS + 4 * n);

		if (ofs == 0) {
			break;
		}
		s_fileOfs[n - 1] = ofs;
	}
	free(seg);
	if (n < 3) {
		sysLogPrintf(LOG_ERROR, "gex: no file table");
		return 0;
	}
	s_numFiles = n - 2;   /* the last offset is the name table's */
	names = s_fileOfs[n - 2];
	if (names + 4 * n > s_romSize) {
		sysLogPrintf(LOG_ERROR, "gex: the file name table is past the ROM's end");
		return 0;
	}
	for (n = 1; n <= s_numFiles; n++) {
		u32 rel = be32(s_rom + names + 4 * n);

		if (rel == 0 || names + rel >= s_romSize) {
			break;
		}
		s_fileName[n - 1] = (const char *)s_rom + names + rel;
	}
	return 1;
}

static s32 gexCheckTextures(void)
{
	const u8 *row0 = s_rom + GEX_TEXLIST_OFS;
	const u8 *row1 = row0 + 8;

	/* the first row starts the data, the runtime pointer words are zero, the
	 * offsets climb, and texture 0's first byte is a sane header */
	return be24(row0 + 1) == 0 && be32(row0 + 4) == 0 && be32(row1 + 4) == 0
		&& be24(row1 + 1) > 0 && (s_rom[GEX_TEXDATA_OFS] & 0x3f) <= 7;
}

s32 gevrGexOpen(void)
{
	static const char *const names[] = { "data/gex.z64", "gex.z64" };
	u32 i, size = 0;
	u8 *data = NULL;

	if (s_rom != NULL || s_tried) {
		return s_rom != NULL;
	}
	s_tried = 1;
	for (i = 0; i < sizeof(names) / sizeof(names[0]) && data == NULL; i++) {
		data = (u8 *)fsFileLoad(names[i], &size);
	}
	if (data == NULL) {
		sysLogPrintf(LOG_NOTE, "gex: no GoldenEye X ROM (data/gex.z64)");
		return 0;
	}
	if (size != GEX_ROM_SIZE || data[0] != 0x80 || data[1] != 0x37
			|| memcmp(data + 0x20, "GoldenEye X", 11) != 0) {
		sysLogPrintf(LOG_ERROR, "gex: data/gex.z64 is not a GoldenEye X .z64 ROM (%u bytes)", size);
		free(data);
		return 0;
	}
	s_rom = data;
	s_romSize = size;
	if (!gexReadFileTable() || !gexCheckTextures()) {
		sysLogPrintf(LOG_ERROR, "gex: not GE-X 6a's layout (file or texture table)");
		free(s_rom);
		s_rom = NULL;
		return 0;
	}
	sysLogPrintf(LOG_NOTE, "gex: GoldenEye X ROM loaded, %u files", s_numFiles);
	return 1;
}

u8 *gevrGexFileLoad(const char *name, u32 *outSize)
{
	u32 n;

	if (!gevrGexOpen()) {
		return NULL;
	}
	for (n = 0; n < s_numFiles; n++) {
		if (s_fileName[n] != NULL && strcmp(s_fileName[n], name) == 0) {
			const u8 *src = s_rom + s_fileOfs[n];
			u32 len = s_fileOfs[n + 1] - s_fileOfs[n];
			u8 *out;

			if (len >= 5 && src[0] == 0x11 && src[1] == 0x73) {
				return gexInflate1173(src, len, outSize);
			}
			out = (u8 *)malloc(len ? len : 1);
			if (out != NULL) {
				memcpy(out, src, len);
				*outSize = len;
			}
			return out;
		}
	}
	sysLogPrintf(LOG_ERROR, "gex: no file %s", name);
	return NULL;
}

const u8 *gevrGexRom(u32 *size)
{
	if (!gevrGexOpen()) {
		return NULL;
	}
	*size = s_romSize;
	return s_rom;
}

const u8 *gevrGexTextureData(s32 texnum, u32 *outSize)
{
	u32 a, b;

	if (!gevrGexOpen() || texnum < 0 || texnum + 1 >= GEX_NUM_TEXTURES) {
		return NULL;
	}
	a = be24(s_rom + GEX_TEXLIST_OFS + 8 * texnum + 1);
	b = be24(s_rom + GEX_TEXLIST_OFS + 8 * (texnum + 1) + 1);
	if (b <= a) {
		return NULL;   /* no data */
	}
	*outSize = b - a;
	return s_rom + GEX_TEXDATA_OFS + a;
}
