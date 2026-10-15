/* DAB assigns remake triangles to the nearest original room triangle rather
 * than overlapping room boxes. Here the reference comes from GoldenEye's
 * separate 1172 vertex/list streams (not Perfect Dark's room block format).
 * A temporary BVH keeps the all-room comparison affordable on Quest. */
#include "ob.h"
#include "gevr_romcheck.h"
extern struct levelentry levelinfotable[];
extern s32 levelentry_index;

struct beanRoomTri {
	float p[3][3], lo[3], hi[3];
	int room;
	uint8_t opaque, culled;   /* the primary (opaque) list's; drawn under G_CULL_BACK */
	/* its texture number (the list's G_NOOP, tex.c), -1 for none; its
	 * vertices' s and t (s10.5) and the brightest of their colours */
	int16_t tex;
	uint8_t shade;
	int16_t st[3][2];
};
struct beanRoomTree {
	float lo[3], hi[3];
	int begin, end, left, right;
};
static struct beanRoomTri *s_reference;
static struct beanRoomTree *s_tree;
static int s_referenceCount, s_referenceCap, s_treeCount, s_roomSortAxis;
static uint8_t s_referenceOpaque, s_referenceCulled;   /* the list being read, its geometry mode */
static int s_referenceTex;                             /* and its texture number */
static int16_t s_referenceSt[16][2];                   /* and its loaded vertices' s, t and shade */
static uint8_t s_referenceShade[16];

static unsigned roomBe32(const uint8_t *p)
{
	return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3];
}
static int roomBe16(const uint8_t *p)
{
	return (int16_t)((unsigned)p[0] << 8 | p[1]);
}

static uint8_t *roomReferenceStream(void *address, int size, unsigned *length)
{
	*length = 0;
	if (!address || size <= 0 || size > 1024 * 1024)
		return NULL;
	unsigned aligned = (size + 15) & ~15u;
	uint8_t *packed = malloc(aligned), *raw = malloc(1024 * 1024);
	if (!packed || !raw) {
		free(packed);
		free(raw);
		return NULL;
	}
	obLoadBGFileBytesAtOffset(levelinfotable[levelentry_index].bg_seg_filename, packed,
	                          (uintptr_t)address & 0xffffffu, aligned);
	int ok = gevrInflate1172(packed, size, raw, 1024 * 1024, length) == 0;
	free(packed);
	if (!ok) {
		free(raw);
		return NULL;
	}
	return raw;
}

static int roomReferenceTri(int room, const float cache[16][3], unsigned valid, int a, int b, int c)
{
	if (a >= 16 || b >= 16 || c >= 16 || a < 0 || b < 0 || c < 0 || !(valid & (1u << a)) ||
	    !(valid & (1u << b)) || !(valid & (1u << c)))
		return 1;
	if (s_referenceCount >= 250000)
		return 0;
	if (s_referenceCount == s_referenceCap) {
		int cap = s_referenceCap ? s_referenceCap * 2 : 4096;
		struct beanRoomTri *grown = realloc(s_reference, cap * sizeof(*grown));
		if (!grown)
			return 0;
		s_reference = grown;
		s_referenceCap = cap;
	}
	struct beanRoomTri *t = &s_reference[s_referenceCount++];
	int index[3] = {a, b, c};
	t->room = room;
	t->opaque = s_referenceOpaque;
	t->culled = s_referenceCulled;
	t->tex = (int16_t)s_referenceTex;
	t->shade = 0;
	for (int v = 0; v < 3; v++) {
		t->st[v][0] = s_referenceSt[index[v]][0];
		t->st[v][1] = s_referenceSt[index[v]][1];
		if (s_referenceShade[index[v]] > t->shade)
			t->shade = s_referenceShade[index[v]];
	}
	for (int k = 0; k < 3; k++) {
		t->lo[k] = 1e30f;
		t->hi[k] = -1e30f;
		for (int v = 0; v < 3; v++) {
			float p = cache[index[v]][k];
			t->p[v][k] = p;
			if (p < t->lo[k])
				t->lo[k] = p;
			if (p > t->hi[k])
				t->hi[k] = p;
		}
	}
	return 1;
}

