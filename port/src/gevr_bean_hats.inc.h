/*
 * A GoldenEye hat on a head drawn as Bean's HD mesh: DAB xblamesh.c
 * xblaMeshHatSeat() and xblaMeshHatSeatMeasure() (port/licenses/dabs-mod.txt).
 *
 * GoldenEye fits a hat to each of its random heads by a table
 * (headHat_array_8003E464): an offset and a scale made for the N64 heads. The
 * release's heads are 4J's, other shapes that mostly wear a hat where the
 * hat's own model puts it: the table's lift for Karl, Duncan, Dwayne and B
 * floated a side cap over their HD hair, tipped back, and hair stood through
 * others. So on an HD head the table is not applied. In the frame of the joint
 * the hat hangs on, both ways round: from each point of the hat's outside
 * along its normal, how far the head's surface stands beyond it; and from each
 * point of the head above the hat's rim, down and in towards its middle, how
 * far it is to the outside it comes out of. Hair through the sides only a
 * bigger hat covers (sized about the middle of its rim); hair through the
 * crown a lift clears. The smallest size with the sides clear and the crown
 * within 4 units, up to maxscale (1.25 a side cap, 1.12 a helmet or a peaked
 * cap); then the lift, 2 clear, at most maxlift (14, a side cap 2). Each the
 * third worst, so one stray strand is not the answer. A hat nothing stands
 * through is set down until it meets the head, at most 20. A beret's and a
 * fur hat's flaps read as hair, so those are worn as modelled (maxscale 0).
 *
 * Handed back as an offset, a size and the point it is sized about, in the
 * hat's own space, for chr.c to apply where it applied the table's. Measured
 * once for each head, hat and hat type (the hat hangs rigidly on the head
 * joint), from this frame's matrices. 0 when the head or the hat is not drawn
 * from Bean, and GoldenEye's table applies.
 */
#define HAT_UP       0.3f
#define HAT_SIDE    -0.2f
#define HAT_SIDEDEEP 12.0f
#define HAT_INWARD  -0.3f
#define HAT_CLEAR    2.0f
#define HAT_DEEPEST  40.0f
#define HAT_STEPMAX  1.3f
#define HAT_STEPS    30
#define HAT_GAIN     0.5f
#define HAT_LIFTOK   4.0f
#define HAT_RIMNEAR  16.0f
#define HAT_LOWEST   20.0f
#define HAT_MAXPTS   8192
#define HAT_CACHE    64

static struct {
	const void *head, *hat;
	int type, ok;
	float seat[7];
} s_hatSeats[HAT_CACHE];
static int s_hatSeatNext;

static void hatXform(const float m[4][4], const float in[3], float out[3])
{
	for (int k = 0; k < 3; k++)
		out[k] = in[0] * m[0][k] + in[1] * m[1][k] + in[2] * m[2][k] + m[3][k];
}

/* an affine matrix's inverse (row vectors, translation in row 3) */
static int hatInvert(const float m[4][4], float out[4][4])
{
	float a[9], inv[9];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			a[i * 3 + j] = m[i][j];
	const float det = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) +
	                  a[2] * (a[3] * a[7] - a[4] * a[6]);
	if (fabsf(det) < 1e-12f)
		return 0;
	inv[0] = (a[4] * a[8] - a[5] * a[7]) / det;
	inv[1] = (a[2] * a[7] - a[1] * a[8]) / det;
	inv[2] = (a[1] * a[5] - a[2] * a[4]) / det;
	inv[3] = (a[5] * a[6] - a[3] * a[8]) / det;
	inv[4] = (a[0] * a[8] - a[2] * a[6]) / det;
	inv[5] = (a[2] * a[3] - a[0] * a[5]) / det;
	inv[6] = (a[3] * a[7] - a[4] * a[6]) / det;
	inv[7] = (a[1] * a[6] - a[0] * a[7]) / det;
	inv[8] = (a[0] * a[4] - a[1] * a[3]) / det;
	memset(out, 0, 16 * sizeof(float));
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			out[i][j] = inv[i * 3 + j];
	for (int j = 0; j < 3; j++)
		out[3][j] = -(m[3][0] * out[0][j] + m[3][1] * out[1][j] + m[3][2] * out[2][j]);
	out[3][3] = 1.0f;
	return 1;
}

