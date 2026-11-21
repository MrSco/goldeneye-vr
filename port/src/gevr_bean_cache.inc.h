/*
 * A Bean level's slow work kept on disk, as a driver keeps its shaders (#269):
 * the level passes (gevr_bean_load.c: twins, unlit faces, decals, shadows,
 * posters, cut-outs; ~1.1 s of Dam's 3.3 s load on a PC, several times that
 * on an Apple TV HD) and the rooms Bean's triangles fall in
 * (gevr_bean_world.c, ~0.65 s). Each file holds one result with the build
 * that made it and a hash of everything it was made from, so a new build, a
 * changed pack, Mirror Mode or anything else upstream only makes it again.
 * Nothing depends on a file being there: missing, short, damaged or stale,
 * the work is done as before and written again. The folder is gevr_cache in
 * the game's save folder (main.c; tvOS may empty Library/Caches, which is
 * fine) and keeps the newest GEVR_BEAN_CACHE_FILES.
 *
 * gevrBeanCacheStamp is the build id and gevrBeanCacheDir the folder (main.c);
 * NULL, as in the tests, turns the cache off.
 */
#ifndef GEVR_BEAN_CACHE_INC_H
#define GEVR_BEAN_CACHE_INC_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#define GEVR_BEAN_CACHE_FILES 8          /* four levels' two files */
#define GEVR_BEAN_CACHE_MAGIC 0x43425647u /* "GVBC" */
#define GEVR_BEAN_CACHE_VERSION 1u

extern const char *gevrBeanCacheStamp;
extern const char *gevrBeanCacheDir;

struct beanCacheHeader {
	uint32_t magic, version;
	char stamp[48];
	uint64_t key;
	uint64_t size;
	uint64_t check;
};

static uint64_t beanCacheFnv(uint64_t h, const void *p, size_t n)
{
	const uint8_t *b = (const uint8_t *)p;
	for (size_t i = 0; i < n; i++) {
		h ^= b[i];
		h *= 1099511628211ull;
	}
	return h;
}

/* a picture's reflection by its slot in mesh->reflection (+1), 0 for none, -1 if not there */
static int beanCacheReflection(const struct gevrBeanMesh *m, const void *r)
{
	if (!r)
		return 0;
	for (int k = 0; k < 256; k++)
		if (m->reflection[k] == r)
			return k + 1;
	return -1;
}

/* The triangles' contents field by field (not the struct's padding), the
 * reflection pointers by slot: the same each run */
static uint64_t beanCacheTris(const struct gevrBeanMesh *m, uint64_t h)
{
	h = beanCacheFnv(h, &m->ntris, sizeof(m->ntris));
	for (int i = 0; i < m->ntris; i++) {
		const struct gevrBeanTri *t = &m->tris[i];
		const int r = beanCacheReflection(m, t->reflection);
		h = beanCacheFnv(h, t->p, sizeof(t->p));
		h = beanCacheFnv(h, t->n, sizeof(t->n));
		h = beanCacheFnv(h, t->uv, sizeof(t->uv));
		h = beanCacheFnv(h, t->bone, sizeof(t->bone));
		h = beanCacheFnv(h, t->weight, sizeof(t->weight));
		h = beanCacheFnv(h, t->rgba, sizeof(t->rgba));
		h = beanCacheFnv(h, &r, sizeof(r));
		h = beanCacheFnv(h, &t->reflectionDiffuse, sizeof(t->reflectionDiffuse));
		h = beanCacheFnv(h, &t->reflectionAlphaBase, sizeof(t->reflectionAlphaBase));
		h = beanCacheFnv(h, &t->reflectionColour, sizeof(t->reflectionColour));
		h = beanCacheFnv(h, &t->cuff, sizeof(t->cuff));
		h = beanCacheFnv(h, &t->tex, sizeof(t->tex));
		h = beanCacheFnv(h, &t->alpha, sizeof(t->alpha));
		h = beanCacheFnv(h, &t->alphatest, sizeof(t->alphatest));
		h = beanCacheFnv(h, &t->sk, sizeof(t->sk));
		h = beanCacheFnv(h, &t->flags, sizeof(t->flags));
		h = beanCacheFnv(h, &t->side, sizeof(t->side));
	}
	return h;
}

