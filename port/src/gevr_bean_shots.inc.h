/* The release's level surfaces for a shot (gevrBeanWorldShotSurface).
 *
 * GoldenEye tests a shot against its own room triangles, and the hole is laid
 * on the one it hit. The release rebuilt some walls: on Streets the first
 * building past the start's car park stands off GoldenEye's facade, so a hole
 * laid on GoldenEye's wall lay behind (or in front of) the wall drawn, past
 * the decal band's reach (gfx_opengl.cpp gevr_band_decal), and no hole showed
 * (user, 2026-11-07). Dab's Mod draws the release's rooms as the rooms
 * themselves, so its shots hit what is drawn (wallhit.c lays the hole there).
 * Here the game's hit stays as it is and only where the hole is drawn moves:
 * on to the release's surface along the same shot.
 *
 * Opaque level triangles in world units (file position x the stage's scale),
 * in a grid over x and z.
 */

#define SHOT_CELL 256.0f
#define SHOT_BIG_CELLS 64

static float (*s_shotTri)[9];
static int s_shotTris;
static struct holeGrid s_shotGrid;

static void shotsFree(void)
{
	free(s_shotTri);
	s_shotTri = NULL;
	s_shotTris = 0;
	holeGridFree(&s_shotGrid);
}

static void shotsBuild(const struct gevrBeanMesh *mesh, const int *assign, float scale)
{
	int n = 0;
	shotsFree();
	for (int i = 0; i < mesh->ntris; i++)
		n += assign[i] >= 0 && mesh->tris[i].alpha == 255 && !mesh->tris[i].alphatest;
	if (!n || !(s_shotTri = malloc((size_t)n * sizeof(*s_shotTri))))
		return;
	s_shotGrid.size = SHOT_CELL;
	for (int i = 0; i < mesh->ntris; i++) {
		const struct gevrBeanTri *t = &mesh->tris[i];
		float *p;
		if (assign[i] < 0 || t->alpha != 255 || t->alphatest)
			continue;
		p = s_shotTri[s_shotTris];
		for (int k = 0; k < 9; k++)
			p[k] = t->p[k] * scale;
		if (!holeGridAdd(&s_shotGrid, s_shotTris, fminf(p[0], fminf(p[3], p[6])), fminf(p[2], fminf(p[5], p[8])),
		                 fmaxf(p[0], fmaxf(p[3], p[6])), fmaxf(p[2], fmaxf(p[5], p[8])), 1)) {
			shotsFree();
			return;
		}
		s_shotTris++;
	}
	holeGridDone(&s_shotGrid);
	GEVR_BEAN_LOG("gevr bean: %d level triangles for shots, %d cell entries, %d big\n",
	              s_shotTris, s_shotGrid.n, s_shotGrid.nbig);
}

/* Moller-Trumbore: the distance along the ray, or a negative */
static float shotTri(const float *p, const float o[3], const float d[3], float n[3])
{
	const float e1[3] = { p[3] - p[0], p[4] - p[1], p[5] - p[2] };
	const float e2[3] = { p[6] - p[0], p[7] - p[1], p[8] - p[2] };
	const float q[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
	const float det = e1[0] * q[0] + e1[1] * q[1] + e1[2] * q[2];
	if (fabsf(det) < 1e-9f)
		return -1.0f;
	const float inv = 1.0f / det;
	const float s[3] = { o[0] - p[0], o[1] - p[1], o[2] - p[2] };
	const float u = (s[0] * q[0] + s[1] * q[1] + s[2] * q[2]) * inv;
	if (u < 0.0f || u > 1.0f)
		return -1.0f;
	const float r[3] = { s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0] };
	const float v = (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) * inv;
	if (v < 0.0f || u + v > 1.0f)
		return -1.0f;
	n[0] = e1[1] * e2[2] - e1[2] * e2[1];
	n[1] = e1[2] * e2[0] - e1[0] * e2[2];
	n[2] = e1[0] * e2[1] - e1[1] * e2[0];
	return (e2[0] * r[0] + e2[1] * r[1] + e2[2] * r[2]) * inv;
}

/* The first release surface on the ray from `from` along the unit `dir`
 * between `near` and `far` (world units): where, and its face's normal turned
 * to the shooter. */
int gevrBeanWorldShotSurface(const float from[3], const float dir[3], float near, float far,
                             float hit[3], float normal[3])
{
	float best = far, bn[3] = { 0, 0, 0 };
	int found = 0;
	if (!ensureStage() || !s_shotTri || !(far > near))
		return 0;
	if (near < 0.0f)
		near = 0.0f;
	const float a[3] = { from[0] + dir[0] * near, from[1] + dir[1] * near, from[2] + dir[2] * near };
	const float b[3] = { from[0] + dir[0] * far, from[1] + dir[1] * far, from[2] + dir[2] * far };
	const int gx0 = (int)floorf(fminf(a[0], b[0]) / SHOT_CELL), gx1 = (int)floorf(fmaxf(a[0], b[0]) / SHOT_CELL);
	const int gz0 = (int)floorf(fminf(a[2], b[2]) / SHOT_CELL), gz1 = (int)floorf(fmaxf(a[2], b[2]) / SHOT_CELL);
	if ((int64_t)(gx1 - gx0 + 1) * (gz1 - gz0 + 1) > 256)
		return 0;   /* a window this long is no hole's */
	for (int pass = 0; pass < 2; pass++) {
		for (int gx = gx0; gx <= (pass ? gx0 : gx1); gx++) {
			for (int gz = gz0; gz <= (pass ? gz0 : gz1); gz++) {
				int first;
				const int end = pass ? s_shotGrid.nbig : holeGridFind(&s_shotGrid, (gx + 0.5f) * SHOT_CELL,
				                                                     (gz + 0.5f) * SHOT_CELL, &first);
				if (pass)
					first = 0;
				for (int c = first; c < end; c++) {
					float n[3];
					const int id = pass ? s_shotGrid.big[c] : s_shotGrid.cells[c].id;
					const float t = shotTri(s_shotTri[id], from, dir, n);
					if (t >= near && t < best) {
						best = t;
						bn[0] = n[0];
						bn[1] = n[1];
						bn[2] = n[2];
						found = 1;
					}
				}
			}
		}
	}
	if (!found)
		return 0;
	{
		float len = sqrtf(bn[0] * bn[0] + bn[1] * bn[1] + bn[2] * bn[2]);
		if (!(len > 0.0f))
			return 0;
		if (bn[0] * dir[0] + bn[1] * dir[1] + bn[2] * dir[2] > 0.0f)
			len = -len;
		for (int k = 0; k < 3; k++) {
			hit[k] = from[k] + dir[k] * best;
			normal[k] = bn[k] / len;
		}
	}
	return 1;
}
