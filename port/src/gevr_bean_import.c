/*
 * Bean from the player's own GoldenEye XBLA copy.
 *
 * Nothing of the release ships with the port or is downloaded for it: the
 * player picks the copy they have, and the files the Bean loaders read are
 * written under texture-packs/ge007-bean/files (gevr_bean.c's first root).
 * This is Dab's Mod's way (gebean.c: "The player drops their copy ... An
 * archive's characters and heads, and nothing else of it, are unpacked once"),
 * and its code: the streamed solid 7z block is archive.c's
 * archive7zStreamFolder() and archiveExtract7zMatching(), the package reader
 * x360.c's STFS functions (dabs-mod 3b069f9, MIT; port/licenses/dabs-mod.txt).
 *
 * The copy comes in three forms, told apart by their first bytes, since a
 * picked document's name is whatever its provider says:
 *   - an archive of the unpacked release (default.xex and files/): the .7z
 *     DAB's ge-bean.md names, one solid LZMA2 block of 740MB, or a .zip;
 *   - the Xbox 360 package itself (LIVE, CON or PIRS; the retail copy's
 *     30BA9271... file);
 *   - an archive holding that package (GoldenEye_007_XBLA_Retail.zip). STFS
 *     is read by seeking, so the package is unpacked to a scratch file first
 *     and removed after, as DAB unpacks the Perfect Dark release's archive
 *     once before reading its package (xblaimport.c).
 *
 * Only Bean's own files are taken (gevrBeanImportWanted()): the meshes the
 * loaders draw, the two font atlases and the release's pictures. The
 * collision meshes (<name>_hits) stay behind, as in DAB: the game has its own.
 */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include "gevr_bean_import.h"
#include "gevr_bean_log.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <zlib.h>

#include "../external/lzma/7z.h"
#include "../external/lzma/7zAlloc.h"
#include "../external/lzma/7zCrc.h"
#include "../external/lzma/7zFile.h"
#include "../external/lzma/Lzma2Dec.h"
#include "../external/lzma/LzmaDec.h"

#define IMPORT_MAXNAME 512
#define IMPORT_MAXPATH 1024
// A Bean file is a few MB (Statue Park, the biggest, is 21MB); one bigger is
// not the release's
#define IMPORT_MAX_FILE (64u << 20)
// What the wanted files come to (about 300MB), with room
#define IMPORT_MAX_TOTAL (768ull << 20)
// The package: 739MB in the retail copy. A smaller entry is not it.
#define IMPORT_PACKAGE_MIN (64ull << 20)
#define IMPORT_PACKAGE_MAX (2ull << 30)
// Room left over after the wanted files and any scratch package
#define IMPORT_SPARE (64ull << 20)
#define IMPORT_CHUNK (1 << 20)

static volatile int s_percent;
static volatile int s_cancel;

struct job {
	const char *dest;
	char *msg;
	size_t msgLen;
	int written;
	int failed;           // and msg says why
	uint64_t total;       // bytes of wanted files written
	uint64_t done, goal;  // the phase's progress
	int lo, hi;           // the phase's share of the bar
	int pkgMode;          // the archive holds the package, not the files
	uint32_t pkgIndex;
	char scratch[IMPORT_MAXPATH];
};

int gevrBeanImportPercent(void)
{
	return s_percent;
}

void gevrBeanImportCancel(void)
{
	s_cancel = 1;
}

static void fail(struct job *j, const char *fmt, ...)
{
	va_list ap;

	if (j->failed) {
		return;
	}

	j->failed = 1;
	va_start(ap, fmt);
	vsnprintf(j->msg, j->msgLen, fmt, ap);
	va_end(ap);
	GEVR_BEAN_LOG("bean import: %s", j->msg);
}

static int cancelled(struct job *j)
{
	if (s_cancel) {
		fail(j, "Cancelled.");
	}

	return j->failed;
}

static void phase(struct job *j, uint64_t goal, int lo, int hi)
{
	j->done = 0;
	j->goal = goal;
	j->lo = lo;
	j->hi = hi;
	s_percent = lo;
}

static void progress(struct job *j, uint64_t add)
{
	j->done += add;

	if (j->goal) {
		const uint64_t done = j->done < j->goal ? j->done : j->goal;
		s_percent = j->lo + (int)((uint64_t)(j->hi - j->lo) * done / j->goal);
	}
}

/* ---- which files ------------------------------------------------------- */

// A folder name as the release spells them: lower case, digits and '_', and
// '.' inside a picture's second level (ModArchive's rule for the old pack)
static int segment(const char *s, size_t n, int dots)
{
	if (n == 0 || n > 64) {
		return 0;
	}

	for (size_t i = 0; i < n; i++) {
		const char c = s[i];

		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || (dots && c == '.' && i > 0))) {
			return 0;
		}
	}

	return 1;
}

// rel starts with files/
static int wantedRel(const char *rel)
{
	static const char *const kinds[] = { "background", "char", "gun", "head", "prop", "skydome" };
	const char *p;
	const char *slash;

	// files/new/<kind>/<name>/default.bin: what gevr_bean.c and gevr_bean_world.c load
	if (!strncmp(rel, "files/new/", 10)) {
		p = rel + 10;

		for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
			const size_t n = strlen(kinds[k]);

			if (!strncmp(p, kinds[k], n) && p[n] == '/') {
				const char *name = p + n + 1;

				slash = strchr(name, '/');

				return slash && segment(name, (size_t)(slash - name), 0) && !strcmp(slash, "/default.bin")
					&& !(slash - name > 5 && !strncmp(slash - 5, "_hits", 5));
			}
		}
	}

	// the menus' two fonts (gevr_bean_font.c)
	if (!strcmp(rel, "files/misc/alps3/default.dt") || !strcmp(rel, "files/misc/doc0/default.dt")) {
		return 1;
	}

	// files/(new/)texture/<name>(/<name>)/default.rba: the pictures (gevr_bean_images.c)
	p = rel + 6;

	if (!strncmp(p, "new/", 4)) {
		p += 4;
	}

	if (strncmp(p, "texture/", 8)) {
		return 0;
	}

	p += 8;
	slash = strchr(p, '/');

	if (!slash || !segment(p, (size_t)(slash - p), 0)) {
		return 0;
	}

	if (!strcmp(slash, "/default.rba")) {
		return 1;
	}

	p = slash + 1;
	slash = strchr(p, '/');

	return slash && segment(p, (size_t)(slash - p), 1) && !strcmp(slash, "/default.rba");
}

