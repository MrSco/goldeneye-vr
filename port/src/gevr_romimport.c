/*
 * Several ROMs from one pick (gevr_romimport.h): loose files, a .zip of them,
 * or a zip of everything (the ROMs, the GoldenEye XBLA release, the Community
 * Edition updater), told apart by what they hold.
 *
 * A zip is read through its directory, seeking, never whole: the release
 * makes one of several hundred MB. Each ROM in it is unpacked alone (32 MB at
 * most), and a zip inside it (a zip of ROMs, the CE updater) is read from
 * memory, one level deep. ROMs are put in .z64 order and written through a
 * .part file, so the launcher's checks never see half a ROM.
 */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include "gevr_romimport.h"
#include "gevr_romcheck.h"
#include "gevr_romlayout.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zlib.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define RMDIR(p) _rmdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir(p, 0755)
#define RMDIR(p) rmdir(p)
#endif

#define ROM_MIN (8u << 20)      /* the smallest campaign ROM is 12 MB */
#define ROM_MAX (64u << 20)     /* the largest a pick may hold */
#define NEST_MAX (192u << 20)   /* a zip inside a zip, read into memory: every campaign zipped is ~100 MB */
/* An archive this large is the release's (its .7z is 740 MB, its zip 372 MB);
 * a smaller one holds ROMs or something else, and a .7z of ROMs is not read. */
#define XBLA_MIN (200ull << 20)   /* above NEST_MAX: every zip read for ROMs is smaller */
#define TAKE_MAX 64             /* ROMs one pick may stage: a zip of thousands of copies fills no disk */
#define NOTE_NEEDS_BEAN ".needs-bean"   /* gevrRomImportTakeNote */
#define NOTE_MODS_BUSY ".mods-busy"
#define STAGING GEVR_ROM_IMPORT_DIR ".part"
#define CE_ENTRY "CEUpdate/files.diff"   /* gevr_bean_ce.c CE_DIFF_ENTRY */
#define CE_FILE "picked-ce.zip"

enum { KIND_NONE, KIND_GE, KIND_GE_MAYBE, KIND_GEX, KIND_HACK };

static uint32_t u16le(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t u32le(const uint8_t *p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static int isZip(const uint8_t *h) { return u32le(h) == 0x04034b50; }
static int isSeven(const uint8_t *h) { return !memcmp(h, "7z\xbc\xaf\x27\x1c", 6); }
static int isPackage(const uint8_t *h) { return !memcmp(h, "LIVE", 4) || !memcmp(h, "CON ", 4) || !memcmp(h, "PIRS", 4); }
static int isN64(const uint8_t *h)
{
	return !memcmp(h, "\x80\x37\x12\x40", 4) || !memcmp(h, "\x37\x80\x40\x12", 4) || !memcmp(h, "\x40\x12\x37\x80", 4);
}

static const char *baseName(const char *path)
{
	const char *s = strrchr(path, '/'), *b = strrchr(path, '\\');
	if (b && (!s || b > s)) s = b;
	return s ? s + 1 : path;
}

/* name ends in tail at a folder boundary, either separator, any case (gevr_bean_import.c zipNameEnds) */
static int nameEnds(const char *name, const char *tail)
{
	const size_t n = strlen(name), t = strlen(tail);
	size_t i;
	if (t > n || (t < n && name[n - t - 1] != '/' && name[n - t - 1] != '\\')) return 0;
	for (i = 0; i < t; i++) {
		char a = name[n - t + i], b = tail[i];
		a = a == '\\' ? '/' : (a >= 'A' && a <= 'Z' ? a + 32 : a);
		b = b >= 'A' && b <= 'Z' ? b + 32 : b;
		if (a != b) return 0;
	}
	return 1;
}

/* ---- what a ROM is -------------------------------------------------------- */

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

int gevrRomImportReplace(const char *from, const char *to)
{
#ifdef _WIN32
	/* rename() does not replace there, and remove-then-rename loses the old file if the move fails */
	wchar_t wf[1100], wt[1100];
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, from, -1, wf, 1100)
	    && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, to, -1, wt, 1100)
	    && MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING))
		return 1;
	/* not UTF-8, or not there as UTF-8: the ANSI code page, as fopen() and rename() read it
	 * (a launcher path from _fullpath, in a folder with an accent) */
	return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
#else
	return rename(from, to) == 0;
#endif
}

