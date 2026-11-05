/* Bean's doorways round GoldenEye's doors, at load (gevr_bean_world.c
 * buildStage). DAB gebeanstage.c closeDoorGaps(), fillDoorReveals() and
 * fillDoorSlots() (dabs-mod cc90f36, MIT, port/licenses/dabs-mod.txt), on the
 * port's Bean triangles in world units (cm) and GoldenEye's own room
 * triangles (gevr_bean_rooms.inc.h).
 *
 * Bean's doorways are Rare's remodel of GoldenEye's, a few units wider or
 * taller than the door GoldenEye's setup sizes to its pad's box, and its
 * walls are sheets a few units in front of the door's face with nothing
 * between: round a sliding door the slot it goes into showed past its edge,
 * and the wall it slides into was not where GoldenEye's is (user,
 * 2026-11-05: Control's lift door, "the rock wall texture showing through
 * the moving sliding door").
 *
 * - closeDoorGaps(): a vertex of Bean's mesh at a door's faces, just past a
 *   side or the top of its box, is pulled onto that edge, so the opening is
 *   the door's size again; one further past is some other wall's and stays,
 *   so does every vertex past a side the level already closes, the door's
 *   foot, and the side a sliding door goes into.
 * - fillDoorReveals(): each edge of a wall's triangle along a side or the top
 *   of a door, in front of its face, gets a strip back to the door's middle
 *   in the wall's picture (the reveal a thicker wall would have), and so does
 *   the floor's edge in front of the door's foot.
 * - fillDoorSlots(): for a door that slides into the level, where GoldenEye
 *   has wall in the band it travels through and Bean has none in front of the
 *   door's face, wall a hair in front of the face in the picture of Bean's
 *   own wall on that side.
 */

#define DOORGAP_REACH 12.0f  /* the most a vertex is pulled */
#define DOORGAP_SHARE 0.06f  /* and no more than this share of the door's side */
#define DOORGAP_DEPTH 10.0f  /* in front of or behind the door's faces */
#define DOORGAP_ON    0.05f  /* a vertex this near a door's edge is on it */
#define SLOT_CELL     12.0f  /* a cell of the band, at most this big */
#define SLOT_OUT      40.0f  /* how far in front of the face the line starts */
#define SLOT_FACE     0.25f  /* at or this near the face is in front of it */
#define SLOT_LIFT     2.0f   /* a patch stands this far in front of the face */
#define SLOT_PAINT    0.25f  /* and what was painted on the wall this far in front of that */

/* A door as GoldenEye's setup has it, in world units: its bound pad and the
 * move of a sliding one fully open (0 for one that swings or turns) */
struct beanDoorIn {
	float pos[3], up[3], look[3];
	float lo[3], hi[3];   /* the pad's box: x along up x look, y up, z look */
	float move[3];
};

struct beanDoorBox {
	float mid[3];
	float axis[3][3];  /* the pad's normal, up and look, as units */
	float half[3];
	float reach[3];
	float radius;      /* past this far from the middle, nothing is the door's */
	int thin;          /* the axis through the door */
	int8_t foot[3];    /* the side of an axis that points down, 0 for neither */
	int8_t slide[3];   /* the side a sliding door goes into the wall at, 0 for neither */
	float travel[3];   /* how far it goes along each axis, fully open */
	int first;         /* its run of the triangles near it (doorNearTris()) */
	int numnear;
	uint8_t open[3][2];/* a side with nothing of the level past it (doorSideOpen()) */
};

/* GoldenEye's own room triangles, world units (fillDoorSlots()) */
struct beanDoorGe {
	const float (*tris)[3][3];
	int num;
};

