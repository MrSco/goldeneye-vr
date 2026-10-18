/*
 * Wreck's Mirror Mode for Bean (XBLA) and HD packs: see gevr_mirror.h.
 */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gevr_mirror.h"
#include "gevr_romlayout.h"
#include "gevr_bean_int.h"

static const char *const s_props[] = {
#define GEVR_MIRROR_PROP(name) name,
#include "gevr_mirror_lists.inc.h"
#undef GEVR_MIRROR_PROP
};

static const unsigned short s_images[] = {
#define GEVR_MIRROR_IMAGE(id) id,
#include "gevr_mirror_lists.inc.h"
#undef GEVR_MIRROR_IMAGE
};

int gevrRomMirrored(void)
{
	return gevrRomCampaign() == GEVR_CAMPAIGN_MIRROR;
}

int gevrMirrorPropFlipped(const char *name)
{
	if (!name) {
		return 0;
	}
	for (unsigned i = 0; i < sizeof(s_props) / sizeof(s_props[0]); i++) {
		if (!strcmp(name, s_props[i])) {
			return 1;
		}
	}
	return 0;
}

int gevrMirrorBeanInPlace(const char *name)
{
	return name && !strcmp(name, "PlegalpageZ");
}

int gevrMirrorBeanFlips(const char *name)
{
	return gevrMirrorPropFlipped(name) && !gevrMirrorBeanInPlace(name);
}

int gevrMirrorImageFlipped(int image)
{
	for (unsigned i = 0; i < sizeof(s_images) / sizeof(s_images[0]); i++) {
		if (s_images[i] == image) {
			return 1;
		}
	}
	return 0;
}

static void swapBytes(unsigned char *a, unsigned char *b, int n)
{
	for (int i = 0; i < n; i++) {
		unsigned char t = a[i];
		a[i] = b[i];
		b[i] = t;
	}
}

/* a triangle turned over along axis: a to twice - a */
static void flipTri(struct gevrBeanMesh *mesh, int i, int axis, float twice)
{
	struct gevrBeanTri *t = &mesh->tris[i];
	for (int c = 0; c < 3; c++) {
		t->p[c * 3 + axis] = twice - t->p[c * 3 + axis];
		t->n[c * 3 + axis] = -t->n[c * 3 + axis];
	}
	swapBytes((unsigned char *)&t->p[3], (unsigned char *)&t->p[6], 3 * sizeof(float));
	swapBytes((unsigned char *)&t->n[3], (unsigned char *)&t->n[6], 3 * sizeof(float));
	swapBytes((unsigned char *)&t->uv[2], (unsigned char *)&t->uv[4], 2 * sizeof(float));
	swapBytes(&t->bone[4], &t->bone[8], 4);
	swapBytes(&t->weight[4], &t->weight[8], 4);
	swapBytes(&t->rgba[4], &t->rgba[8], 4);
	if (mesh->triSrc) {
		uint32_t v = mesh->triSrc[i].vi[1];
		mesh->triSrc[i].vi[1] = mesh->triSrc[i].vi[2];
		mesh->triSrc[i].vi[2] = v;
	}
}

struct mirrorCorner {
	int32_t key[3];
	int tri;
};

static int cornerCmp(const void *a, const void *b)
{
	const struct mirrorCorner *x = a, *y = b;
	for (int k = 0; k < 3; k++) {
		if (x->key[k] != y->key[k]) {
			return x->key[k] < y->key[k] ? -1 : 1;
		}
	}
	return 0;
}

static int findRoot(int *parent, int i)
{
	while (parent[i] != i) {
		parent[i] = parent[parent[i]];
		i = parent[i];
	}
	return i;
}