static int writeWhole(const char *path, const uint8_t *data, uint32_t size)
{
	char part[1100];
	FILE *f;
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
	if (!gevrRomImportReplace(part, path)) {
		remove(part);
		return 0;
	}
	return 1;
}

/* ---- reading: a file, or a zip held in memory ----------------------------- */

struct src {
	FILE *f;
	const uint8_t *mem;
	uint64_t size;
};

static int srcRead(const struct src *s, uint64_t off, void *buf, size_t n)
{
	if (off > s->size || s->size - off < n) return 0;
	if (s->mem) {
		memcpy(buf, s->mem + off, n);
		return 1;
	}
	return fseeko(s->f, (off_t)off, SEEK_SET) == 0 && fread(buf, 1, n, s->f) == n;
}

struct zent {
	uint32_t method, crc, csize, usize, local;
	char name[512];
};

/*
 * The first n bytes of an entry (a peek), or with whole the entry itself,
 * its size and CRC checked.
 */
static int entryRead(const struct src *s, const struct zent *e, uint8_t *out, uint32_t n, int whole)
{
	uint8_t local[30], *in;
	uint64_t ofs;
	z_stream zs;
	int res = Z_OK;
	uint32_t left;
	if (n > e->usize || !srcRead(s, e->local, local, sizeof(local)) || u32le(local) != 0x04034b50) return 0;
	ofs = (uint64_t)e->local + 30 + u16le(local + 26) + u16le(local + 28);
	if (e->method == 0) {
		if (e->csize != e->usize || !srcRead(s, ofs, out, n)) return 0;
		return !whole || crc32(0, out, n) == e->crc;
	}
	if (e->method != 8) return 0;
	in = (uint8_t *)malloc(1 << 16);
	if (!in) return 0;
	memset(&zs, 0, sizeof(zs));
	if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
		free(in);
		return 0;
	}
	zs.next_out = out;
	zs.avail_out = n;
	left = e->csize;
	while (zs.avail_out > 0 && res != Z_STREAM_END) {
		if (zs.avail_in == 0) {
			const uint32_t take = left < (1u << 16) ? left : (1u << 16);
			if (take == 0 || !srcRead(s, ofs, in, take)) break;
			ofs += take;
			left -= take;
			zs.next_in = in;
			zs.avail_in = take;
		}
		res = inflate(&zs, Z_NO_FLUSH);
		if (res != Z_OK && res != Z_STREAM_END) break;
	}
	inflateEnd(&zs);
	free(in);
	if (zs.avail_out != 0) return 0;
	return !whole || crc32(0, out, n) == e->crc;
}

/* ---- walking a pick ------------------------------------------------------- */

/*
 * What a pick holds, as the walk finds it: each ROM (in memory, any byte
 * order; rom() may change it) and each name it cannot use go to the
 * callbacks; the XBLA release and the CE updater are only noted, for their
 * own importers.
 */
struct walk {
	void (*rom)(struct walk *w, uint8_t *data, uint32_t size, const char *label);
	void (*unknown)(struct walk *w, const char *label);
	void *ctx;
	const char *dataDir;
	int keepCe;      /* write a CE updater found inside the zip to dataDir/picked-ce.zip */
	int seen;        /* ROMs and unknowns reported */
	int xbla;        /* the release: unpacked (default.xex), its package, or an archive of them */
	int ce;          /* the pick holds CEUpdate/files.diff itself */
	int ceNested;    /* a zip inside it did: written to cePath */
	char cePath[1024];
};

static void walkZip(const struct src *s, const char *label, struct walk *w, int depth);

static void romFound(struct walk *w, uint8_t *data, uint32_t size, const char *label)
{
	w->seen++;
	w->rom(w, data, size, label);
}

static void unknownFound(struct walk *w, const char *label)
{
	w->seen++;
	w->unknown(w, label);
}

