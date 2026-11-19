/* The first-person Bean glove uses the N64 hand's open shell. Its denser
 * rim needs its own stitches, and its added faces use the existing atlas.
 * The generator transfers the sculpted N64 fingers to the Bean rim. */
struct beanHandPoint {
	uint16_t corner;       /* triangle/corner ordinal within the glove atlas */
	int16_t offset[3];     /* 1/16 Bean unit, zero on the original rim */
};
struct beanHandVertex {
	uint16_t point, u, v;  /* atlas coordinates in units of 1/32768, V down */
	uint8_t shade;
};
struct beanHandPatch {
	int gloveTris;
	uint32_t fingerprint;
	const struct beanHandPoint *points;
	const struct beanHandVertex *vertices;
	const uint16_t (*faces)[3];
	int nfaces;
};

#include "gevr_bean_handpatch_data.inc.h"

static int beanGunHandTexture(const struct gevrBeanMesh *mesh, int tex)
{
	return tex >= 0 && tex < mesh->ntex && mesh->texW[tex] == 512 && mesh->texH[tex] == 511;
}

static uint32_t beanHandHash(uint32_t hash, const float *values, int count, float scale)
{
	for (int i = 0; i < count; i++)
		hash = (hash ^ (uint32_t)lroundf(values[i] * scale)) * 16777619u;
	return hash;
}

static void beanPatchGunHand(struct gevrBeanMesh *mesh)
{
	int tex = -1;
	for (int i = 0; i < mesh->ntex; i++) {
		if (!beanGunHandTexture(mesh, i))
			continue;
		if (tex >= 0 || !mesh->standin[i])
			return;
		tex = i;
	}
	if (tex < 0)
		return;

	/* Check the decoded vertex order and UV layout before following any
	 * corner reference. A different CAFF can use the same size atlas. */
	int glove[BEAN_HAND_MAX_TRIS], count = 0;
	uint32_t hash = 2166136261u;
	for (int i = 0; i < mesh->ntris; i++) {
		const struct gevrBeanTri *t = &mesh->tris[i];
		if (t->tex != tex)
			continue;
		if (count == BEAN_HAND_MAX_TRIS)
			return;
		glove[count++] = i;
		hash = beanHandHash(hash, t->p, 9, 16.0f);
		hash = beanHandHash(hash, t->uv, 6, 32768.0f);
	}
	const struct beanHandPatch *patch = NULL;
	for (size_t i = 0; i < sizeof(s_beanHandPatches) / sizeof(s_beanHandPatches[0]); i++) {
		if (s_beanHandPatches[i].gloveTris == count && s_beanHandPatches[i].fingerprint == hash) {
			patch = &s_beanHandPatches[i];
			break;
		}
	}
	if (!patch)
		return;

	const int total = mesh->ntris + patch->nfaces;
	if (total > mesh->tricaps) {
		struct gevrBeanTri *grown = realloc(mesh->tris, (size_t)total * sizeof(*grown));
		if (!grown)
			return;
		mesh->tris = grown;
		mesh->tricaps = total;
	}
	const struct gevrBeanTri *material = &mesh->tris[glove[0]];
	for (int i = 0; i < patch->nfaces; i++) {
		struct gevrBeanTri *t = &mesh->tris[mesh->ntris++];
		*t = *material;
		for (int c = 0; c < 3; c++) {
			const struct beanHandVertex *v = &patch->vertices[patch->faces[i][c]];
			const struct beanHandPoint *point = &patch->points[v->point];
			const struct gevrBeanTri *anchor = &mesh->tris[glove[point->corner / 3]];
			const int from = point->corner % 3;
			int onRim = !(point->offset[0] || point->offset[1] || point->offset[2]);
			for (int k = 0; k < 3; k++) {
				t->p[c * 3 + k] = anchor->p[from * 3 + k] + point->offset[k] / 16.0f;
				t->n[c * 3 + k] = anchor->n[from * 3 + k];
				const int shade = anchor->rgba[from * 4 + k];
				t->rgba[c * 4 + k] = (onRim ? shade : (shade + 164) / 2) * v->shade / 255;
			}
			t->uv[c * 2] = v->u / 32768.0f;
			t->uv[c * 2 + 1] = -v->v / 32768.0f;
		}
	}
}