static int roomReferenceLists(int room, const uint8_t *vertices, unsigned vlen, const uint8_t *gdl,
                              unsigned glen, unsigned fileOffset)
{
	float cache[16][3];
	unsigned valid = 0, pc = 0, stack[32];
	int depth = 0;
	s_referenceCulled = 0;
	s_referenceTex = -1;
	for (unsigned guard = 0; pc + 8 <= glen && guard < 262144; guard++) {
		unsigned w0 = roomBe32(gdl + pc), w1 = roomBe32(gdl + pc + 4), op = w0 >> 24;
		pc += 8;
		/* the cartridge's F3D geometry mode: G_CULL_BACK is 0x2000 there */
		if (op == (G_SETGEOMETRYMODE & 255) && (w1 & 0x2000))
			s_referenceCulled = 1;
		else if (op == (G_CLEARGEOMETRYMODE & 255) && (w1 & 0x2000))
			s_referenceCulled = 0;
		/* the cartridge's G_NOOP names the texture the triangles after it use (tex.c) */
		if (op == 0xc0)
			s_referenceTex = (int)(w1 & 0xfff);
		if (op == (G_VTX & 255)) {
			unsigned first = (w0 >> 16) & 15, count = (w0 & 0xffff) / 16, offset = w1 & 0xffffff;
			if (first + count > 16 || offset > vlen || count > (vlen - offset) / 16) {
				valid = 0;
				continue;
			}
			for (unsigned v = 0; v < count; v++) {
				const uint8_t *raw = vertices + offset + 16 * v;
				for (int k = 0; k < 3; k++)
					cache[first + v][k] = roomBe16(raw + 2 * k) +
					                      ptr_bgdata_room_fileposition_list[room].pos.f[k];
				s_referenceSt[first + v][0] = (int16_t)roomBe16(raw + 8);
				s_referenceSt[first + v][1] = (int16_t)roomBe16(raw + 10);
				s_referenceShade[first + v] = raw[12] > raw[13] ? (raw[12] > raw[14] ? raw[12] : raw[14])
				                                                : (raw[13] > raw[14] ? raw[13] : raw[14]);
				valid |= 1u << (first + v);
			}
		} else if (op == (G_TRI1 & 255)) {
			if (!roomReferenceTri(room, cache, valid, (w1 >> 16 & 255) / 10, (w1 >> 8 & 255) / 10,
			                      (w1 & 255) / 10))
				return 0;
		} else if (op == 0xb1) {
			for (int i = 0; i < 4; i++) {
				int a = w1 >> (i * 8) & 15, b = w1 >> (i * 8 + 4) & 15, c = w0 >> (i * 4) & 15;
				if ((a || b || c) && !roomReferenceTri(room, cache, valid, a, b, c))
					return 0;
			}
		} else if (op == (G_DL & 255)) {
			unsigned offset = (w1 & 0xffffff) - fileOffset;
			if (offset < glen && !(offset & 7)) {
				if (!(w0 & 0x10000)) {
					if (depth == 32)
						return 0;
					stack[depth++] = pc;
				}
				pc = offset;
			}
		} else if (op == (G_ENDDL & 255)) {
			if (!depth)
				break;
			pc = stack[--depth];
		}
	}
	return 1;
}