static void walkEntry(const struct src *s, const struct zent *e, struct walk *w, int depth)
{
	const char *label = baseName(e->name);
	uint8_t head[6];
	uint8_t *data;
	if (nameEnds(e->name, CE_ENTRY)) {
		if (depth == 0) w->ce = 1;
		else w->ceNested = 1;   /* the caller keeps the zip it is in */
		return;
	}
	if (nameEnds(e->name, "default.xex")) {
		w->xbla = 1;
		return;
	}
	if (e->usize < sizeof(head) || !entryRead(s, e, head, sizeof(head), 0)) return;
	if (isN64(head) && e->usize >= ROM_MIN && e->usize <= ROM_MAX) {
		data = (uint8_t *)malloc(e->usize);
		if (data && entryRead(s, e, data, e->usize, 1)) romFound(w, data, e->usize, label);
		else unknownFound(w, label);
		free(data);
	} else if (isPackage(head) || ((isZip(head) || isSeven(head)) && e->usize >= XBLA_MIN)) {
		w->xbla = 1;   /* the release's package, or its own archive (gevr_bean_import.c zipRunNested) */
	} else if (isZip(head) && depth == 0 && e->usize <= NEST_MAX) {
		/* a zip of ROMs, or the CE updater, inside the zip picked */
		struct src inner;
		const int ceBefore = w->ceNested;
		data = (uint8_t *)malloc(e->usize);
		if (!data || !entryRead(s, e, data, e->usize, 1)) {
			free(data);
			unknownFound(w, label);
			return;
		}
		inner.f = NULL;
		inner.mem = data;
		inner.size = e->usize;
		w->ceNested = 0;
		walkZip(&inner, label, w, 1);
		if (w->ceNested && !w->cePath[0] && w->keepCe) {
			char path[1024];
			snprintf(path, sizeof(path), "%s/" CE_FILE, w->dataDir);
			if (writeWhole(path, data, e->usize)) snprintf(w->cePath, sizeof(w->cePath), "%s", path);
		}
		w->ceNested = w->ceNested || ceBefore;
		free(data);
	} else if (isZip(head) || isSeven(head)) {
		unknownFound(w, label);   /* a .7z, or a zip deeper or larger than is read: named, not taken */
	}
}

static void walkZip(const struct src *s, const char *label, struct walk *w, int depth)
{
	uint8_t *tail = NULL, *cd = NULL;
	uint32_t back, count, cdLen, cdOfs, i, at;
	int64_t eocd = -1;
	back = s->size < 0x10000 + 22 ? (uint32_t)s->size : 0x10000 + 22;
	tail = (uint8_t *)malloc(back ? back : 1);
	if (!tail || back < 22 || !srcRead(s, s->size - back, tail, back)) goto bad;
	for (i = 22; i <= back; i++) {
		if (u32le(tail + back - i) == 0x06054b50) {
			eocd = back - i;
			break;
		}
	}
	if (eocd < 0) goto bad;
	count = u16le(tail + eocd + 10);
	cdLen = u32le(tail + eocd + 12);
	cdOfs = u32le(tail + eocd + 16);
	if (count == 0xffff || cdOfs == 0xffffffffu) goto bad;   /* zip64 */
	cd = (uint8_t *)malloc(cdLen ? cdLen : 1);
	if (!cd || !srcRead(s, cdOfs, cd, cdLen)) goto bad;
	for (i = 0, at = 0; i < count; i++) {
		struct zent e;
		uint32_t nameLen;
		if (at > cdLen || cdLen - at < 46 || u32le(cd + at) != 0x02014b50) break;
		nameLen = u16le(cd + at + 28);
		if (cdLen - at - 46 < nameLen) break;
		e.method = u16le(cd + at + 10);
		e.crc = u32le(cd + at + 16);
		e.csize = u32le(cd + at + 20);
		e.usize = u32le(cd + at + 24);
		e.local = u32le(cd + at + 42);
		snprintf(e.name, sizeof(e.name), "%.*s", (int)(nameLen < sizeof(e.name) - 1 ? nameLen : sizeof(e.name) - 1),
		         (const char *)cd + at + 46);
		at += 46 + nameLen + u16le(cd + at + 30) + u16le(cd + at + 32);
		if (!e.name[0] || e.name[strlen(e.name) - 1] == '/' || e.name[strlen(e.name) - 1] == '\\') continue;
		walkEntry(s, &e, w, depth);
	}
	free(cd);
	free(tail);
	return;
bad:
	free(cd);
	free(tail);
	unknownFound(w, label);
}