int gevrBeanImportWanted(const char *name, char *out, size_t outLen)
{
	char norm[IMPORT_MAXNAME];
	const size_t n = strlen(name);

	if (n == 0 || n >= sizeof(norm)) {
		return 0;
	}

	// '\\' read as '/' and without case, as DAB's archiveNameHas()
	for (size_t i = 0; i <= n; i++) {
		const char c = name[i] == '\\' ? '/' : name[i];
		norm[i] = c >= 'A' && c <= 'Z' ? (char)(c + 'a' - 'A') : c;
	}

	// files/ wherever the archive put the release's folder
	for (size_t at = 0; at + 6 <= n; at++) {
		if ((at == 0 || norm[at - 1] == '/') && !strncmp(norm + at, "files/", 6) && wantedRel(norm + at)) {
			if (n - at >= outLen) {
				return 0;
			}

			memcpy(out, norm + at, n - at + 1);
			return 1;
		}
	}

	return 0;
}

/* ---- writing ----------------------------------------------------------- */

struct out {
	FILE *fp;
	char path[IMPORT_MAXPATH];
	uint64_t size;
	uint64_t limit;
	unsigned char head[4];
	int nhead;
	int caff;  // one of the release's files, not the scratch package
};

static void makeParents(char *path)
{
	for (char *p = path + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(path, 0755);
			*p = '/';
		}
	}
}

static int outOpen(struct job *j, struct out *o, const char *path, uint64_t limit, int caff)
{
	memset(o, 0, sizeof(*o));
	snprintf(o->path, sizeof(o->path), "%s", path);
	makeParents(o->path);
	o->fp = fopen(o->path, "wb");
	o->limit = limit;
	o->caff = caff;

	if (!o->fp) {
		fail(j, "Could not write %s.", o->path);
		return 0;
	}

	return 1;
}

static int outWrite(struct job *j, struct out *o, const void *data, size_t n)
{
	const unsigned char *b = data;

	if (!o->fp) {
		return 1;  // decoded past
	}

	if (o->size + n > o->limit || (o->caff && j->total + n > IMPORT_MAX_TOTAL)) {
		fail(j, "The copy holds a file too big to be the release's (%s).", o->path);
		return 0;
	}

	for (size_t i = 0; o->nhead < 4 && i < n; i++) {
		o->head[o->nhead++] = b[i];
	}

	if (fwrite(data, 1, n, o->fp) != n) {
		fail(j, "Could not write %s: is the headset full?", o->path);
		return 0;
	}

	o->size += n;

	if (o->caff) {
		j->total += n;
	}

	return 1;
}

static int outClose(struct job *j, struct out *o, int complete)
{
	int ok;

	if (!o->fp) {
		return 1;
	}

	ok = fclose(o->fp) == 0 && complete && !j->failed;
	o->fp = NULL;

	// every file the loaders read is a CAFF container
	if (ok && o->caff && (o->nhead < 4 || memcmp(o->head, "CAFF", 4))) {
		fail(j, "That is not GoldenEye XBLA: %s is not one of its files.", o->path);
		ok = 0;
	}

	if (!ok) {
		remove(o->path);
		return 0;
	}

	if (o->caff) {
		j->written++;
	}

	return 1;
}

static int roomFor(struct job *j, uint64_t bytes)
{
	struct statvfs vfs;

	if (statvfs(j->dest, &vfs) != 0) {
		return 1;  // nothing to go on: let the writes say
	}

	if ((uint64_t)vfs.f_bavail * vfs.f_frsize < bytes + IMPORT_SPARE) {
		fail(j, "Not enough free space: this needs %llu MB.",
				(unsigned long long)((bytes + IMPORT_SPARE) >> 20));
		return 0;
	}

	return 1;
}

/* ---- the Xbox 360 package (DAB x360.c) ---------------------------------- */

#define STFS_DATA_START      0xc000
#define STFS_BLOCK           0x1000
#define STFS_BLOCKS_PER_HASH 0xaa
#define STFS_ENTRY_SIZE      0x40
#define STFS_MAX_FILES       0x8000
#define STFS_NAMELEN         40

struct stfsfile {
	char name[STFS_NAMELEN + 1];
	uint8_t isDir;
	uint8_t consecutive;
	uint32_t numBlocks;
	uint32_t startBlock;
	uint16_t parent;
	uint32_t size;
};

struct stfs {
	FILE *fp;
	uint32_t tableShift;
	uint32_t fileTableBlocks;
	uint32_t fileTableBlock;
	uint32_t totalBlocks;
	uint32_t numFiles;
	struct stfsfile *files;
};