void gevrBeanMeshMirrorPieces(struct gevrBeanMesh *mesh, int axis)
{
	if (!mesh || mesh->ntris <= 0 || axis < 0 || axis > 2) {
		return;
	}
	const int n = mesh->ntris;
	struct mirrorCorner *corners = malloc((size_t)n * 3 * sizeof(*corners));
	int *parent = malloc((size_t)n * sizeof(int));
	float *lo = malloc((size_t)n * sizeof(float)), *hi = malloc((size_t)n * sizeof(float));
	if (!corners || !parent || !lo || !hi) {
		free(corners);
		free(parent);
		free(lo);
		free(hi);
		return;
	}
	/* pieces: triangles that share a corner */
	for (int i = 0; i < n; i++) {
		parent[i] = i;
		lo[i] = 1e30f;
		hi[i] = -1e30f;
		for (int c = 0; c < 3; c++) {
			for (int k = 0; k < 3; k++) {
				corners[i * 3 + c].key[k] = (int32_t)lroundf(mesh->tris[i].p[c * 3 + k] * 64.0f);
			}
			corners[i * 3 + c].tri = i;
		}
	}
	qsort(corners, (size_t)n * 3, sizeof(*corners), cornerCmp);
	for (int i = 1; i < n * 3; i++) {
		if (!cornerCmp(&corners[i - 1], &corners[i])) {
			int a = findRoot(parent, corners[i - 1].tri), b = findRoot(parent, corners[i].tri);
			if (a != b) {
				parent[a] = b;
			}
		}
	}
	/* each piece's extent along the axis, then each turned over about its middle */
	for (int i = 0; i < n; i++) {
		int r = findRoot(parent, i);
		for (int c = 0; c < 3; c++) {
			float v = mesh->tris[i].p[c * 3 + axis];
			if (v < lo[r]) lo[r] = v;
			if (v > hi[r]) hi[r] = v;
		}
	}
	for (int i = 0; i < n; i++) {
		int r = findRoot(parent, i);
		flipTri(mesh, i, axis, lo[r] + hi[r]);
	}
	free(corners);
	free(parent);
	free(lo);
	free(hi);
}

void gevrBeanMeshMirrorX(struct gevrBeanMesh *mesh)
{
	if (!mesh) {
		return;
	}
	for (int i = 0; i < mesh->ntris; i++) {
		struct gevrBeanTri *t = &mesh->tris[i];
		for (int c = 0; c < 3; c++) {
			t->p[c * 3] = -t->p[c * 3];
			t->n[c * 3] = -t->n[c * 3];
		}
		swapBytes((unsigned char *)&t->p[3], (unsigned char *)&t->p[6], 3 * sizeof(float));
		swapBytes((unsigned char *)&t->n[3], (unsigned char *)&t->n[6], 3 * sizeof(float));
		swapBytes((unsigned char *)&t->uv[2], (unsigned char *)&t->uv[4], 2 * sizeof(float));
		swapBytes(&t->bone[4], &t->bone[8], 4);
		swapBytes(&t->weight[4], &t->weight[8], 4);
		swapBytes(&t->rgba[4], &t->rgba[8], 4);
		if (mesh->triSrc) {
			uint32_t v = mesh->triSrc[i].vi[1];
			mesh->triSrc[i].vi[1] = mesh->triSrc[i].vi[2];
			mesh->triSrc[i].vi[2] = v;
		}
	}
	for (int b = 0; b < GEVR_BEAN_SK_COUNT; b++) {
		mesh->bind[b][0] = -mesh->bind[b][0];
	}
	for (int b = 0; b < 64; b++) {
		mesh->boneBind[b][0] = -mesh->boneBind[b][0];
	}
}

void gevrMirrorRgbaX(unsigned char *rgba, int w, int h)
{
	if (!rgba || w <= 1 || h <= 0) {
		return;
	}
	for (int y = 0; y < h; y++) {
		unsigned char *row = rgba + (size_t)y * w * 4;
		for (int x = 0; x < w / 2; x++) {
			swapBytes(row + x * 4, row + (w - 1 - x) * 4, 4);
		}
	}
}