static float doorDot(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static int doorUnit(float *v)
{
	const float l = sqrtf(doorDot(v, v));
	if (!(l > 1e-6f)) return 0;
	v[0] /= l;
	v[1] /= l;
	v[2] /= l;
	return 1;
}

static float doorTriNormal(const struct gevrBeanTri *t, float *n)
{
	return holeNormal(t->p, n);
}

/* a cut-out or translucent face is no wall */
static int doorSeeThrough(const struct gevrBeanMesh *m, const struct gevrBeanTri *t)
{
	return t->tex < 0 || (t->tex < m->ntex && m->texCut[t->tex]) || t->alpha < 255 || t->alphatest;
}

static int doorBoxesFrom(const struct beanDoorIn *in, int nin, struct beanDoorBox **out)
{
	struct beanDoorBox *boxes = calloc((size_t)(nin > 0 ? nin : 1), sizeof(*boxes));
	int num = 0;
	*out = boxes;
	if (!boxes) return 0;
	for (int i = 0; i < nin; i++) {
		struct beanDoorBox *b = &boxes[num];
		const struct beanDoorIn *d = &in[i];
		memset(b, 0, sizeof(*b));
		/* the normal as GoldenEye's door7F0526EC: up x look */
		b->axis[0][0] = d->up[1] * d->look[2] - d->up[2] * d->look[1];
		b->axis[0][1] = d->up[2] * d->look[0] - d->up[0] * d->look[2];
		b->axis[0][2] = d->up[0] * d->look[1] - d->up[1] * d->look[0];
		memcpy(b->axis[1], d->up, sizeof(b->axis[1]));
		memcpy(b->axis[2], d->look, sizeof(b->axis[2]));
		if (!doorUnit(b->axis[0]) || !doorUnit(b->axis[1]) || !doorUnit(b->axis[2])) continue;
		b->thin = 0;
		for (int a = 0; a < 3; a++) {
			b->half[a] = (d->hi[a] - d->lo[a]) * 0.5f;
			b->reach[a] = fminf(DOORGAP_REACH, (d->hi[a] - d->lo[a]) * DOORGAP_SHARE);
			b->foot[a] = b->axis[a][1] < -0.7f ? 1 : b->axis[a][1] > 0.7f ? -1 : 0;
			if (b->half[a] < b->half[b->thin]) b->thin = a;
		}
		if (!(b->half[0] > 0.0f && b->half[1] > 0.0f && b->half[2] > 0.0f)) continue;
		b->radius = sqrtf(doorDot(b->half, b->half)) + DOORGAP_DEPTH + DOORGAP_REACH;
		for (int a = 0; a < 3; a++) {
			const float by = doorDot(d->move, b->axis[a]);
			b->slide[a] = by > 0.01f ? 1 : by < -0.01f ? -1 : 0;
			b->travel[a] = fabsf(by);
		}
		for (int k = 0; k < 3; k++) {
			b->mid[k] = d->pos[k];
			for (int a = 0; a < 3; a++) b->mid[k] += (d->lo[a] + d->hi[a]) * 0.5f * b->axis[a][k];
		}
		num++;
	}
	return num;
}

/* Where a point stands in a door's box: its distance along each axis from
 * the middle. Whether it is past the door's faces by more than the depth. */
static int doorBoxAt(const struct beanDoorBox *b, const float *p, float *at)
{
	const float d[3] = { p[0] - b->mid[0], p[1] - b->mid[1], p[2] - b->mid[2] };
	for (int a = 0; a < 3; a++) at[a] = doorDot(d, b->axis[a]);
	return fabsf(at[b->thin]) <= b->half[b->thin] + DOORGAP_DEPTH;
}

/* The mesh with room for more: the triangles stay where they are until it grows */
struct doorMesh {
	struct gevrBeanMesh *m;
	int cap;
};

static int doorAddTri(struct doorMesh *dm, const struct gevrBeanTri *t)
{
	if (dm->m->ntris >= dm->cap) {
		const int cap = dm->cap ? dm->cap * 2 : 1024;
		struct gevrBeanTri *grown = realloc(dm->m->tris, (size_t)cap * sizeof(*grown));
		if (!grown) return 0;
		dm->m->tris = grown;
		dm->cap = cap;
	}
	dm->m->tris[dm->m->ntris++] = *t;
	return 1;
}

/* Whether a point is on a triangle of the level already, one in the plane
 * with that normal: a strip there would fight it. The triangles near the
 * door and every strip added since the level's own (from first on). */
static int doorStripCovered(const struct gevrBeanMesh *m, const int *list, int numlist, int first,
                            const float *p, const float *normal)
{
	for (int i = 0; i < numlist + m->ntris - first; i++) {
		const struct gevrBeanTri *tri = &m->tris[i < numlist ? list[i] : first + i - numlist];
		float n[3], d[3];
		int inside = 1;
		for (int j = 0; j < 3 && inside; j++) {
			inside = p[j] >= fminf(tri->p[j], fminf(tri->p[3 + j], tri->p[6 + j])) - 0.5f
			      && p[j] <= fmaxf(tri->p[j], fmaxf(tri->p[3 + j], tri->p[6 + j])) + 0.5f;
		}
		if (!inside || doorTriNormal(tri, n) <= 0.0f || fabsf(doorDot(n, normal)) < 0.9f) continue;
		for (int j = 0; j < 3; j++) d[j] = p[j] - tri->p[j];
		if (fabsf(doorDot(d, n)) > 0.5f) continue;
		/* on the inner side of each edge, about the triangle's own normal */
		for (int k = 0; k < 3 && inside; k++) {
			const float *a = &tri->p[k * 3], *b = &tri->p[((k + 1) % 3) * 3];
			const float e[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
			const float q[3] = { p[0] - a[0], p[1] - a[1], p[2] - a[2] };
			const float x[3] = { e[1] * q[2] - e[2] * q[1], e[2] * q[0] - e[0] * q[2], e[0] * q[1] - e[1] * q[0] };
			inside = doorDot(x, n) >= 0.0f;
		}
		if (inside) return 1;
	}
	return 0;
}

static void doorSwapCorners(struct gevrBeanTri *t, int a, int b)
{
	float p[3], n[3], uv[2];
	uint8_t c[4], bone[4], weight[4];
	memcpy(p, &t->p[a * 3], sizeof(p)); memcpy(&t->p[a * 3], &t->p[b * 3], sizeof(p)); memcpy(&t->p[b * 3], p, sizeof(p));
	memcpy(n, &t->n[a * 3], sizeof(n)); memcpy(&t->n[a * 3], &t->n[b * 3], sizeof(n)); memcpy(&t->n[b * 3], n, sizeof(n));
	memcpy(uv, &t->uv[a * 2], sizeof(uv)); memcpy(&t->uv[a * 2], &t->uv[b * 2], sizeof(uv)); memcpy(&t->uv[b * 2], uv, sizeof(uv));
	memcpy(c, &t->rgba[a * 4], sizeof(c)); memcpy(&t->rgba[a * 4], &t->rgba[b * 4], sizeof(c)); memcpy(&t->rgba[b * 4], c, sizeof(c));
	memcpy(bone, &t->bone[a * 4], sizeof(bone)); memcpy(&t->bone[a * 4], &t->bone[b * 4], sizeof(bone)); memcpy(&t->bone[b * 4], bone, sizeof(bone));
	memcpy(weight, &t->weight[a * 4], sizeof(weight)); memcpy(&t->weight[a * 4], &t->weight[b * 4], sizeof(weight)); memcpy(&t->weight[b * 4], weight, sizeof(weight));
}

/* A strip face of the level: solid, drawn as the face it came from */
static void doorPlainFace(struct gevrBeanTri *t)
{
	t->flags &= (uint8_t)~(GEVR_BEAN_F_DECAL | GEVR_BEAN_F_PAINTED);
}

static int fillDoorReveals(struct doorMesh *dm, const struct beanDoorBox *boxes, int num, const int *nearby)
{
	struct gevrBeanMesh *m = dm->m;
	const int numtris = m->ntris;
	int fillers = 0;
	for (int i = 0; i < num; i++) {
		const struct beanDoorBox *b = &boxes[i];
		const int *near = &nearby[b->first];
		const int numnear = b->numnear;
		const float across = fminf(b->half[(b->thin + 1) % 3], b->half[(b->thin + 2) % 3]);
		/* A reveal is the depth of a wall a door stands in. A block as thick
		 * as it is wide (Temple's sliding stone) is no door in a wall. */
		if (b->half[b->thin] * 2.0f > across) continue;
		for (int q = 0; q < numnear; q++) {
			struct gevrBeanTri from;
			float n[3];
			if (doorTriNormal(&m->tris[near[q]], n) <= 0.0f) continue;
			from = m->tris[near[q]];   /* a copy: the strips added below can move the array */
			const int wall = fabsf(doorDot(n, b->axis[b->thin])) >= 0.9f;
			for (int k = 0; k < 3; k++) {
				const int k2 = (k + 1) % 3;
				float at0[3], at1[3];
				if (!doorBoxAt(b, &from.p[k * 3], at0) || !doorBoxAt(b, &from.p[k2 * 3], at1)) continue;
				const int face = at0[b->thin] < 0.0f ? -1 : 1;
				if (at0[b->thin] * face - b->half[b->thin] < 0.25f || at1[b->thin] * face - b->half[b->thin] < 0.25f)
					continue;
				for (int a = 0; a < 3; a++) {
					const int o = 3 - a - b->thin;
					const int side = at0[a] < 0.0f ? -1 : 1;
					struct gevrBeanTri strip[2];
					float u[2], lo, hi, end[2][3], back[2][3], mid[3], sn[3];
					if (a == b->thin || fabsf(at1[o] - at0[o]) < 0.5f) continue;
					if (wall) {
						/* along a side or the top */
						if (side == b->foot[a] || fabsf(at0[a] - side * b->half[a]) > DOORGAP_ON ||
						    fabsf(at1[a] - side * b->half[a]) > DOORGAP_ON)
							continue;
					} else {
						/* the floor's edge across the door's foot */
						if (side != b->foot[a] || fabsf(doorDot(n, b->axis[a])) < 0.9f ||
						    fabsf(at0[b->thin] - at1[b->thin]) > 0.5f ||
						    fabsf(fabsf(at0[a]) - b->half[a]) > b->reach[a] ||
						    fabsf(fabsf(at1[a]) - b->half[a]) > b->reach[a])
							continue;
					}
					/* the part of the edge along this door: the wall over a
					 * double door runs across both leaves */
					u[0] = (-b->half[o] - at0[o]) / (at1[o] - at0[o]);
					u[1] = (b->half[o] - at0[o]) / (at1[o] - at0[o]);
					if (u[0] > u[1]) {
						const float swap = u[0];
						u[0] = u[1];
						u[1] = swap;
					}
					u[0] = fmaxf(u[0], 0.0f);
					u[1] = fminf(u[1], 1.0f);
					lo = at0[o] + (at1[o] - at0[o]) * u[0];
					hi = at0[o] + (at1[o] - at0[o]) * u[1];
					if (fabsf(hi - lo) < 0.5f || u[0] >= u[1]) continue;
					for (int e = 0; e < 2; e++) {
						const float depth = at0[b->thin] + (at1[b->thin] - at0[b->thin]) * u[e];
						for (int j = 0; j < 3; j++) {
							end[e][j] = from.p[k * 3 + j] + (from.p[k2 * 3 + j] - from.p[k * 3 + j]) * u[e];
							back[e][j] = end[e][j] - depth * b->axis[b->thin][j];
						}
					}
					for (int j = 0; j < 3; j++) mid[j] = (end[0][j] + end[1][j] + back[0][j] + back[1][j]) * 0.25f;
					/* a wall's strip lies square to the side, the floor's in the floor */
					if (doorStripCovered(m, near, numnear, numtris, mid, wall ? b->axis[a] : n)) continue;
					strip[0] = strip[1] = from;
					doorPlainFace(&strip[0]);
					doorPlainFace(&strip[1]);
					/* end 0, end 1, back of 1; end 0, back of 1, back of 0 */
					memcpy(&strip[0].p[0], end[0], sizeof(end[0]));
					memcpy(&strip[0].p[3], end[1], sizeof(end[1]));
					memcpy(&strip[0].p[6], back[1], sizeof(back[1]));
					memcpy(&strip[1].p[0], end[0], sizeof(end[0]));
					memcpy(&strip[1].p[3], back[1], sizeof(back[1]));
					memcpy(&strip[1].p[6], back[0], sizeof(back[0]));
					for (int j = 0; j < 2; j++) {
						const float uv0 = from.uv[k * 2 + j] + (from.uv[k2 * 2 + j] - from.uv[k * 2 + j]) * u[0];
						const float uv1 = from.uv[k * 2 + j] + (from.uv[k2 * 2 + j] - from.uv[k * 2 + j]) * u[1];
						strip[0].uv[0 + j] = strip[1].uv[0 + j] = strip[1].uv[4 + j] = uv0;
						strip[0].uv[2 + j] = strip[0].uv[4 + j] = strip[1].uv[2 + j] = uv1;
					}
					for (int j = 0; j < 4; j++) {
						strip[0].rgba[0 + j] = strip[1].rgba[0 + j] = strip[1].rgba[8 + j] = from.rgba[k * 4 + j];
						strip[0].rgba[4 + j] = strip[0].rgba[8 + j] = strip[1].rgba[4 + j] = from.rgba[k2 * 4 + j];
					}
					for (int s = 0; s < 2; s++)
						for (int c = 0; c < 3; c++)
							memcpy(&strip[s].n[c * 3], &from.n[k * 3], sizeof(float) * 3);
					/* the side culling keeps: a wall's strip faces into the
					 * doorway, the floor's up as the floor does */
					doorTriNormal(&strip[0], sn);
					if (wall ? doorDot(sn, b->axis[a]) * side > 0.0f : doorDot(sn, n) < 0.0f) {
						doorSwapCorners(&strip[0], 1, 2);
						doorSwapCorners(&strip[1], 1, 2);
					}
					if (!doorAddTri(dm, &strip[0]) || !doorAddTri(dm, &strip[1])) return fillers;
					m = dm->m;
					fillers++;
				}
			}
		}
	}
	return fillers;
}

/* How far a triangle's UVs move for a move of delta along it: what is off
 * its plane moves nothing (a reveal pulled sideways). */
static void doorUvShift(const struct gevrBeanTri *t, const float *delta, float *duv)
{
	const float e1[3] = { t->p[3] - t->p[0], t->p[4] - t->p[1], t->p[5] - t->p[2] };
	const float e2[3] = { t->p[6] - t->p[0], t->p[7] - t->p[1], t->p[8] - t->p[2] };
	const float g11 = doorDot(e1, e1), g12 = doorDot(e1, e2), g22 = doorDot(e2, e2);
	const float det = g11 * g22 - g12 * g12;
	duv[0] = duv[1] = 0.0f;
	if (det <= 1e-6f * g11 * g22) return;
	const float r1 = doorDot(delta, e1), r2 = doorDot(delta, e2);
	const float x = (r1 * g22 - r2 * g12) / det, y = (r2 * g11 - r1 * g12) / det;
	for (int j = 0; j < 2; j++) duv[j] = x * (t->uv[2 + j] - t->uv[j]) + y * (t->uv[4 + j] - t->uv[j]);
}

/* Each door's run of the triangles whose box comes within its reach */
static int *doorNearTris(const struct gevrBeanMesh *m, struct beanDoorBox *boxes, int num)
{
	float (*tbox)[6] = malloc(sizeof(*tbox) * (size_t)(m->ntris > 0 ? m->ntris : 1));
	int *list = NULL, len = 0, cap = 0;
	if (!tbox) return NULL;
	for (int t = 0; t < m->ntris; t++) {
		const float *p = m->tris[t].p;
		for (int j = 0; j < 3; j++) {
			tbox[t][j] = fminf(p[j], fminf(p[3 + j], p[6 + j]));
			tbox[t][3 + j] = fmaxf(p[j], fmaxf(p[3 + j], p[6 + j]));
		}
	}
	for (int i = 0; i < num; i++) {
		struct beanDoorBox *b = &boxes[i];
		b->first = len;
		b->numnear = 0;
		for (int t = 0; t < m->ntris; t++) {
			float dd = 0.0f;
			/* a big floor triangle's corners can all be far off */
			for (int j = 0; j < 3; j++) {
				const float off = b->mid[j] < tbox[t][j] ? tbox[t][j] - b->mid[j]
				                : b->mid[j] > tbox[t][3 + j] ? b->mid[j] - tbox[t][3 + j] : 0.0f;
				dd += off * off;
			}
			if (dd > b->radius * b->radius) continue;
			if (len >= cap) {
				int *grown = realloc(list, sizeof(*list) * (size_t)(cap ? cap * 2 : 4096));
				if (!grown) {
					free(list);
					free(tbox);
					return NULL;
				}
				list = grown;
				cap = cap ? cap * 2 : 4096;
			}
			list[len++] = t;
			b->numnear++;
		}
	}
	free(tbox);
	if (!list) list = malloc(sizeof(int));
	return list;
}

/* Whether a line through the door's thickness, from a point just past one of
 * its sides, meets nothing of the level: the side stands open onto whatever
 * is behind. A side the level closes (Bunker's bevelled frames) stays. */
static int doorSideOpen(const struct gevrBeanMesh *m, const struct beanDoorBox *b, const int *near, int a, int side)
{
	const int o = 3 - a - b->thin;
	const float reach = b->half[b->thin] + DOORGAP_DEPTH;
	const float *d = b->axis[b->thin];
	for (int f = -1; f <= 1; f++) {
		float q[3];
		int hit = 0;
		for (int j = 0; j < 3; j++)
			q[j] = b->mid[j] + b->axis[a][j] * side * (b->half[a] + 0.5f) + b->axis[o][j] * f * b->half[o] * 0.7f;
		for (int n = 0; n < b->numnear && !hit; n++) {
			const float *p = m->tris[near[n]].p;
			float e1[3], e2[3], h[3], w[3], x[3];
			for (int j = 0; j < 3; j++) {
				e1[j] = p[3 + j] - p[j];
				e2[j] = p[6 + j] - p[j];
				w[j] = q[j] - p[j];
			}
			h[0] = d[1] * e2[2] - d[2] * e2[1];
			h[1] = d[2] * e2[0] - d[0] * e2[2];
			h[2] = d[0] * e2[1] - d[1] * e2[0];
			const float det = doorDot(e1, h);
			if (fabsf(det) < 1e-6f) continue;
			const float u = doorDot(w, h) / det;
			x[0] = w[1] * e1[2] - w[2] * e1[1];
			x[1] = w[2] * e1[0] - w[0] * e1[2];
			x[2] = w[0] * e1[1] - w[1] * e1[0];
			const float v = doorDot(d, x) / det, t = doorDot(e2, x) / det;
			hit = u >= 0.0f && v >= 0.0f && u + v <= 1.0f && fabsf(t) <= reach;
		}
		if (!hit) return 1;
	}
	return 0;
}

static int closeDoorGaps(struct doorMesh *dm, struct beanDoorBox *boxes, int num, int *reveals)
{
	struct gevrBeanMesh *m = dm->m;
	int *nearby = num > 0 ? doorNearTris(m, boxes, num) : NULL;
	uint8_t *state = nearby ? calloc((size_t)m->ntris * 3, 1) : NULL;   /* 1 in a doorway, 2 done with */
	int moved = 0;
	*reveals = 0;
	if (!state) {
		free(nearby);
		return 0;
	}
	/* in a doorway (the other leaf's), whatever it is, it stays */
	for (int i = 0; i < num; i++) {
		const struct beanDoorBox *b = &boxes[i];
		for (int q = 0; q < b->numnear; q++) {
			const int t = nearby[b->first + q];
			for (int k = 0; k < 3; k++) {
				float at[3];
				int out = 0;
				if (!doorBoxAt(b, &m->tris[t].p[k * 3], at)) continue;
				for (int a = 0; a < 3; a++) out |= a != b->thin && fabsf(at[a]) > b->half[a];
				if (!out) state[t * 3 + k] = 1;
			}
		}
	}
	for (int i = 0; i < num; i++)
		for (int a = 0; a < 3; a++)
			for (int side = 0; side < 2; side++)
				boxes[i].open[a][side] = a != boxes[i].thin && boxes[i].slide[a] != (side ? 1 : -1) &&
				                         doorSideOpen(m, &boxes[i], &nearby[boxes[i].first], a, side ? 1 : -1);
	/* the first door a vertex is near has it */
	for (int i = 0; i < num; i++) {
		const struct beanDoorBox *b = &boxes[i];
		for (int q = 0; q < b->numnear; q++) {
			const int t = nearby[b->first + q];
			for (int k = 0; k < 3; k++) {
				float *p = &m->tris[t].p[k * 3];
				float at[3], delta[3] = { 0.0f, 0.0f, 0.0f };
				int near = 1, pulled = 0;
				if (state[t * 3 + k] || !doorBoxAt(b, p, at)) continue;
				for (int a = 0; a < 3; a++)
					if (a != b->thin && fabsf(at[a]) - b->half[a] > b->reach[a]) near = 0;
				if (!near) continue;
				for (int a = 0; a < 3; a++) {
					const int side = at[a] < 0.0f ? -1 : 1;
					const float by = side * b->half[a] - at[a];
					if (a == b->thin || fabsf(at[a]) <= b->half[a] || side == b->foot[a] || !b->open[a][side > 0])
						continue;
					for (int j = 0; j < 3; j++) delta[j] += by * b->axis[a][j];
					pulled = 1;
				}
				if (pulled) {
					float duv[2];
					state[t * 3 + k] = 2;
					/* the picture stays where it was on the wall or floor */
					doorUvShift(&m->tris[t], delta, duv);
					for (int j = 0; j < 3; j++) p[j] += delta[j];
					m->tris[t].uv[k * 2] += duv[0];
					m->tris[t].uv[k * 2 + 1] += duv[1];
					moved++;
				}
			}
		}
	}
	*reveals = fillDoorReveals(dm, boxes, num, nearby);
	free(state);
	free(nearby);
	return moved;
}

/* How far along d (a unit) the line from o meets the triangle, either face, or -1 */
static float slotRayTri(const float *o, const float *d, const float *v0, const float *v1, const float *v2)
{
	const float e1[3] = { v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2] };
	const float e2[3] = { v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2] };
	const float p[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
	const float det = doorDot(e1, p);
	float tv[3], q[3];
	if (fabsf(det) < 1e-6f) return -1.0f;
	const float inv = 1.0f / det;
	tv[0] = o[0] - v0[0];
	tv[1] = o[1] - v0[1];
	tv[2] = o[2] - v0[2];
	const float u = doorDot(tv, p) * inv;
	if (u < -1e-4f || u > 1.0001f) return -1.0f;
	q[0] = tv[1] * e1[2] - tv[2] * e1[1];
	q[1] = tv[2] * e1[0] - tv[0] * e1[2];
	q[2] = tv[0] * e1[1] - tv[1] * e1[0];
	const float v = doorDot(d, q) * inv;
	if (v < -1e-4f || u + v > 1.0001f) return -1.0f;
	return doorDot(e2, q) * inv;
}

/* A point's picture on a triangle's plane, its mapping carried past its edges */
static void slotUvAt(const struct gevrBeanTri *t, const float *p, float *uv)
{
	const float delta[3] = { p[0] - t->p[0], p[1] - t->p[1], p[2] - t->p[2] };
	float duv[2];
	doorUvShift(t, delta, duv);
	uv[0] = t->uv[0] + duv[0];
	uv[1] = t->uv[1] + duv[1];
}

/* A rectangle of wall a hair in front of a door's face, u0 to u1 across it
 * and a0 to a1 up its rise, in the donor's picture, facing out along -d */
static int slotPatch(struct doorMesh *dm, const struct beanDoorBox *b, int t, int w, int a, int face,
                     const struct gevrBeanTri *donor, const float *d, float u0, float u1, float a0, float a1)
{
	struct gevrBeanTri patch = *donor;
	float corner[4][3], nrm[3];
	uint8_t rgba[4];
	int added = 0;
	for (int k = 0; k < 4; k++) {
		const float cu = k == 1 || k == 2 ? u1 : u0;
		const float ca = k >= 2 ? a1 : a0;
		for (int j = 0; j < 3; j++)
			corner[k][j] = b->mid[j] + face * (b->half[t] + SLOT_LIFT) * b->axis[t][j] + cu * b->axis[w][j] +
			               b->slide[a] * ca * b->axis[a][j];
	}
	for (int ch = 0; ch < 3; ch++) {
		unsigned sum = 0;
		for (int k = 0; k < 3; k++) sum += donor->rgba[k * 4 + ch];
		rgba[ch] = (uint8_t)(sum / 3);
	}
	rgba[3] = donor->rgba[3];
	doorPlainFace(&patch);
	for (int half = 0; half < 2; half++) {
		const int idx[3] = { 0, half ? 2 : 1, half ? 3 : 2 };
		for (int k = 0; k < 3; k++) {
			memcpy(&patch.p[k * 3], corner[idx[k]], sizeof(corner[0]));
			slotUvAt(donor, &patch.p[k * 3], &patch.uv[k * 2]);
			memcpy(&patch.rgba[k * 4], rgba, sizeof(rgba));
			memcpy(&patch.n[k * 3], &donor->n[0], sizeof(float) * 3);
		}
		/* facing the way the donor faces, out of the door */
		if (doorTriNormal(&patch, nrm) > 0.0f && doorDot(nrm, d) > 0.0f) doorSwapCorners(&patch, 1, 2);
		added += doorAddTri(dm, &patch);
	}
	return added;
}

/* Bean's wall on one side of a door: the nearest triangle facing out that
 * way, beside or over the doorway, in front of the door's middle */
static int slotDonor(const struct gevrBeanMesh *m, const struct beanDoorBox *b, const int *near, int numnear,
                     int t, int w, int a, int face, const float *d, float extent)
{
	int donor = -1;
	float donordist = 1e30f;
	for (int q = 0; q < numnear; q++) {
		const struct gevrBeanTri *tri = &m->tris[near[q]];
		float nrm[3], mid[3], at[3];
		/* a cut-out is no wall: Depot's "C5" stencil over a warehouse door */
		if (doorSeeThrough(m, tri) || doorTriNormal(tri, nrm) <= 0.0f || doorDot(nrm, d) > -0.9f) continue;
		for (int j = 0; j < 3; j++) mid[j] = (tri->p[j] + tri->p[3 + j] + tri->p[6 + j]) / 3.0f - b->mid[j];
		for (int k = 0; k < 3; k++) at[k] = doorDot(mid, b->axis[k]);
		if (at[t] * face < 0.0f || at[t] * face > extent || at[a] * b->slide[a] < b->half[a] * 0.5f) continue;
		const float dw = fmaxf(0.0f, fabsf(at[w]) - b->half[w]);
		const float da = fabsf(at[a] * b->slide[a] - (b->half[a] + b->travel[a] * 0.5f));
		const float dist = dw * dw + da * da;
		if (dist < donordist) {
			donordist = dist;
			donor = near[q];
		}
	}
	return donor;
}

static int fillDoorSlots(struct doorMesh *dm, const struct beanDoorBox *boxes, int num, const struct beanDoorGe *ge,
                         int *doorsOut)
{
	int patched = 0, doors = 0;
	*doorsOut = 0;
	if (num <= 0 || !ge || ge->num <= 0) return 0;
	for (int i = 0; i < num; i++) {
		const struct beanDoorBox *b = &boxes[i];
		const int t = b->thin, before = patched;
		const int numtris = dm->m->ntris;
		int a = -1, numnear = 0, *near = NULL, *gelist = NULL, numge = 0;
		float lo[3], hi[3];
		for (int k = 0; k < 3; k++)
			if (k != t && b->slide[k] && b->travel[k] > 1.0f) a = k;
		if (a < 0) continue;
		const int w = 3 - t - a;
		const float extent = b->half[t] + SLOT_OUT;
		/* the band's box, both faces' lines and all */
		for (int j = 0; j < 3; j++) {
			float l = 1e30f, h = -1e30f;
			for (int corner = 0; corner < 8; corner++) {
				float ext[3], v = b->mid[j];
				ext[t] = (corner & 1 ? 1 : -1) * extent;
				ext[w] = (corner & 2 ? 1 : -1) * b->half[w];
				ext[a] = b->slide[a] * (corner & 4 ? b->half[a] + b->travel[a] : b->half[a]);
				for (int k = 0; k < 3; k++) v += ext[k] * b->axis[k][j];
				l = fminf(l, v);
				h = fmaxf(h, v);
			}
			lo[j] = l - 1.0f;
			hi[j] = h + 1.0f;
		}
		gelist = malloc(sizeof(*gelist) * (size_t)ge->num);
		for (int q = 0; gelist && q < ge->num; q++) {
			int in = 1;
			for (int j = 0; j < 3 && in; j++)
				in = fmaxf(ge->tris[q][0][j], fmaxf(ge->tris[q][1][j], ge->tris[q][2][j])) >= lo[j] &&
				     fminf(ge->tris[q][0][j], fminf(ge->tris[q][1][j], ge->tris[q][2][j])) <= hi[j];
			if (in) gelist[numge++] = q;
		}
		near = numge ? malloc(sizeof(*near) * (size_t)(numtris > 0 ? numtris : 1)) : NULL;
		for (int q = 0; near && q < numtris; q++) {
			const struct gevrBeanTri *tri = &dm->m->tris[q];
			int in = !(tri->flags & GEVR_BEAN_F_DECAL) && tri->alpha == 255;
			for (int j = 0; j < 3 && in; j++)
				in = fmaxf(tri->p[j], fmaxf(tri->p[3 + j], tri->p[6 + j])) >= lo[j] - SLOT_OUT &&
				     fminf(tri->p[j], fminf(tri->p[3 + j], tri->p[6 + j])) <= hi[j] + SLOT_OUT;
			if (in) near[numnear++] = q;
		}
		const int nw = (int)fmaxf(1.0f, ceilf(b->half[w] * 2.0f / SLOT_CELL));
		const int na = (int)fmaxf(1.0f, ceilf(b->travel[a] / SLOT_CELL));
		const float cw = b->half[w] * 2.0f / nw, ca = b->travel[a] / na;
		for (int face = -1; near && face <= 1; face += 2) {
			const int facebefore = patched;
			float d[3];
			for (int j = 0; j < 3; j++) d[j] = -face * b->axis[t][j];
			const int found = slotDonor(dm->m, b, near, numnear, t, w, a, face, d, extent);
			if (found < 0) continue;
			const struct gevrBeanTri donor = dm->m->tris[found];   /* a copy: patches can move the array */
			uint8_t *need = calloc((size_t)nw * na, 1);
			uint8_t *hdfaces = calloc((size_t)(numnear > 0 ? numnear : 1), 1);
			if (!need || !hdfaces) {
				free(need);
				free(hdfaces);
				continue;
			}
			for (int q = 0; q < numnear; q++) {
				float nrm[3];
				const struct gevrBeanTri *tri = &dm->m->tris[near[q]];
				hdfaces[q] = !doorSeeThrough(dm->m, tri) && doorTriNormal(tri, nrm) > 0.0f && doorDot(nrm, d) < 0.0f;
			}
			for (int iw = 0; iw < nw; iw++) {
				for (int ia = 0; ia < na; ia++) {
					const float u0 = -b->half[w] + (iw + 0.5f) * cw;
					const float a0 = b->half[a] + (ia + 0.5f) * ca;
					float besthd = 1e30f, bestge = 1e30f, o[3];
					for (int j = 0; j < 3; j++)
						o[j] = b->mid[j] + face * extent * b->axis[t][j] + u0 * b->axis[w][j] + b->slide[a] * a0 * b->axis[a][j];
					for (int q = 0; q < numge; q++) {
						const float (*g)[3] = ge->tris[gelist[q]];
						const float h = slotRayTri(o, d, g[0], g[1], g[2]);
						if (h >= 0.0f && h < bestge) bestge = h;
					}
					/* GoldenEye has wall here, in the door's thickness or in front of it */
					if (bestge > SLOT_OUT + b->half[t] * 2.0f + SLOT_FACE) continue;
					/* Bean's faces this way: one turned away is culled */
					for (int q = 0; q < numnear; q++) {
						if (!hdfaces[q]) continue;
						const float *p = dm->m->tris[near[q]].p;
						const float h = slotRayTri(o, d, p, p + 3, p + 6);
						if (h >= 0.0f && h < besthd) besthd = h;
					}
					/* and Bean's is not in front of the door's face */
					need[iw * na + ia] = besthd <= SLOT_OUT + SLOT_FACE ? 3 : 1;
				}
			}
			/* a cell Bean covers beside one it does not is covered only as far
			 * as its middle: the wall goes on under the cover */
			for (int iw = 0; iw < nw; iw++)
				for (int ia = 0; ia < na; ia++)
					if (need[iw * na + ia] == 3 &&
					    ((iw > 0 && need[(iw - 1) * na + ia] == 1) || (iw + 1 < nw && need[(iw + 1) * na + ia] == 1) ||
					     (ia > 0 && need[iw * na + ia - 1] == 1) || (ia + 1 < na && need[iw * na + ia + 1] == 1)))
						need[iw * na + ia] = 4;
			for (int q = 0; q < nw * na; q++)
				if (need[q] == 4) need[q] = 1;
			/* the cells as few rectangles: a column's run, carried over the
			 * columns after it that need the same run */
			for (int iw = 0; iw < nw; iw++) {
				for (int ia = 0; ia < na; ia++) {
					int top = ia, right = iw;
					if (need[iw * na + ia] != 1) continue;
					while (top + 1 < na && need[iw * na + top + 1] == 1) top++;
					while (right + 1 < nw) {
						int same = 1;
						for (int k = ia; k <= top && same; k++) same = need[(right + 1) * na + k] == 1;
						if (!same) break;
						right++;
					}
					for (int x = iw; x <= right; x++)
						for (int k = ia; k <= top; k++) need[x * na + k] = 2;
					patched += slotPatch(dm, b, t, w, a, face, &donor, d, -b->half[w] + iw * cw,
					                     -b->half[w] + (right + 1) * cw, b->half[a] + ia * ca, b->half[a] + (top + 1) * ca);
				}
			}
			/* what Bean painted on its wall inside the band comes forward with it */
			if (patched > facebefore) {
				for (int q = 0; q < numnear; q++) {
					struct gevrBeanTri copy = dm->m->tris[near[q]];
					float nrm[3], l[3] = { 1e30f, 1e30f, 1e30f }, h[3] = { -1e30f, -1e30f, -1e30f };
					if (copy.tex == donor.tex || doorTriNormal(&copy, nrm) <= 0.0f || doorDot(nrm, d) > -0.9f) continue;
					for (int k = 0; k < 3; k++) {
						float rel[3];
						for (int j = 0; j < 3; j++) rel[j] = copy.p[k * 3 + j] - b->mid[j];
						for (int x = 0; x < 3; x++) {
							const float at = doorDot(rel, b->axis[x]) * (x == t ? face : x == a ? b->slide[a] : 1);
							l[x] = fminf(l[x], at);
							h[x] = fmaxf(h[x], at);
						}
					}
					if (l[w] < -b->half[w] - SLOT_FACE || h[w] > b->half[w] + SLOT_FACE ||
					    l[a] < b->half[a] - SLOT_FACE || h[a] > b->half[a] + b->travel[a] + SLOT_FACE ||
					    l[t] < -b->half[t] || h[t] > b->half[t] + SLOT_LIFT)
						continue;
					const float by = b->half[t] + SLOT_LIFT + SLOT_PAINT - (l[t] + h[t]) * 0.5f;
					for (int k = 0; k < 3; k++)
						for (int j = 0; j < 3; j++) copy.p[k * 3 + j] += face * by * b->axis[t][j];
					patched += doorAddTri(dm, &copy);
				}
			}
			free(need);
			free(hdfaces);
		}
		if (patched > before) doors++;
		free(near);
		free(gelist);
	}
	*doorsOut = doors;
	return patched;
}

struct beanDoorResult {
	int doors, moved, reveals, slotTris, slotDoors;
};

/* All three on a level mesh in file units; cm its scale (centimetres a unit) */
static struct beanDoorResult beanFitDoorways(struct gevrBeanMesh *mesh, float cm, const struct beanDoorIn *in, int nin,
                                             const struct beanDoorGe *ge)
{
	struct beanDoorResult r = { 0, 0, 0, 0, 0 };
	struct beanDoorBox *boxes = NULL;
	if (!mesh || mesh->ntris <= 0 || !(cm > 0.0f) || nin <= 0) return r;
	r.doors = doorBoxesFrom(in, nin, &boxes);
	if (r.doors > 0) {
		struct doorMesh dm = { mesh, mesh->ntris };
		for (int i = 0; i < mesh->ntris; i++)
			for (int k = 0; k < 9; k++) mesh->tris[i].p[k] *= cm;
		r.moved = closeDoorGaps(&dm, boxes, r.doors, &r.reveals);
		r.slotTris = fillDoorSlots(&dm, boxes, r.doors, ge, &r.slotDoors);
		for (int i = 0; i < mesh->ntris; i++)
			for (int k = 0; k < 9; k++) mesh->tris[i].p[k] /= cm;
	}
	free(boxes);
	return r;
}