/* One pick, a file: a ROM, a zip, the release, or something else. */
static void walkFile(FILE *f, const char *label, struct walk *w)
{
	struct src s;
	uint8_t head[6];
	const int seen = w->seen, xbla = w->xbla, ce = w->ce, ceNested = w->ceNested;
	s.f = f;
	s.mem = NULL;
	s.size = 0;
	if (fseeko(f, 0, SEEK_END) == 0) {
		const off_t end = ftello(f);
		s.size = end > 0 ? (uint64_t)end : 0;
	}
	if (!srcRead(&s, 0, head, sizeof(head))) {
		unknownFound(w, label);
		return;
	}
	if (isPackage(head) || (isSeven(head) && s.size >= XBLA_MIN)) {
		w->xbla = 1;
	} else if (isZip(head)) {
		walkZip(&s, label, w, 0);
		if (w->seen == seen && w->xbla == xbla && w->ce == ce && w->ceNested == ceNested) unknownFound(w, label);
	} else if (isN64(head) && s.size >= ROM_MIN && s.size <= ROM_MAX) {
		uint8_t *data = (uint8_t *)malloc((size_t)s.size);
		if (data && srcRead(&s, 0, data, (size_t)s.size)) romFound(w, data, (uint32_t)s.size, label);
		else unknownFound(w, label);
		free(data);
	} else {
		unknownFound(w, label);
	}
}

/* ---- the sort: what the launcher runs over picked-import/ ----------------- */

struct sorted {
	int filed, gotGe, geGood;
	char added[512];
	char unknown[256];
	char xbla[256];
	char ce[256];
	char needsBean[256];
	char busy[256];
};

static void listAdd(char *list, size_t cap, const char *item)
{
	size_t len = strlen(list);
	if (strstr(list, item)) return;   /* one GoldenEye in two places is one line */
	if (len + strlen(item) + 3 >= cap) return;
	if (len) strcat(list, ", ");
	strcat(list, item);
}

/* "3-notes.txt" -> "notes.txt": the pickers number what they stage */
static const char *shown(const char *label)
{
	const char *p = label;
	while (*p >= '0' && *p <= '9') p++;
	return p != label && *p == '-' && p[1] ? p + 1 : label;
}