static uint32_t stfsBE32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t stfsLE24(const uint8_t *p)
{
	return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static uint32_t stfsBackingBlock(struct stfs *stfs, uint32_t block)
{
	uint32_t adjust = 0;

	if (block >= 0xaa) {
		adjust += ((block / 0xaa) + 1) << stfs->tableShift;
	}

	if (block >= 0x70e4) {
		adjust += ((block / 0x70e4) + 1) << stfs->tableShift;
	}

	return block + adjust;
}

static uint64_t stfsBlockOffset(struct stfs *stfs, uint32_t block)
{
	return STFS_DATA_START + (uint64_t)stfsBackingBlock(stfs, block) * STFS_BLOCK;
}

static int stfsReadBlock(struct stfs *stfs, uint32_t block, uint8_t *dst)
{
	if (fseeko(stfs->fp, (off_t)stfsBlockOffset(stfs, block), SEEK_SET) != 0) {
		return 0;
	}

	return fread(dst, 1, STFS_BLOCK, stfs->fp) == STFS_BLOCK;
}

// The block after this one, from its level 0 hash table entry. Only needed for
// a file whose blocks are not consecutive, which in a read-only package is
// rare but not impossible.
static uint32_t stfsNextBlock(struct stfs *stfs, uint32_t block)
{
	const uint64_t table = stfsBlockOffset(stfs, block - (block % STFS_BLOCKS_PER_HASH)) - STFS_BLOCK;
	uint8_t entry[0x18];

	if (fseeko(stfs->fp, (off_t)(table + (block % STFS_BLOCKS_PER_HASH) * 0x18), SEEK_SET) != 0
			|| fread(entry, 1, sizeof(entry), stfs->fp) != sizeof(entry)) {
		return 0xffffff;
	}

	// The entry is 20 bytes of hash then a status byte, and the next block
	// number as 24 bits big-endian.
	return ((uint32_t)entry[0x15] << 16) | ((uint32_t)entry[0x16] << 8) | entry[0x17];
}

static void stfsClose(struct stfs *stfs)
{
	free(stfs->files);
	memset(stfs, 0, sizeof(*stfs));
}

// fp stays the caller's
static int stfsOpen(struct stfs *stfs, FILE *fp)
{
	uint8_t head[STFS_BLOCK];
	uint32_t headerSize;

	memset(stfs, 0, sizeof(*stfs));

	if (fseeko(fp, 0, SEEK_SET) != 0 || fread(head, 1, sizeof(head), fp) != sizeof(head)) {
		return 0;
	}

	if (memcmp(head, "LIVE", 4) && memcmp(head, "CON ", 4) && memcmp(head, "PIRS", 4)) {
		return 0;
	}

	stfs->fp = fp;

	// A read-only package keeps one hash table before each run of 0xaa
	// blocks; a read-write one keeps two and every table offset doubles. The
	// rounded header size is what says which: 0xb000 means one.
	headerSize = stfsBE32(head + 0x340);
	stfs->tableShift = (((headerSize + 0xfff) & 0xf000) >> 12) == 0xb ? 0 : 1;

	// Volume descriptor at 0x379: length, version, block separation, then the
	// file table's block count and block number. Reading these one byte out
	// is the classic mistake here - it turns a one block table of names into
	// 257 blocks of mostly garbage.
	stfs->fileTableBlocks = head[0x37c] | ((uint32_t)head[0x37d] << 8);
	stfs->fileTableBlock = stfsLE24(head + 0x37e);
	stfs->totalBlocks = stfsBE32(head + 0x395);

	if (stfs->fileTableBlocks == 0 || stfs->fileTableBlocks > 0x400) {
		stfsClose(stfs);
		return 0;
	}

	const uint32_t tableBytes = stfs->fileTableBlocks * STFS_BLOCK;
	uint8_t *table = malloc(tableBytes);

	if (!table) {
		stfsClose(stfs);
		return 0;
	}

	for (uint32_t i = 0; i < stfs->fileTableBlocks; i++) {
		if (!stfsReadBlock(stfs, stfs->fileTableBlock + i, table + i * STFS_BLOCK)) {
			free(table);
			stfsClose(stfs);
			return 0;
		}
	}

	const uint32_t slots = tableBytes / STFS_ENTRY_SIZE;
	stfs->files = calloc(slots, sizeof(*stfs->files));

	if (!stfs->files) {
		free(table);
		stfsClose(stfs);
		return 0;
	}

	for (uint32_t i = 0; i < slots && stfs->numFiles < STFS_MAX_FILES; i++) {
		const uint8_t *e = table + i * STFS_ENTRY_SIZE;
		const uint32_t flags = e[0x28];
		const uint32_t nameLen = flags & 0x3f;
		struct stfsfile *f;

		if (nameLen == 0) {
			continue;
		}

		f = &stfs->files[stfs->numFiles++];
		memcpy(f->name, e, nameLen > STFS_NAMELEN ? STFS_NAMELEN : nameLen);
		f->name[nameLen > STFS_NAMELEN ? STFS_NAMELEN : nameLen] = '\0';
		f->isDir = (flags & 0x80) ? 1 : 0;
		f->consecutive = (flags & 0x40) ? 1 : 0;
		f->numBlocks = stfsLE24(e + 0x29);
		f->startBlock = stfsLE24(e + 0x2f);
		f->parent = (uint16_t)((e[0x32] << 8) | e[0x33]);
		f->size = stfsBE32(e + 0x34);
	}

	free(table);

	return 1;
}

static const char *stfsPath(struct stfs *stfs, uint32_t index, char *buf, uint32_t bufLen)
{
	const char *parts[32];
	uint32_t numParts = 0;
	uint32_t at = index;
	uint32_t pos = 0;

	if (index >= stfs->numFiles) {
		buf[0] = '\0';
		return buf;
	}

	// Walk up to the root, bounded: a malformed table could name a cycle.
	while (numParts < 32) {
		parts[numParts++] = stfs->files[at].name;

		const uint16_t parent = stfs->files[at].parent;
		if (parent == 0xffff || parent >= stfs->numFiles) {
			break;
		}

		at = parent;
	}

	for (int32_t i = (int32_t)numParts - 1; i >= 0; i--) {
		const uint32_t len = (uint32_t)strlen(parts[i]);

		if (pos + len + 2 > bufLen) {
			break;
		}

		memcpy(buf + pos, parts[i], len);
		pos += len;

		if (i > 0) {
			buf[pos++] = '/';
		}
	}

	buf[pos] = '\0';

	return buf;
}

// One file's blocks, in order, into o (DAB's x360StfsRead, a block at a time)
static int stfsCopy(struct job *j, struct stfs *stfs, const struct stfsfile *f, struct out *o)
{
	uint8_t block[STFS_BLOCK];
	uint32_t left = f->size;
	uint32_t at = f->startBlock;

	for (uint32_t i = 0; i < f->numBlocks && left; i++) {
		const uint32_t take = left < STFS_BLOCK ? left : STFS_BLOCK;

		if (cancelled(j)) {
			return 0;
		}

		if (!stfsReadBlock(stfs, at, block)) {
			fail(j, "The package ends early: is it a whole copy?");
			return 0;
		}

		if (!outWrite(j, o, block, take)) {
			return 0;
		}

		left -= take;
		progress(j, take);
		at = f->consecutive ? at + 1 : stfsNextBlock(stfs, at);
	}

	if (left) {
		fail(j, "The package ends early: is it a whole copy?");
		return 0;
	}

	return 1;
}

static int stfsRun(struct job *j, FILE *fp, int lo, int hi)
{
	struct stfs stfs;
	char name[512];
	char rel[IMPORT_MAXNAME];
	char path[IMPORT_MAXPATH];
	uint64_t goal = 0;

	if (!stfsOpen(&stfs, fp)) {
		fail(j, "Could not read the Xbox 360 package.");
		return 0;
	}

	for (uint32_t i = 0; i < stfs.numFiles; i++) {
		if (!stfs.files[i].isDir && gevrBeanImportWanted(stfsPath(&stfs, i, name, sizeof(name)), rel, sizeof(rel))) {
			goal += stfs.files[i].size;
		}
	}

	if (goal == 0) {
		fail(j, "That package is not GoldenEye XBLA: it has no Bean files.");
		stfsClose(&stfs);
		return 0;
	}

	if (!roomFor(j, goal)) {
		stfsClose(&stfs);
		return 0;
	}

	phase(j, goal, lo, hi);

	for (uint32_t i = 0; i < stfs.numFiles && !j->failed; i++) {
		struct out o;

		if (stfs.files[i].isDir || !gevrBeanImportWanted(stfsPath(&stfs, i, name, sizeof(name)), rel, sizeof(rel))) {
			continue;
		}

		snprintf(path, sizeof(path), "%s/%s", j->dest, rel);

		if (outOpen(j, &o, path, IMPORT_MAX_FILE, 1)) {
			outClose(j, &o, stfsCopy(j, &stfs, &stfs.files[i], &o));
		}
	}

	stfsClose(&stfs);

	return !j->failed;
}

// The scratch package an archive held, read and removed
static int stfsRunScratch(struct job *j, int lo, int hi)
{
	FILE *fp = fopen(j->scratch, "rb");
	int ok;

	if (!fp) {
		fail(j, "Could not read the package back.");
		remove(j->scratch);
		return 0;
	}

	ok = stfsRun(j, fp, lo, hi);
	fclose(fp);
	remove(j->scratch);

	return ok;
}

/* ---- .7z (DAB archive.c) ------------------------------------------------ */

#define IMPORT_7Z_LZMA2 0x21
#define IMPORT_7Z_LZMA  0x30101

struct sevenz {
	CFileInStream stream;
	CLookToRead look;
	CSzArEx db;
	ISzAlloc alloc;
	ISzAlloc allocTemp;
};

/** Entry i's name as ASCII, the way archiveExtract7z() spells it. 0 if it has none. */
static int sevenName(const CSzArEx *db, UInt32 i, char *name)
{
	UInt16 utf16[IMPORT_MAXNAME];
	const size_t len = SzArEx_GetFileNameUtf16(db, i, NULL);

	if (len == 0 || len > IMPORT_MAXNAME) {
		return 0;
	}

	SzArEx_GetFileNameUtf16(db, i, utf16);

	for (size_t k = 0; k + 1 < len; k++) {
		name[k] = utf16[k] < 0x80 ? (char)utf16[k] : '_';
	}

	name[len - 1] = '\0';

	return 1;
}

// fp stays the caller's
static int sevenOpen(struct sevenz *a, FILE *fp)
{
	a->alloc.Alloc = SzAlloc;
	a->alloc.Free = SzFree;
	a->allocTemp.Alloc = SzAllocTemp;
	a->allocTemp.Free = SzFreeTemp;

	File_Construct(&a->stream.file);
	a->stream.file.file = fp;
	rewind(fp);

	FileInStream_CreateVTable(&a->stream);
	LookToRead_CreateVTable(&a->look, False);
	a->look.realStream = &a->stream.s;
	LookToRead_Init(&a->look);

	CrcGenerateTable();
	SzArEx_Init(&a->db);

	if (SzArEx_Open(&a->db, &a->look.s, &a->alloc, &a->allocTemp) != SZ_OK) {
		SzArEx_Free(&a->db, &a->alloc);
		return 0;
	}

	return 1;
}

/** The next entry at or after i that has data in folder fi, or NumFiles. */
static UInt32 sevenNextInFolder(const CSzArEx *db, UInt32 i, UInt32 fi)
{
	while (i < db->db.NumFiles && db->FileIndexToFolderIndexMap[i] == (UInt32)-1) {
		i++;
	}

	return i < db->db.NumFiles && db->FileIndexToFolderIndexMap[i] == fi ? i : db->db.NumFiles;
}

/** Where entry i goes: a wanted file under dest, or the scratch package. */
static int sevenTarget(struct job *j, const CSzArEx *db, UInt32 i, char *path, size_t len)
{
	char name[IMPORT_MAXNAME];
	char rel[IMPORT_MAXNAME];

	if (db->db.Files[i].IsDir) {
		return 0;
	}

	if (j->pkgMode) {
		if (i != j->pkgIndex) {
			return 0;
		}

		snprintf(path, len, "%s", j->scratch);
		return 1;
	}

	if (!sevenName(db, i, name) || !gevrBeanImportWanted(name, rel, sizeof(rel))) {
		return 0;
	}

	snprintf(path, len, "%s/%s", j->dest, rel);
	return 1;
}

/**
 * The file entry i goes to: opened when it is wanted, left closed (decoded
 * past) when not.
 */
static void sevenOpenEntry(struct job *j, const CSzArEx *db, UInt32 i, struct out *o)
{
	char path[IMPORT_MAXPATH];

	memset(o, 0, sizeof(*o));

	if (sevenTarget(j, db, i, path, sizeof(path))) {
		outOpen(j, o, path, j->pkgMode ? IMPORT_PACKAGE_MAX : IMPORT_MAX_FILE, !j->pkgMode);
	}
}

/**
 * One solid block, decoded as a stream into the wanted files it holds. -2 when
 * the block is not one this can stream, and the caller extracts it the SDK's
 * way; -1 on a fault.
 */
static int sevenStreamFolder(struct job *j, struct sevenz *a, UInt32 fi)
{
	const CSzArEx *db = &a->db;
	CSzFolder *folder = db->db.Folders + fi;
	const CSzCoderInfo *coder;
	CLzma2Dec lzma2;
	CLzmaDec lzma;
	int islzma2;
	UInt64 packLeft;
	UInt64 unpackLeft;
	UInt32 cur;
	UInt64 curLeft;
	struct out o;
	Byte *chunk;
	int failed = 0;
	int wantedLeft = 0;

	memset(&o, 0, sizeof(o));

	if (folder->NumCoders != 1 || folder->NumPackStreams != 1 || folder->NumBindPairs != 0) {
		return -2;
	}

	coder = &folder->Coders[0];

	if (coder->MethodID == IMPORT_7Z_LZMA2 && coder->Props.size == 1) {
		islzma2 = 1;
	} else if (coder->MethodID == IMPORT_7Z_LZMA && coder->Props.size == 5) {
		islzma2 = 0;
	} else {
		return -2;
	}

	packLeft = db->db.PackSizes[db->FolderStartPackStreamIndex[fi]];
	unpackLeft = SzFolder_GetUnpackSize(folder);

	if (LookInStream_SeekTo(&a->look.s, SzArEx_GetFolderStreamPos(db, fi, 0)) != SZ_OK) {
		return -1;
	}

	chunk = malloc(IMPORT_CHUNK);

	if (!chunk) {
		return -1;
	}

	if (islzma2) {
		Lzma2Dec_Construct(&lzma2);

		if (Lzma2Dec_Allocate(&lzma2, coder->Props.data[0], &a->alloc) != SZ_OK) {
			free(chunk);
			return -1;
		}

		Lzma2Dec_Init(&lzma2);
	} else {
		LzmaDec_Construct(&lzma);

		if (LzmaDec_Allocate(&lzma, coder->Props.data, 5, &a->alloc) != SZ_OK) {
			free(chunk);
			return -1;
		}

		LzmaDec_Init(&lzma);
	}

	// How many of the block's files are wanted, so decoding can stop after the
	// last of them rather than run on through the rest of the block: the
	// GoldenEye release is one block of 740MB, and what comes after its last
	// wanted file was most of the wait with nothing left to write.
	for (UInt32 i = sevenNextInFolder(db, db->FolderStartFileIndex[fi], fi);
			i < db->db.NumFiles; i = sevenNextInFolder(db, i + 1, fi)) {
		char path[IMPORT_MAXPATH];

		if (sevenTarget(j, db, i, path, sizeof(path))) {
			wantedLeft++;
		}
	}

	// The block's files in order, each taking the next Size bytes of it.
	cur = sevenNextInFolder(db, db->FolderStartFileIndex[fi], fi);
	curLeft = cur < db->db.NumFiles ? db->db.Files[cur].Size : 0;

	if (cur < db->db.NumFiles) {
		sevenOpenEntry(j, db, cur, &o);
	}

	while (!failed && !j->failed) {
		// Empty files, and the ones just finished, give way to the next.
		while (cur < db->db.NumFiles && curLeft == 0) {
			if (o.fp) {
				outClose(j, &o, 1);
				wantedLeft--;
			}

			cur = sevenNextInFolder(db, cur + 1, fi);

			if (cur < db->db.NumFiles) {
				curLeft = db->db.Files[cur].Size;
				sevenOpenEntry(j, db, cur, &o);
			}
		}

		if (unpackLeft == 0 || cur >= db->db.NumFiles || (wantedLeft <= 0 && !o.fp) || cancelled(j)) {
			break;
		}

		{
			const void *inBuf = NULL;
			size_t lookahead = 1 << 18;
			SizeT inProcessed;
			SizeT outLen = IMPORT_CHUNK;
			ELzmaStatus status;
			SRes res;

			if (lookahead > packLeft) {
				lookahead = (size_t)packLeft;
			}

			if (outLen > unpackLeft) {
				outLen = (SizeT)unpackLeft;
			}

			if (a->look.s.Look(&a->look.s, &inBuf, &lookahead) != SZ_OK) {
				failed = 1;
				break;
			}

			inProcessed = lookahead;
			res = islzma2
				? Lzma2Dec_DecodeToBuf(&lzma2, chunk, &outLen, inBuf, &inProcessed, LZMA_FINISH_ANY, &status)
				: LzmaDec_DecodeToBuf(&lzma, chunk, &outLen, inBuf, &inProcessed, LZMA_FINISH_ANY, &status);

			if (res != SZ_OK || (inProcessed == 0 && outLen == 0)) {
				failed = 1;
				break;
			}

			if (a->look.s.Skip(&a->look.s, inProcessed) != SZ_OK) {
				failed = 1;
				break;
			}

			packLeft -= inProcessed;
			unpackLeft -= outLen;
			progress(j, outLen);

			for (SizeT pos = 0; pos < outLen && cur < db->db.NumFiles; ) {
				const SizeT take = (UInt64)(outLen - pos) < curLeft ? outLen - pos : (SizeT)curLeft;

				if (!outWrite(j, &o, chunk + pos, take)) {
					failed = 1;
					break;
				}

				pos += take;
				curLeft -= take;

				while (cur < db->db.NumFiles && curLeft == 0) {
					if (o.fp) {
						outClose(j, &o, 1);
						wantedLeft--;
					}

					cur = sevenNextInFolder(db, cur + 1, fi);

					if (cur < db->db.NumFiles) {
						curLeft = db->db.Files[cur].Size;
						sevenOpenEntry(j, db, cur, &o);
					}
				}
			}
		}
	}

	outClose(j, &o, 0);

	if (islzma2) {
		Lzma2Dec_Free(&lzma2, &a->alloc);
	} else {
		LzmaDec_Free(&lzma, &a->alloc);
	}

	free(chunk);

	return failed || j->failed ? -1 : 0;
}

// What decoding has to get through: each wanted block up to the end of its last wanted file
static uint64_t sevenGoal(struct job *j, const CSzArEx *db)
{
	uint64_t goal = 0;

	for (UInt32 fi = 0; fi < db->db.NumFolders; fi++) {
		uint64_t run = 0;
		uint64_t last = 0;

		for (UInt32 i = sevenNextInFolder(db, db->FolderStartFileIndex[fi], fi);
				i < db->db.NumFiles; i = sevenNextInFolder(db, i + 1, fi)) {
			char path[IMPORT_MAXPATH];

			run += db->db.Files[i].Size;

			if (sevenTarget(j, db, i, path, sizeof(path))) {
				last = run;
			}
		}

		goal += last;
	}

	return goal;
}

static int sevenRun(struct job *j, FILE *fp)
{
	struct sevenz a;
	UInt32 blockIndex = 0xffffffff;
	Byte *outBuffer = NULL;
	size_t outBufferSize = 0;
	uint64_t wantedBytes = 0;
	uint64_t biggest = 0;

	if (!sevenOpen(&a, fp)) {
		fail(j, "Could not read that .7z.");
		return 0;
	}

	// The unpacked release, or the package?
	for (UInt32 i = 0; i < a.db.db.NumFiles; i++) {
		char name[IMPORT_MAXNAME];
		char rel[IMPORT_MAXNAME];

		if (a.db.db.Files[i].IsDir || !sevenName(&a.db, i, name)) {
			continue;
		}

		if (gevrBeanImportWanted(name, rel, sizeof(rel))) {
			wantedBytes += a.db.db.Files[i].Size;
		} else if (a.db.db.Files[i].Size > biggest) {
			biggest = a.db.db.Files[i].Size;
			j->pkgIndex = i;
		}
	}

	if (wantedBytes == 0) {
		if (biggest < IMPORT_PACKAGE_MIN || biggest > IMPORT_PACKAGE_MAX) {
			fail(j, "That archive is not GoldenEye XBLA: it has no Bean files.");
			SzArEx_Free(&a.db, &a.alloc);
			return 0;
		}

		j->pkgMode = 1;
		snprintf(j->scratch, sizeof(j->scratch), "%s/.package", j->dest);
	}

	if (!roomFor(j, j->pkgMode ? biggest + (400ull << 20) : wantedBytes)) {
		SzArEx_Free(&a.db, &a.alloc);
		return 0;
	}

	phase(j, sevenGoal(j, &a.db), 0, j->pkgMode ? 60 : 100);

	for (UInt32 fi = 0; fi < a.db.db.NumFolders && !j->failed; fi++) {
		int wanted = 0;
		int got;

		for (UInt32 i = sevenNextInFolder(&a.db, a.db.FolderStartFileIndex[fi], fi);
				i < a.db.db.NumFiles && !wanted; i = sevenNextInFolder(&a.db, i + 1, fi)) {
			char path[IMPORT_MAXPATH];
			wanted = sevenTarget(j, &a.db, i, path, sizeof(path));
		}

		if (!wanted) {
			continue;
		}

		got = sevenStreamFolder(j, &a, fi);

		if (got == 0) {
			continue;
		}

		if (got == -1) {
			fail(j, "That .7z is damaged: block %u would not decode.", (unsigned)fi);
			break;
		}

		// Not a block this streams: the SDK's own way, a file at a time.
		for (UInt32 i = sevenNextInFolder(&a.db, a.db.FolderStartFileIndex[fi], fi);
				i < a.db.db.NumFiles && !cancelled(j); i = sevenNextInFolder(&a.db, i + 1, fi)) {
			struct out o;
			size_t offset = 0;
			size_t outSize = 0;

			sevenOpenEntry(j, &a.db, i, &o);

			if (!o.fp) {
				continue;
			}

			if (SzArEx_Extract(&a.db, &a.look.s, i, &blockIndex, &outBuffer, &outBufferSize,
					&offset, &outSize, &a.alloc, &a.allocTemp) != SZ_OK) {
				fail(j, "That .7z is damaged: an entry would not decode.");
				outClose(j, &o, 0);
				break;
			}

			outClose(j, &o, outWrite(j, &o, outBuffer + offset, outSize));
			progress(j, outSize);
		}
	}

	IAlloc_Free(&a.alloc, outBuffer);
	SzArEx_Free(&a.db, &a.alloc);

	if (j->pkgMode) {
		if (j->failed) {
			remove(j->scratch);
			return 0;
		}

		return stfsRunScratch(j, 60, 100);
	}

	return !j->failed;
}

/* ---- .zip --------------------------------------------------------------- */

// Central directory signatures, little-endian on disk (DAB archive.c)
#define ZIP_EOCD_SIG   0x06054b50
#define ZIP_CDIR_SIG   0x02014b50
#define ZIP_LOCAL_SIG  0x04034b50
#define ZIP_EOCD_MINLEN 22
#define ZIP_EOCD_MAXBACK (ZIP_EOCD_MINLEN + 0xffff)

static uint32_t zipU32(const uint8_t *p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint32_t zipU16(const uint8_t *p) { return p[0] | (p[1] << 8); }

/**
 * Whether [ofs, ofs + len) lies inside a file of size bytes - subtracting, as
 * DAB's archiveFits(), so a large offset cannot wrap past the check.
 */
static int zipFits(uint64_t ofs, uint64_t len, uint64_t size)
{
	return ofs <= size && len <= size - ofs;
}

struct zipentry {
	char name[IMPORT_MAXNAME];
	uint32_t method;
	uint32_t crc;
	uint32_t csize;
	uint32_t usize;
	uint32_t localOfs;
};

/*
 * One entry, inflated as it is read rather than whole: DAB's zip reader holds
 * the archive in memory, which the retail zip (372MB) would not fit beside the
 * game on a headset.
 */
static int zipExtract(struct job *j, FILE *fp, uint64_t size, const struct zipentry *e,
		const char *path, uint64_t limit, int caff)
{
	uint8_t local[30];
	uint64_t dataOfs;
	uint64_t inLeft = e->csize;
	uint8_t *in;
	uint8_t *outBuf;
	uint32_t crc = crc32(0, NULL, 0);
	z_stream zs;
	struct out o;
	int ended = 0;

	if (!zipFits(e->localOfs, sizeof(local), size) || fseeko(fp, e->localOfs, SEEK_SET) != 0
			|| fread(local, 1, sizeof(local), fp) != sizeof(local) || zipU32(local) != ZIP_LOCAL_SIG) {
		fail(j, "That zip is damaged (%s).", e->name);
		return 0;
	}

	// The local header repeats the name and carries its own extra field,
	// which is not the same length as the one in the directory.
	dataOfs = (uint64_t)e->localOfs + 30 + zipU16(local + 26) + zipU16(local + 28);

	if (!zipFits(dataOfs, e->csize, size) || fseeko(fp, (off_t)dataOfs, SEEK_SET) != 0) {
		fail(j, "That zip is damaged (%s).", e->name);
		return 0;
	}

	if (e->method != 0 && e->method != 8) {
		fail(j, "That zip uses a compression this cannot read (%s).", e->name);
		return 0;
	}

	in = malloc(IMPORT_CHUNK);
	outBuf = malloc(IMPORT_CHUNK);
	memset(&zs, 0, sizeof(zs));

	if (!in || !outBuf || (e->method == 8 && inflateInit2(&zs, -MAX_WBITS) != Z_OK)) {
		free(in);
		free(outBuf);
		fail(j, "Out of memory.");
		return 0;
	}

	if (outOpen(j, &o, path, limit, caff)) {
		while (!ended && !cancelled(j)) {
			const size_t want = inLeft < IMPORT_CHUNK ? (size_t)inLeft : IMPORT_CHUNK;
			const size_t got = want ? fread(in, 1, want, fp) : 0;

			if (got != want) {
				fail(j, "That zip ends early: is it a whole copy?");
				break;
			}

			inLeft -= got;
			progress(j, got);

			if (e->method == 0) {
				crc = crc32(crc, in, (uInt)got);
				outWrite(j, &o, in, got);
				ended = inLeft == 0;
				continue;
			}

			zs.next_in = in;
			zs.avail_in = (uInt)got;

			do {
				int res;

				zs.next_out = outBuf;
				zs.avail_out = IMPORT_CHUNK;
				res = inflate(&zs, Z_NO_FLUSH);

				// (Z_BUF_ERROR: this input is used up and nothing was pending)
				if (res != Z_OK && res != Z_STREAM_END && res != Z_BUF_ERROR) {
					fail(j, "That zip is damaged (%s).", e->name);
					break;
				}

				const size_t n = IMPORT_CHUNK - zs.avail_out;
				crc = crc32(crc, outBuf, (uInt)n);

				if (!outWrite(j, &o, outBuf, n)) {
					break;
				}

				ended = res == Z_STREAM_END;
			} while (!ended && zs.avail_out == 0);

			if (j->failed || (!ended && inLeft == 0)) {
				fail(j, "That zip is damaged (%s).", e->name);
				break;
			}
		}

		if (!j->failed && (o.size != e->usize || crc != e->crc)) {
			fail(j, "That zip is damaged (%s): it does not check out.", e->name);
		}

		outClose(j, &o, ended && !j->failed);
	}

	if (e->method == 8) {
		inflateEnd(&zs);
	}

	free(in);
	free(outBuf);

	return !j->failed;
}

static int zipRun(struct job *j, FILE *fp)
{
	uint8_t *tail = NULL;
	uint8_t *cdir = NULL;
	struct zipentry *entries = NULL;
	uint64_t size;
	uint32_t back;
	int64_t eocd = -1;
	uint32_t numEntries;
	uint32_t numParsed = 0;
	uint32_t cdirOfs;
	uint32_t cdirLen;
	uint64_t wantedBytes = 0;
	uint64_t wantedPacked = 0;
	int package = -1;
	char rel[IMPORT_MAXNAME];
	char path[IMPORT_MAXPATH];

	if (fseeko(fp, 0, SEEK_END) != 0 || (size = (uint64_t)ftello(fp)) < ZIP_EOCD_MINLEN) {
		fail(j, "Could not read that zip.");
		return 0;
	}

	back = size < ZIP_EOCD_MAXBACK ? (uint32_t)size : ZIP_EOCD_MAXBACK;
	tail = malloc(back);

	if (!tail || fseeko(fp, (off_t)(size - back), SEEK_SET) != 0 || fread(tail, 1, back, fp) != back) {
		fail(j, "Could not read that zip.");
		goto done;
	}

	for (uint32_t i = ZIP_EOCD_MINLEN; i <= back; i++) {
		if (zipU32(tail + back - i) == ZIP_EOCD_SIG) {
			eocd = back - i;
			break;
		}
	}

	if (eocd < 0) {
		fail(j, "That zip has no directory: is it a whole copy?");
		goto done;
	}

	numEntries = zipU16(tail + eocd + 10);
	cdirLen = zipU32(tail + eocd + 12);
	cdirOfs = zipU32(tail + eocd + 16);

	if (numEntries == 0xffff || cdirOfs == 0xffffffff) {
		fail(j, "That zip is a zip64 this cannot read: unpack it and choose the file inside.");
		goto done;
	}

	if (!zipFits(cdirOfs, cdirLen, size) || !(cdir = malloc(cdirLen ? cdirLen : 1))
			|| !(entries = calloc(numEntries ? numEntries : 1, sizeof(*entries)))
			|| fseeko(fp, cdirOfs, SEEK_SET) != 0 || fread(cdir, 1, cdirLen, fp) != cdirLen) {
		fail(j, "Could not read that zip's directory.");
		goto done;
	}

	for (uint32_t i = 0, pos = 0; i < numEntries; i++) {
		struct zipentry *e = &entries[numParsed];
		uint32_t nameLen;

		if (!zipFits(pos, 46, cdirLen) || zipU32(cdir + pos) != ZIP_CDIR_SIG) {
			break;
		}

		nameLen = zipU16(cdir + pos + 28);

		if (nameLen == 0 || nameLen >= sizeof(e->name) || !zipFits(pos + 46, nameLen, cdirLen)) {
			break;
		}

		e->method = zipU16(cdir + pos + 10);
		e->crc = zipU32(cdir + pos + 16);
		e->csize = zipU32(cdir + pos + 20);
		e->usize = zipU32(cdir + pos + 24);
		e->localOfs = zipU32(cdir + pos + 42);
		memcpy(e->name, cdir + pos + 46, nameLen);
		e->name[nameLen] = '\0';
		pos += 46 + nameLen + zipU16(cdir + pos + 30) + zipU16(cdir + pos + 32);

		// A trailing separator is how zip stores a directory entry.
		if (e->name[nameLen - 1] == '/' || e->name[nameLen - 1] == '\\') {
			continue;
		}

		if (gevrBeanImportWanted(e->name, rel, sizeof(rel))) {
			wantedBytes += e->usize;
			wantedPacked += e->csize;
		} else if (package < 0 || e->usize > entries[package].usize) {
			package = (int)numParsed;
		}

		numParsed++;
	}

	if (wantedBytes == 0) {
		if (package < 0 || entries[package].usize < IMPORT_PACKAGE_MIN || entries[package].usize > IMPORT_PACKAGE_MAX) {
			fail(j, "That zip is not GoldenEye XBLA: it has no Bean files.");
			goto done;
		}

		if (!roomFor(j, (uint64_t)entries[package].usize + (400ull << 20))) {
			goto done;
		}

		snprintf(j->scratch, sizeof(j->scratch), "%s/.package", j->dest);
		phase(j, entries[package].csize, 0, 50);

		if (zipExtract(j, fp, size, &entries[package], j->scratch, IMPORT_PACKAGE_MAX, 0)) {
			stfsRunScratch(j, 50, 100);
		} else {
			remove(j->scratch);
		}

		goto done;
	}

	if (!roomFor(j, wantedBytes)) {
		goto done;
	}

	phase(j, wantedPacked, 0, 100);

	for (uint32_t i = 0; i < numParsed && !j->failed; i++) {
		if (gevrBeanImportWanted(entries[i].name, rel, sizeof(rel))) {
			snprintf(path, sizeof(path), "%s/%s", j->dest, rel);
			zipExtract(j, fp, size, &entries[i], path, IMPORT_MAX_FILE, 1);
		}
	}

done:
	free(entries);
	free(cdir);
	free(tail);

	return !j->failed;
}

/* ---- the copy ----------------------------------------------------------- */

int gevrBeanImportStream(FILE *src, const char *dest, char *msg, size_t msgLen)
{
	static const uint8_t sevenSig[6] = { '7', 'z', 0xbc, 0xaf, 0x27, 0x1c };
	struct job j;
	uint8_t magic[6] = { 0 };
	char probe[IMPORT_MAXPATH];
	struct stat st;

	memset(&j, 0, sizeof(j));
	j.dest = dest;
	j.msg = msg;
	j.msgLen = msgLen;
	msg[0] = '\0';
	s_cancel = 0;
	s_percent = 0;

	if (!src) {
		fail(&j, "Could not open that file.");
		return -1;
	}

	mkdir(dest, 0755);

	if (stat(dest, &st) != 0 || !S_ISDIR(st.st_mode)) {
		fail(&j, "Could not make %s.", dest);
	} else if (fread(magic, 1, sizeof(magic), src) != sizeof(magic)) {
		fail(&j, "That file is too small to be GoldenEye XBLA.");
	} else if (!memcmp(magic, sevenSig, sizeof(sevenSig))) {
		sevenRun(&j, src);
	} else if (!memcmp(magic, "PK\x03\x04", 4)) {
		zipRun(&j, src);
	} else if (!memcmp(magic, "LIVE", 4) || !memcmp(magic, "CON ", 4) || !memcmp(magic, "PIRS", 4)) {
		stfsRun(&j, src, 0, 100);
	} else {
		fail(&j, "Choose the GoldenEye XBLA .7z, .zip or Xbox 360 package file.");
	}

	fclose(src);

	// Bean draws nothing without its Bond (gevrBeanInit())
	snprintf(probe, sizeof(probe), "%s/files/new/char/boilerbond/default.bin", dest);

	if (!j.failed && stat(probe, &st) != 0) {
		fail(&j, "That copy has no Bean characters: is it GoldenEye XBLA?");
	}

	GEVR_BEAN_LOG("bean import: %d files, %llu MB%s%s", j.written, (unsigned long long)(j.total >> 20),
			j.failed ? ": " : "", j.failed ? msg : "");
	s_percent = 100;

	return j.failed ? -1 : j.written;
}

#ifdef __ANDROID__
#include <jni.h>

/*
 * ModManager's import job: fd is the picked document, opened by Java and
 * handed over (detachFd); dest the folder files/ goes in. "" when it worked,
 * else the reason to show.
 */
JNIEXPORT jstring JNICALL Java_com_gevr_port_ModManager_nativeBeanImport(JNIEnv *env, jclass cls, jint fd, jstring dest)
{
	char msg[256];
	const char *d = (*env)->GetStringUTFChars(env, dest, NULL);
	FILE *src = fdopen(fd, "rb");
	(void)cls;

	if (!src) {
		close(fd);
	}

	const int n = gevrBeanImportStream(src, d, msg, sizeof(msg));

	(*env)->ReleaseStringUTFChars(env, dest, d);

	return (*env)->NewStringUTF(env, n < 0 ? msg : "");
}

JNIEXPORT jint JNICALL Java_com_gevr_port_ModManager_nativeBeanImportPercent(JNIEnv *env, jclass cls)
{
	(void)env;
	(void)cls;
	return gevrBeanImportPercent();
}

JNIEXPORT void JNICALL Java_com_gevr_port_ModManager_nativeBeanImportCancel(JNIEnv *env, jclass cls)
{
	(void)env;
	(void)cls;
	gevrBeanImportCancel();
}
#endif