/* where a ray meets a triangle (0 a miss); det < 0 the ray leaves through it */
static float hatTriHit(const float *p0, const float *p1, const float *p2, const float o[3], const float d[3], float *det)
{
	const float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
	const float e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
	const float h[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
	const float sv[3] = { o[0] - p0[0], o[1] - p0[1], o[2] - p0[2] };
	float qv[3];
	*det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
	if (*det > -1e-6f && *det < 1e-6f)
		return 0.0f;
	const float u = (sv[0] * h[0] + sv[1] * h[1] + sv[2] * h[2]) / *det;
	if (u < 0.0f || u > 1.0f)
		return 0.0f;
	qv[0] = sv[1] * e1[2] - sv[2] * e1[1];
	qv[1] = sv[2] * e1[0] - sv[0] * e1[2];
	qv[2] = sv[0] * e1[1] - sv[1] * e1[0];
	const float v = (d[0] * qv[0] + d[1] * qv[1] + d[2] * qv[2]) / *det;
	if (v < 0.0f || u + v > 1.0f)
		return 0.0f;
	const float t = (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]) / *det;
	return t > 0.0f ? t : 0.0f;
}

/* how far the head's surface stands beyond a point of the hat's outside along
 * its normal: the triangles the ray leaves through, up to the first it enters
 * (DAB xblaMeshHatSeatRay()); head triangles are consecutive corners */
static float hatRay(const float *head, int ntris, const float o[3], const float n[3])
{
	float exitt = 0.0f, entert = HAT_DEEPEST, rlo[3], rhi[3];
	for (int a = 0; a < 3; a++) {
		const float end = o[a] + n[a] * HAT_DEEPEST;
		rlo[a] = o[a] < end ? o[a] : end;
		rhi[a] = o[a] > end ? o[a] : end;
	}
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < ntris; i++) {
			const float *p0 = &head[i * 9], *p1 = p0 + 3, *p2 = p0 + 6;
			int apart = 0;
			float det;
			for (int a = 0; a < 3 && !apart; a++)
				apart = (p0[a] < rlo[a] && p1[a] < rlo[a] && p2[a] < rlo[a]) ||
				        (p0[a] > rhi[a] && p1[a] > rhi[a] && p2[a] > rhi[a]);
			if (apart)
				continue;
			const float t = hatTriHit(p0, p1, p2, o, n, &det);
			if (t <= 0.0f || (pass == 0) != (det > 0.0f))
				continue;
			if (pass == 0)
				entert = t < entert ? t : entert;
			else if (t < entert && t > exitt)
				exitt = t;
		}
	return exitt;
}

/* the nearest of the hat's outside triangles a ray enters from in front, short
 * of maxt (DAB xblaMeshHatSeatHit()) */
static float hatHit(const float *tris, int ntris, const float o[3], const float d[3], float maxt, int anyface)
{
	float best = 0.0f, rlo[3], rhi[3];
	for (int a = 0; a < 3; a++) {
		const float end = o[a] + d[a] * maxt;
		rlo[a] = o[a] < end ? o[a] : end;
		rhi[a] = o[a] > end ? o[a] : end;
	}
	for (int i = 0; i < ntris; i++) {
		const float *p = &tris[i * 9];
		int apart = 0;
		float det;
		for (int a = 0; a < 3 && !apart; a++)
			apart = (p[a] < rlo[a] && p[3 + a] < rlo[a] && p[6 + a] < rlo[a]) ||
			        (p[a] > rhi[a] && p[3 + a] > rhi[a] && p[6 + a] > rhi[a]);
		if (apart)
			continue;
		const float t = hatTriHit(p, p + 3, p + 6, o, d, &det);
		if (t > 0.0f && (anyface || det > 0.0f) && t < maxt && (best == 0.0f || t < best))
			best = t;
	}
	return best;
}

static void hatTop3(float worst[3], float d)
{
	for (int r = 0; r < 3; r++)
		if (d > worst[r]) {
			for (int m = 2; m > r; m--)
				worst[m] = worst[m - 1];
			worst[r] = d;
			break;
		}
}