static void sortRom(struct walk *w, uint8_t *data, uint32_t size, const char *label)
{
	struct sorted *s = (struct sorted *)w->ctx;
	const char *name = NULL, *file = NULL;
	char path[1024];
	int kind;
	if (!gevrRomNormaliseOrder(data, size)) {
		listAdd(s->unknown, sizeof(s->unknown), shown(label));
		return;
	}
	kind = classify(data, size, &name, &file);
	if (kind == KIND_NONE) {
		listAdd(s->unknown, sizeof(s->unknown), shown(label));
		return;
	}
	if (kind == KIND_GE_MAYBE && s->geGood) return;   /* a good one came first */
	snprintf(path, sizeof(path), "%s/%s", w->dataDir, file);
	if (!writeWhole(path, data, size)) {
		listAdd(s->unknown, sizeof(s->unknown), shown(label));
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

static void sortUnknown(struct walk *w, const char *label)
{
	struct sorted *s = (struct sorted *)w->ctx;
	listAdd(s->unknown, sizeof(s->unknown), shown(label));
}

static int endsWith(const char *name, const char *tail)
{
	const size_t n = strlen(name), t = strlen(tail);
	return n > t && !strcmp(name + n - t, tail);
}

/* a picker's note (gevrRomImportTakeNote): "3-Updater.zip.needs-bean" -> "Updater.zip" */
static int sortNote(const char *label, struct sorted *s)
{
	char name[256];
	char *list = endsWith(label, NOTE_NEEDS_BEAN) ? s->needsBean : endsWith(label, NOTE_MODS_BUSY) ? s->busy : NULL;
	size_t cap = list == s->needsBean ? sizeof(s->needsBean) : sizeof(s->busy);
	if (!list) return 0;
	snprintf(name, sizeof(name), "%s", shown(label));
	*strrchr(name, '.') = 0;
	listAdd(list, cap, name);
	return 1;
}

static void sortFile(const char *dataDir, const char *path, const char *label, struct sorted *s)
{
	struct walk w;
	FILE *f;
	if (sortNote(label, s)) return;
	f = fopen(path, "rb");
	memset(&w, 0, sizeof(w));
	w.rom = sortRom;
	w.unknown = sortUnknown;
	w.ctx = s;
	w.dataDir = dataDir;   /* keepCe stays 0: the pickers take the updater (gevrRomImportTake) */
	if (!f) {
		listAdd(s->unknown, sizeof(s->unknown), shown(label));
		return;
	}
	walkFile(f, label, &w);
	fclose(f);
	/* the pickers hand these on; one that reached the sort is named */
	if (w.xbla) listAdd(s->xbla, sizeof(s->xbla), shown(label));
	if (w.ce || w.ceNested) listAdd(s->ce, sizeof(s->ce), shown(label));
}

/* picked.z64 is sorted when it is a zip, the release, GoldenEye X or another campaign. */
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
	if (n < 6) return 0;
	if (isZip(hdr) || isSeven(hdr) || isPackage(hdr)) return 1;
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

static int isDir(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* a sorted pick goes; one that cannot (locked, a folder) is hidden, so the sort does not run again for it */
static void dropPick(const char *path, const char *dir, const char *name)
{
	char hidden[1400];
	if (remove(path) == 0) return;
	snprintf(hidden, sizeof(hidden), "%s/.%s", dir, name);
	if (rename(path, hidden) != 0) remove(path);
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
		char path[1400];
		if (e->d_name[0] == '.' || isPart(e->d_name)) continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		if (isDir(path)) continue;
		any = 1;
		break;
	}
	closedir(d);
	return any;
}

static void sentence(char *out, size_t cap, const char *list, const char *what)
{
	size_t len = strlen(out);
	if (!list[0] || len + 1 >= cap) return;
	snprintf(out + len, cap - len, "%s%s %s", len ? " " : "", list, what);
	if (!len && out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 'a' + 'A');
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
			if (isDir(path)) continue;
			sortFile(dataDir, path, e->d_name, &s);
			dropPick(path, dir, e->d_name);
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
		} else {
			remove(picked);   /* a copy the picker made: sorting it again each second helps nobody */
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
		if (strstr(s.unknown, ".7z") || strstr(s.unknown, ".7Z"))
			sentence(summary, (size_t)summaryLen, "A .7z of ROMs", "is not read here: unpack it, or zip the ROMs.");
		sentence(summary, (size_t)summaryLen, s.xbla, "is GoldenEye XBLA: import it under Mods, XBLA / Bean.");
		sentence(summary, (size_t)summaryLen, s.ce, "is the Community Edition updater: apply it under Mods, CE...");
		sentence(summary, (size_t)summaryLen, s.needsBean,
		         "holds the Community Edition updater, which patches Bean: choose your GoldenEye XBLA copy with it.");
		sentence(summary, (size_t)summaryLen, s.busy, "was not taken while Mods was busy: choose it again once it is done.");
	}
	return s.filed;
}

/* ---- the take: what a picker does with each file chosen ------------------- */

static int s_takeCount, s_takeRoms;

static void takeName(const char *label, char *out, size_t cap)
{
	size_t i, n = 0;
	char safe[200];
	label = baseName(label);
	for (i = 0; label[i] && n + 1 < sizeof(safe); i++) {
		const char c = label[i];
		safe[n++] = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
			|| c == ' ' || c == '.' || c == '_' || c == '(' || c == ')' || c == '+' || c == '-' ? c : '_';
	}
	safe[n] = 0;
	if (n >= 5 && !strcmp(safe + n - 5, ".part")) safe[n - 1] = '_';
	snprintf(out, cap, "%d-%s", s_takeCount++, safe[0] ? shown(safe) : "file");
}

static void takeRom(struct walk *w, uint8_t *data, uint32_t size, const char *label)
{
	char name[256], path[1400];
	if (s_takeRoms >= TAKE_MAX) return;
	takeName(label, name, sizeof(name));
	snprintf(path, sizeof(path), "%s/" STAGING "/%s", w->dataDir, name);
	if (writeWhole(path, data, size)) {
		((struct gevrRomImportTake *)w->ctx)->staged++;
		s_takeRoms++;
	}
}

/* a name only: the sort says it was not recognised */
static void takeUnknown(struct walk *w, const char *label)
{
	char name[256], path[1400];
	FILE *f;
	takeName(label, name, sizeof(name));
	snprintf(path, sizeof(path), "%s/" STAGING "/%s", w->dataDir, name);
	f = fopen(path, "wb");
	if (f) {
		fclose(f);
		((struct gevrRomImportTake *)w->ctx)->staged++;
	}
}

void gevrRomImportTakeBegin(const char *dataDir)
{
	char dir[1024], path[1400];
	DIR *d;
	struct dirent *e;
	s_takeCount = 0;
	s_takeRoms = 0;
	snprintf(dir, sizeof(dir), "%s/" STAGING, dataDir);
	MKDIR(dataDir);
	MKDIR(dir);
	d = opendir(dir);   /* what a pick cut short left */
	if (d) {
		while ((e = readdir(d)) != NULL) {
			if (e->d_name[0] == '.') continue;
			snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
			remove(path);
		}
		closedir(d);
	}
}

int gevrRomImportTake(FILE *src, const char *label, const char *dataDir, struct gevrRomImportTake *out)
{
	struct walk w;
	memset(out, 0, sizeof(*out));
	if (!src) return 0;
	memset(&w, 0, sizeof(w));
	w.rom = takeRom;
	w.unknown = takeUnknown;
	w.ctx = out;
	w.dataDir = dataDir;
	w.keepCe = 1;
	walkFile(src, label ? label : "file", &w);
	fclose(src);
	out->xbla = w.xbla;
	out->ce = w.ce;
	snprintf(out->cePath, sizeof(out->cePath), "%s", w.cePath);
	return 1;
}

int gevrRomImportTakeNote(const char *dataDir, const char *label, int note)
{
	char name[256], path[1400];
	FILE *f;
	takeName(label, name, sizeof(name));
	snprintf(path, sizeof(path), "%s/" STAGING "/%s%s", dataDir, name,
	         note == GEVR_ROM_IMPORT_NOTE_NEEDS_BEAN ? NOTE_NEEDS_BEAN : NOTE_MODS_BUSY);
	f = fopen(path, "wb");
	if (!f) return 0;
	fclose(f);
	return 1;
}

int gevrRomImportTakeEnd(const char *dataDir)
{
	char from[1024], to[1024], a[1400], b[1400];
	DIR *d;
	struct dirent *e;
	int moved = 0;
	snprintf(from, sizeof(from), "%s/" STAGING, dataDir);
	snprintf(to, sizeof(to), "%s/" GEVR_ROM_IMPORT_DIR, dataDir);
	MKDIR(to);
	d = opendir(from);
	if (!d) return 0;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.' || isPart(e->d_name)) continue;
		snprintf(a, sizeof(a), "%s/%s", from, e->d_name);
		snprintf(b, sizeof(b), "%s/%s", to, e->d_name);
		remove(b);
		if (rename(a, b) == 0) moved++;
		else remove(a);
	}
	closedir(d);
	RMDIR(from);
	return moved;
}