static int roomReferenceCompare(const void *a, const void *b)
{
	const struct beanRoomTri *x = a, *y = b;
	int k = s_roomSortAxis;
	float d = (x->lo[k] + x->hi[k]) - (y->lo[k] + y->hi[k]);
	return (d > 0) - (d < 0);
}
static int roomReferenceTree(int begin, int end)
{
	int index = s_treeCount++;
	struct beanRoomTree *t = &s_tree[index];
	t->begin = begin;
	t->end = end;
	t->left = t->right = -1;
	for (int k = 0; k < 3; k++) {
		t->lo[k] = 1e30f;
		t->hi[k] = -1e30f;
		for (int i = begin; i < end; i++) {
			if (s_reference[i].lo[k] < t->lo[k])
				t->lo[k] = s_reference[i].lo[k];
			if (s_reference[i].hi[k] > t->hi[k])
				t->hi[k] = s_reference[i].hi[k];
		}
	}
	if (end - begin > 8) {
		s_roomSortAxis = 0;
		for (int k = 1; k < 3; k++)
			if (t->hi[k] - t->lo[k] > t->hi[s_roomSortAxis] - t->lo[s_roomSortAxis])
				s_roomSortAxis = k;
		qsort(s_reference + begin, end - begin, sizeof(*s_reference), roomReferenceCompare);
		int middle = begin + (end - begin) / 2;
		t->left = roomReferenceTree(begin, middle);
		t->right = roomReferenceTree(middle, end);
	}
	return index;
}
static void roomReferenceFree(void)
{
	free(s_reference);
	free(s_tree);
	s_reference = NULL;
	s_tree = NULL;
	s_referenceCount = s_referenceCap = s_treeCount = 0;
}
static int roomReferenceBuild(int rooms)
{
	roomReferenceFree();
	for (int r = 1; r < rooms; r++) {
		bg_room_data *data = &ptr_bgdata_room_fileposition_list[r];
		s_room_info *info = &g_BgRoomInfo[r];
		unsigned vlen = 0;
		uint8_t *v = roomReferenceStream(data->pPointTableBin, info->csize_point_index_binary, &vlen);
		if (!v)
			continue;
		for (int pass = 0; pass < 2; pass++) {
			void *address = pass ? data->pSecMappingBin : data->pPriMappingBin;
			s_referenceOpaque = pass == 0;
			unsigned length = 0;
			uint8_t *g = roomReferenceStream(
			    address, pass ? info->csize_secondary_DL_binary : info->csize_primary_DL_binary, &length);
			int ok = !g || roomReferenceLists(r, v, vlen, g, length, (uintptr_t)address & 0xffffff);
			free(g);
			if (!ok) {
				free(v);
				roomReferenceFree();
				return 0;
			}
		}
		free(v);
	}
	if (!s_referenceCount)
		return 0;
	s_tree = malloc(s_referenceCount * 2 * sizeof(*s_tree));
	if (!s_tree) {
		roomReferenceFree();
		return 0;
	}
	roomReferenceTree(0, s_referenceCount);
	return 1;
}

/*
 * The nearest of GoldenEye's opaque room triangles along a ray from o (d a
 * unit, so t is level units; DAB gebeanstage.c shellRay): 1 the side
 * G_CULL_BACK keeps, or a triangle GoldenEye draws two-sided; -1 its back.
 */
static void roomRayNode(int node, const float o[3], const float d[3], const float inv[3], float *best, int *facing)
{
	const struct beanRoomTree *n = &s_tree[node];
	float t0 = 0.0f, t1 = *best;
	for (int k = 0; k < 3; k++) {
		float a = (n->lo[k] - o[k]) * inv[k], b = (n->hi[k] - o[k]) * inv[k];
		if (a > b) {
			const float t = a;
			a = b;
			b = t;
		}
		t0 = a > t0 ? a : t0;
		t1 = b < t1 ? b : t1;
		if (t0 > t1)
			return;
	}
	if (n->left >= 0) {
		roomRayNode(n->left, o, d, inv, best, facing);
		roomRayNode(n->right, o, d, inv, best, facing);
		return;
	}
	for (int i = n->begin; i < n->end; i++) {
		const struct beanRoomTri *t = &s_reference[i];
		float e1[3], e2[3], p[3], s[3], q[3];
		if (!t->opaque)
			continue;
		for (int k = 0; k < 3; k++) {
			e1[k] = t->p[1][k] - t->p[0][k];
			e2[k] = t->p[2][k] - t->p[0][k];
			s[k] = o[k] - t->p[0][k];
		}
		p[0] = d[1] * e2[2] - d[2] * e2[1];
		p[1] = d[2] * e2[0] - d[0] * e2[2];
		p[2] = d[0] * e2[1] - d[1] * e2[0];
		const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
		if (det > -1e-6f && det < 1e-6f)
			continue;
		const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) / det;
		if (u < 0.0f || u > 1.0f)
			continue;
		q[0] = s[1] * e1[2] - s[2] * e1[1];
		q[1] = s[2] * e1[0] - s[0] * e1[2];
		q[2] = s[0] * e1[1] - s[1] * e1[0];
		const float w = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) / det;
		if (w < 0.0f || u + w > 1.0f)
			continue;
		const float tt = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) / det;
		if (tt > 1.0f && tt < *best) {
			*best = tt;
			/* det is the normal (e1 x e2) against -d: positive on the side facing the ray */
			*facing = det > 0.0f || !t->culled ? 1 : -1;
		}
	}
}