/* with the hat sized about anchor, hair through its sides and the lift that
 * clears its crown, each the third worst (DAB xblaMeshHatSeatNeed()) */
static void hatNeed(const float *pts, int npts, const float anchor[3], float scale, float *side, float *need)
{
	float sides[3] = { 0.0f, 0.0f, 0.0f }, lifts[3] = { 0.0f, 0.0f, 0.0f };
	for (int i = 0; i < npts; i++) {
		const float *pt = &pts[i * 7];
		if (pt[6] <= 0.0f)
			continue;
		const float r = pt[6] - (scale - 1.0f) * ((pt[0] - anchor[0]) * pt[3] + (pt[1] - anchor[1]) * pt[4] +
		                                          (pt[2] - anchor[2]) * pt[5]);
		if (r <= 0.0f)
			continue;
		if (pt[4] >= HAT_UP)
			hatTop3(lifts, r / pt[4]);
		else if (pt[6] <= HAT_SIDEDEEP)
			hatTop3(sides, r);
	}
	*side = sides[2];
	*need = lifts[2];
}

/* a growing float array */
static int hatPush(float **arr, int *n, int *cap, const float *v, int count)
{
	if (*n + count > *cap) {
		const int grown = (*cap ? *cap * 2 : 4096) + count;
		float *p = realloc(*arr, (size_t)grown * sizeof(float));
		if (!p)
			return 0;
		*arr = p;
		*cap = grown;
	}
	memcpy(*arr + *n, v, (size_t)count * sizeof(float));
	*n += count;
	return 1;
}