#ifdef __ANDROID__
#include <jni.h>
#include <unistd.h>

/* MainActivity.takeMainPick: the picker's three steps. fd is the picked document, seekable, handed over. */
JNIEXPORT void JNICALL Java_com_gevr_port_MainActivity_nativeRomImportTakeBegin(JNIEnv *env, jclass cls, jstring dataDir)
{
	const char *d = (*env)->GetStringUTFChars(env, dataDir, NULL);
	(void)cls;
	gevrRomImportTakeBegin(d);
	(*env)->ReleaseStringUTFChars(env, dataDir, d);
}

/* "xbla \t ce \t cePath \t staged", or "" when it could not be read */
JNIEXPORT jstring JNICALL Java_com_gevr_port_MainActivity_nativeRomImportTake(JNIEnv *env, jclass cls, jint fd,
		jstring label, jstring dataDir)
{
	struct gevrRomImportTake t;
	char out[1200] = "";
	const char *l = (*env)->GetStringUTFChars(env, label, NULL);
	const char *d = (*env)->GetStringUTFChars(env, dataDir, NULL);
	FILE *src = fdopen(fd, "rb");
	(void)cls;
	if (!src) close(fd);
	if (gevrRomImportTake(src, l, d, &t))
		snprintf(out, sizeof(out), "%d\t%d\t%s\t%d", t.xbla, t.ce, t.cePath, t.staged);
	(*env)->ReleaseStringUTFChars(env, label, l);
	(*env)->ReleaseStringUTFChars(env, dataDir, d);
	return (*env)->NewStringUTF(env, out);
}

JNIEXPORT void JNICALL Java_com_gevr_port_MainActivity_nativeRomImportTakeNote(JNIEnv *env, jclass cls, jstring dataDir,
		jstring label, jint note)
{
	const char *d = (*env)->GetStringUTFChars(env, dataDir, NULL);
	const char *l = (*env)->GetStringUTFChars(env, label, NULL);
	(void)cls;
	gevrRomImportTakeNote(d, l, note);
	(*env)->ReleaseStringUTFChars(env, label, l);
	(*env)->ReleaseStringUTFChars(env, dataDir, d);
}

JNIEXPORT jint JNICALL Java_com_gevr_port_MainActivity_nativeRomImportTakeEnd(JNIEnv *env, jclass cls, jstring dataDir)
{
	const char *d = (*env)->GetStringUTFChars(env, dataDir, NULL);
	int n;
	(void)cls;
	n = gevrRomImportTakeEnd(d);
	(*env)->ReleaseStringUTFChars(env, dataDir, d);
	return n;
}
#endif