static float roomBoxDistance(const float p[3], const float lo[3], const float hi[3])
{
	float distance = 0;
	for (int k = 0; k < 3; k++) {
		float d = p[k] < lo[k] ? lo[k] - p[k] : p[k] > hi[k] ? p[k] - hi[k] : 0;
		distance += d * d;
	}
	return distance;
}
static float roomTriangleDistance(const float p[3], const struct beanRoomTri *t)
{
	float ab[3], ac[3], ap[3], normal[3];
	float best = 1e30f;
	float aa = 0, bb = 0, cc = 0, da = 0, dc = 0;
	for (int k = 0; k < 3; k++) {
		ab[k] = t->p[1][k] - t->p[0][k];
		ac[k] = t->p[2][k] - t->p[0][k];
		ap[k] = p[k] - t->p[0][k];
		aa += ab[k] * ab[k];
		bb += ab[k] * ac[k];
		cc += ac[k] * ac[k];
		da += ap[k] * ab[k];
		dc += ap[k] * ac[k];
	}
	float determinant = aa * cc - bb * bb;
	if (determinant > 1e-10f) {
		float u = (cc * da - bb * dc) / determinant, v = (aa * dc - bb * da) / determinant;
		if (u >= 0 && v >= 0 && u + v <= 1) {
			best = 0;
			for (int k = 0; k < 3; k++) {
				normal[k] = ap[k] - u * ab[k] - v * ac[k];
				best += normal[k] * normal[k];
			}
			return best;
		}
	}
	for (int e = 0; e < 3; e++) {
		float d[3], length = 0, dot = 0;
		int next = (e + 1) % 3;
		for (int k = 0; k < 3; k++) {
			d[k] = t->p[next][k] - t->p[e][k];
			length += d[k] * d[k];
			dot += (p[k] - t->p[e][k]) * d[k];
		}
		float u = length > 0 ? dot / length : 0;
		if (u < 0)
			u = 0;
		if (u > 1)
			u = 1;
		float distance = 0;
		for (int k = 0; k < 3; k++) {
			float x = p[k] - t->p[e][k] - u * d[k];
			distance += x * x;
		}
		if (distance < best)
			best = distance;
	}
	return best;
}
static void roomReferenceNearest(int node, const float p[3], float *distance, int *room)
{
	const struct beanRoomTree *t = &s_tree[node];
	if (roomBoxDistance(p, t->lo, t->hi) > *distance)
		return;
	if (t->left < 0) {
		for (int i = t->begin; i < t->end; i++) {
			if (roomBoxDistance(p, s_reference[i].lo, s_reference[i].hi) > *distance)
				continue;
			float d = roomTriangleDistance(p, &s_reference[i]);
			if (d < *distance) {
				*distance = d;
				*room = s_reference[i].room;
			}
		}
	} else {
		float a = roomBoxDistance(p, s_tree[t->left].lo, s_tree[t->left].hi);
		float b = roomBoxDistance(p, s_tree[t->right].lo, s_tree[t->right].hi);
		roomReferenceNearest(a <= b ? t->left : t->right, p, distance, room);
		roomReferenceNearest(a <= b ? t->right : t->left, p, distance, room);
	}
}