static int hatMeasure(Model *chr, const ModelFileHeader *headfile, Model *hatmodel, const float joint[4][4],
                      float maxscale, float maxlift, float out[7])
{
	float invj[4][4], hatm[4][4];
	float *head = NULL, *atris = NULL, *htris = NULL, *pts = NULL;
	int nhead = 0, chead = 0, natris = 0, catris = 0, nhtris = 0, chtris = 0, npts = 0;
	float alo[3] = { 1e9f, 1e9f, 1e9f }, ahi[3] = { -1e9f, -1e9f, -1e9f }, mid[2], anchor[3];
	int ok = 0;
	if (!hatInvert(joint, invj))
		return 0;
	memcpy(hatm, hatmodel->render_pos[0].pos.m, sizeof(hatm));
	/* the head as Bean draws it: its skin's corners on the anchor (gevr_bean_pose.inc.h) */
	for (int i = 0; i < s_nswap; i++) {
		const struct beanSwap *s = &s_swap[i];
		if (s->owner != headfile || !s->skin || activeSwap(s->node) != s)
			continue;
		const int anchorIndex = s->skin->anchor >= 0 ? s->skin->anchor : nodeMatrix(s->node);
		if (anchorIndex < 0 || anchorIndex >= chr->obj->numMatrices)
			continue;
		const float (*am)[4] = chr->render_pos[anchorIndex].pos.m;
		for (int v = 0; v + 2 < s->skin->nverts; v += 3)
			for (int c = 0; c < 3; c++) {
				float cam[3], local[3];
				hatXform(am, s->skin->verts[v + c].p[0], cam);
				hatXform(invj, cam, local);
				if (!hatPush(&head, &nhead, &chead, local, 3))
					goto done;
			}
	}
	/* the hat as Bean draws it: its lists' triangles under their matrices */
	for (int i = 0; i < s_nswap; i++) {
		const struct beanSwap *s = &s_swap[i];
		if (s->owner != hatmodel->obj || s->skin || s->ours == s_blank || !s->emitted || !s->emittedVtx ||
		    activeSwap(s->node) != s)
			continue;
		const int m = nodeMatrix(s->node);
		if (m < 0 || m >= hatmodel->obj->numMatrices)
			continue;
		const float (*mm)[4] = hatmodel->render_pos[m].pos.m;
		for (int v = 0; v + 2 < s->emittedVerts; v += 3) {
			float q[9];
			for (int c = 0; c < 3; c++) {
				float p[3], cam[3];
				if (s->emittedWorld) {
					memcpy(p, ((const GevrWorldVertex *)s->emittedVtx)[v + c].p, sizeof(p));
				} else {
					const Vtx *vt = &((const Vtx *)s->emittedVtx)[v + c];
					p[0] = vt->v.ob[0];
					p[1] = vt->v.ob[1];
					p[2] = vt->v.ob[2];
				}
				hatXform(mm, p, cam);
				hatXform(invj, cam, &q[c * 3]);
			}
			if (!hatPush(&atris, &natris, &catris, q, 9))
				goto done;
		}
	}
	nhead /= 9;
	natris /= 9;
	if (!nhead || !natris)
		goto done;
	out[0] = out[1] = out[2] = 0.0f;
	out[3] = 1.0f;
	out[4] = out[5] = out[6] = 0.0f;
	if (maxscale <= 0.0f) {
		ok = 1;   /* worn as modelled */
		goto done;
	}
	/* the hat's middle across, and the head's triangles within reach of it */
	{
		float mlo[2] = { 1e9f, 1e9f }, mhi[2] = { -1e9f, -1e9f }, blo[3] = { 1e9f, 1e9f, 1e9f }, bhi[3] = { -1e9f, -1e9f, -1e9f };
		for (int i = 0; i < natris * 3; i++) {
			const float *q = &atris[i * 3];
			mlo[0] = q[0] < mlo[0] ? q[0] : mlo[0];
			mhi[0] = q[0] > mhi[0] ? q[0] : mhi[0];
			mlo[1] = q[2] < mlo[1] ? q[2] : mlo[1];
			mhi[1] = q[2] > mhi[1] ? q[2] : mhi[1];
			for (int a = 0; a < 3; a++) {
				blo[a] = q[a] < blo[a] ? q[a] : blo[a];
				bhi[a] = q[a] > bhi[a] ? q[a] : bhi[a];
			}
		}
		mid[0] = (mlo[0] + mhi[0]) * 0.5f;
		mid[1] = (mlo[1] + mhi[1]) * 0.5f;
		for (int a = 0; a < 3; a++) {
			const float grow = HAT_DEEPEST + (bhi[a] - blo[a]) * (HAT_STEPMAX - 1.0f);
			blo[a] -= grow;
			bhi[a] += grow;
		}
		int kept = 0;
		for (int i = 0; i < nhead; i++) {
			int near = 1;
			for (int a = 0; a < 3 && near; a++) {
				const float v0 = head[i * 9 + a], v1 = head[i * 9 + 3 + a], v2 = head[i * 9 + 6 + a];
				near = !((v0 < blo[a] && v1 < blo[a] && v2 < blo[a]) || (v0 > bhi[a] && v1 > bhi[a] && v2 > bhi[a]));
			}
			if (near) {
				memmove(&head[kept * 9], &head[i * 9], 9 * sizeof(float));
				kept++;
			}
		}
		nhead = kept;
	}
	/* the hat's outside: triangles facing up in the joint's frame, or out from
	 * its middle and not down (a band's inward lining is not); each corner with
	 * the outside's normal there, and how far the head stands beyond it */
	pts = malloc((size_t)HAT_MAXPTS * 7 * sizeof(float));
	if (!pts)
		goto done;
	for (int i = 0; i < natris; i++) {
		const float *q = &atris[i * 9];
		float e0[3], e1[3], n[3];
		for (int a = 0; a < 3; a++) {
			e0[a] = q[3 + a] - q[a];
			e1[a] = q[6 + a] - q[a];
		}
		n[0] = e0[1] * e1[2] - e0[2] * e1[1];
		n[1] = e0[2] * e1[0] - e0[0] * e1[2];
		n[2] = e0[0] * e1[1] - e0[1] * e1[0];
		const float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		if (len < 1e-6f)
			continue;
		for (int a = 0; a < 3; a++)
			n[a] /= len;
		const float cx = (q[0] + q[3] + q[6]) / 3.0f - mid[0], cz = (q[2] + q[5] + q[8]) / 3.0f - mid[1];
		const float cl = sqrtf(cx * cx + cz * cz);
		const float outward = cl > 1e-3f ? (n[0] * cx + n[2] * cz) / cl : 0.0f;
		if (n[1] < HAT_UP ? n[1] < HAT_SIDE || outward <= 0.0f : outward < HAT_INWARD)
			continue;
		if (!hatPush(&htris, &nhtris, &chtris, q, 9))
			goto done;
		/* each corner along its triangle's normal (Bean's corners are not
		 * shared between triangles, so not averaged as DAB's are) */
		for (int c = 0; c < 3 && npts < HAT_MAXPTS; c++) {
			float *pt = &pts[npts * 7];
			for (int a = 0; a < 3; a++) {
				pt[a] = q[c * 3 + a];
				pt[3 + a] = n[a];
			}
			alo[0] = pt[0] < alo[0] ? pt[0] : alo[0];
			ahi[0] = pt[0] > ahi[0] ? pt[0] : ahi[0];
			alo[1] = pt[1] < alo[1] ? pt[1] : alo[1];
			alo[2] = pt[2] < alo[2] ? pt[2] : alo[2];
			ahi[2] = pt[2] > ahi[2] ? pt[2] : ahi[2];
			pt[6] = hatRay(head, nhead, pt, &pt[3]);
			npts++;
		}
	}
	nhtris /= 9;
	/* the head's own points out through the hat's outside, straight down onto
	 * it or in towards its middle, above the hat's rim there */
	for (int i = 0; i < nhead * 3 && nhtris > 0; i++) {
		const float *o = &head[i * 3];
		float dirs[2][3] = { { 0.0f, -1.0f, 0.0f }, { mid[0] - o[0], 0.0f, mid[1] - o[2] } };
		const float across = sqrtf(dirs[1][0] * dirs[1][0] + dirs[1][2] * dirs[1][2]);
		float hit[2] = { 0.0f, 0.0f }, rim = 1e9f;
		if (o[0] < alo[0] || o[0] > ahi[0] || o[2] < alo[2] || o[2] > ahi[2] || o[1] <= alo[1])
			continue;
		hit[0] = hatHit(htris, nhtris, o, dirs[0], HAT_DEEPEST, 0);
		if (across >= 1e-3f) {
			dirs[1][0] /= across;
			dirs[1][2] /= across;
			hit[1] = hatHit(htris, nhtris, o, dirs[1], across < HAT_DEEPEST ? across : HAT_DEEPEST, 0);
		}
		if (hit[0] <= 0.0f && hit[1] <= 0.0f)
			continue;
		for (int j = 0; j < natris * 3; j++) {
			const float *c = &atris[j * 3];
			const float dx = c[0] - o[0], dz = c[2] - o[2];
			if (dx * dx + dz * dz < HAT_RIMNEAR * HAT_RIMNEAR && c[1] < rim)
				rim = c[1];
		}
		if (o[1] <= rim)
			continue;
		for (int k = 0; k < 2 && npts < HAT_MAXPTS; k++)
			if (hit[k] > 0.0f) {
				float *pt = &pts[npts * 7];
				for (int a = 0; a < 3; a++) {
					pt[a] = o[a] + dirs[k][a] * hit[k];
					pt[3 + a] = -dirs[k][a];
				}
				pt[6] = hit[k];
				npts++;
			}
	}
	/* how far the hat could come down before it meets the head */
	float drop = 0.0f;
	{
		float nearest[3] = { -HAT_DEEPEST, -HAT_DEEPEST, -HAT_DEEPEST };
		const float up[3] = { 0.0f, 1.0f, 0.0f };
		int hits = 0;
		for (int i = 0; i < nhead * 3; i++) {
			const float *o = &head[i * 3];
			if (o[0] < alo[0] || o[0] > ahi[0] || o[2] < alo[2] || o[2] > ahi[2])
				continue;
			const float t = hatHit(atris, natris, o, up, HAT_DEEPEST, 1);
			if (t > 0.0f) {
				hatTop3(nearest, -t);
				hits++;
			}
		}
		drop = hits >= 3 ? -nearest[2] : 0.0f;
	}
	if (npts < 3)
		goto done;
	anchor[0] = (alo[0] + ahi[0]) * 0.5f;
	anchor[1] = alo[1];
	anchor[2] = (alo[2] + ahi[2]) * 0.5f;
	float scale = 1.0f, lift = 0.0f, first = 0.0f;
	{
		float bestcost = 1e9f;
		for (int step = 0; step <= HAT_STEPS; step++) {
			const float s = 1.0f + (HAT_STEPMAX - 1.0f) * (float)step / (float)HAT_STEPS;
			float side, need;
			if (s > maxscale + 0.001f)
				break;
			hatNeed(pts, npts, anchor, s, &side, &need);
			if (step == 0)
				first = need > side ? need : side;
			if (side <= 0.0f && need <= HAT_LIFTOK) {
				scale = s;
				lift = need;
				break;
			}
			const float cost = (side > 0.0f ? side : 0.0f) + (need > 0.0f ? need : 0.0f);
			if (cost < bestcost - HAT_GAIN) {
				bestcost = cost;
				scale = s;
				lift = need;
			}
		}
	}
	const float hair = lift;
	lift = lift > 0.0f ? lift + HAT_CLEAR : 0.0f;
	lift = lift > maxlift ? maxlift : lift;
	if (scale == 1.0f && lift == 0.0f && drop > HAT_CLEAR)
		lift = -(drop - HAT_CLEAR < HAT_LOWEST ? drop - HAT_CLEAR : HAT_LOWEST);
	/* along the joint's up, as an offset in the hat's own space, and the point
	 * it is sized about there */
	{
		float invh[4][4], aw[3], t[3], upw[3];
		if (!hatInvert(hatm, invh))
			goto done;
		hatXform(joint, anchor, aw);
		hatXform(invh, aw, t);
		out[4] = t[0];
		out[5] = t[1];
		out[6] = t[2];
		for (int k = 0; k < 3; k++)
			upw[k] = joint[1][k] * lift;
		for (int k = 0; k < 3; k++)
			out[k] = upw[0] * invh[0][k] + upw[1] * invh[1][k] + upw[2] * invh[2][k];
		out[3] = scale;
	}
	GEVR_BEAN_LOG("gevr bean: hat seated by its HD mesh: hair %.1f through its outside, sized %.2f (hair %.1f), "
	              "%.1f over the head, lifted %.1f\n", first, scale, hair, drop, lift);
	ok = 1;
done:
	free(head);
	free(atris);
	free(htris);
	free(pts);
	return ok;
}