static void beanCachePath(char *out, size_t n, const char *source, const char *kind)
{
	/* the pack's own folder name: .../background/dam/default.bin -> background_dam */
	char name[96] = { 0 };
	const char *s = strstr(source, "/new/");
	if (!s)
		s = strstr(source, "\\new\\");
	s = s ? s + 5 : source;
	size_t j = 0;
	for (; *s && j + 1 < sizeof(name); s++) {
		if (!strncmp(s, "/default.bin", 12) || !strncmp(s, "\\default.bin", 12))
			break;
		name[j++] = (*s == '/' || *s == '\\' || *s == ':' || *s == '.') ? '_' : *s;
	}
	snprintf(out, n, "%s/%s.%s", gevrBeanCacheDir, name, kind);
}

/* The payload, malloc'd, if a file for this key and build is whole; NULL otherwise */
static void *beanCacheRead(const char *source, const char *kind, uint64_t key, size_t *size)
{
	char path[512];
	struct beanCacheHeader h;
	void *data = NULL;
	FILE *f;
	if (!gevrBeanCacheStamp || !gevrBeanCacheDir)
		return NULL;
	beanCachePath(path, sizeof(path), source, kind);
	f = fopen(path, "rb");
	if (!f)
		return NULL;
	if (fread(&h, sizeof(h), 1, f) == 1 && h.magic == GEVR_BEAN_CACHE_MAGIC && h.version == GEVR_BEAN_CACHE_VERSION &&
	    !strncmp(h.stamp, gevrBeanCacheStamp, sizeof(h.stamp) - 1) && h.key == key && h.size > 0 &&
	    h.size < ((uint64_t)1 << 31)) {
		data = malloc((size_t)h.size);
		if (data && fread(data, 1, (size_t)h.size, f) == (size_t)h.size &&
		    beanCacheFnv(1469598103934665603ull, data, (size_t)h.size) == h.check) {
			*size = (size_t)h.size;
		} else {
			free(data);
			data = NULL;
		}
	}
	fclose(f);
	return data;
}

/* The newest GEVR_BEAN_CACHE_FILES files stay */
static void beanCacheTrim(void)
{
	char paths[64][512];
	time_t times[64];
	int n = 0;
	DIR *d = opendir(gevrBeanCacheDir);
	struct dirent *e;
	if (!d)
		return;
	while ((e = readdir(d)) != NULL && n < 64) {
		struct stat st;
		if (e->d_name[0] == '.')
			continue;
		snprintf(paths[n], sizeof(paths[n]), "%s/%s", gevrBeanCacheDir, e->d_name);
		if (stat(paths[n], &st) == 0) {
			times[n] = st.st_mtime;
			n++;
		}
	}
	closedir(d);
	while (n > GEVR_BEAN_CACHE_FILES) {
		int oldest = 0;
		for (int i = 1; i < n; i++)
			if (times[i] < times[oldest])
				oldest = i;
		remove(paths[oldest]);
		memmove(paths[oldest], paths[n - 1], sizeof(paths[0]));
		times[oldest] = times[n - 1];
		n--;
	}
}

static void beanCacheWrite(const char *source, const char *kind, uint64_t key, const void *data, size_t size)
{
	char path[512], tmp[528];
	struct beanCacheHeader h;
	FILE *f;
	if (!gevrBeanCacheStamp || !gevrBeanCacheDir || !data || !size)
		return;
#ifdef _WIN32
	_mkdir(gevrBeanCacheDir);
#else
	mkdir(gevrBeanCacheDir, 0755);
#endif
	beanCachePath(path, sizeof(path), source, kind);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	memset(&h, 0, sizeof(h));
	h.magic = GEVR_BEAN_CACHE_MAGIC;
	h.version = GEVR_BEAN_CACHE_VERSION;
	snprintf(h.stamp, sizeof(h.stamp), "%s", gevrBeanCacheStamp);
	h.key = key;
	h.size = size;
	h.check = beanCacheFnv(1469598103934665603ull, data, size);
	f = fopen(tmp, "wb");
	if (!f)
		return;
	const int ok = fwrite(&h, sizeof(h), 1, f) == 1 && fwrite(data, 1, size, f) == size;
	if (fclose(f) != 0 || !ok) {
		remove(tmp);
		return;
	}
	remove(path);
	if (rename(tmp, path) != 0) {
		remove(tmp);
		return;
	}
	beanCacheTrim();
}

#endif