int gevrBeanHatSeat(void *chrModel, const void *headHeader, void *hatModel, const float joint[4][4], int hattype,
                    float seat[7])
{
	Model *chr = chrModel, *hat = hatModel;
	if (!gevrBeanIsEnabled() || !chr || !headHeader || !hat || !hat->obj || !hat->render_pos || !chr->render_pos ||
	    !chr->obj || !joint)
		return 0;
	/* 1 a side cap, 2 a peaked cap, 3 a helmet (bondconstants.h HATTYPE_*) */
	const float maxscale = hattype == 1 ? 1.25f : (hattype == 2 || hattype == 3) ? 1.12f : 0.0f;
	const float maxlift = hattype == 1 ? 2.0f : 14.0f;
	for (int i = 0; i < HAT_CACHE; i++)
		if (s_hatSeats[i].head == headHeader && s_hatSeats[i].hat == hat->obj && s_hatSeats[i].type == hattype) {
			if (s_hatSeats[i].ok)
				memcpy(seat, s_hatSeats[i].seat, sizeof(s_hatSeats[i].seat));
			return s_hatSeats[i].ok;
		}
	const int at = s_hatSeatNext;
	s_hatSeatNext = (s_hatSeatNext + 1) % HAT_CACHE;
	s_hatSeats[at].head = headHeader;
	s_hatSeats[at].hat = hat->obj;
	s_hatSeats[at].type = hattype;
	s_hatSeats[at].ok = hatMeasure(chr, headHeader, hat, joint, maxscale, maxlift, s_hatSeats[at].seat);
	if (s_hatSeats[at].ok)
		memcpy(seat, s_hatSeats[at].seat, sizeof(s_hatSeats[at].seat));
	return s_hatSeats[at].ok;
}

/* the stage's models went with the stage */
static void hatSeatsReset(void)
{
	memset(s_hatSeats, 0, sizeof(s_hatSeats));
	s_hatSeatNext = 0;
}
