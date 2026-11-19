/*
 * Seams in a skinned Bean mesh (DAB gebean.c beanWeldSeams(),
 * beanWeldNeckWeights() and beanSmoothNeckWeights(); port/licenses/
 * dabs-mod.txt).
 *
 * The release's characters and heads are cut along their pictures' seams, so a
 * point of the mesh is two or more vertices, and Bean weights each on its own:
 * moved apart by their bones the copies opened a crack along the seam, the
 * light through Trevelyan's nape and a hairline round the throat looking up.
 * Here every corner of a triangle is its own vertex, so the copies are the
 * corners standing at one place:
 *
 *  - a body's, within half a unit at the rig's scale (DAB's BEAN_SEAM_GRID),
 *    each take the bones and weights of the first;
 *  - a head's that keeps its back weights, at exactly one place, one share
 *    between the neck and the back, their mean.
 *
 * Then the share a vertex gives the back against the neck, where it hangs from
 * those two alone: one far from its neighbours' (more than 0.3 off their mean,
 * with four of them or more) takes their mean - Natalya's lip was a spike of
 * back weight in a ring of neck. The copies of a point are smoothed as one, so
 * the smoothing cannot pull a seam's two halves apart again.
 *
 * Done once on the mesh's own bones and weights, the first time it is drawn,
 * so a body drawn whole and drawn limb by limb is skinned the same (bones of a
 * part are one matrix: the neck is any bone of GEVR_BEAN_SK_NECK).
 */
#define SEAM_GRID 2.0f

static const struct gevrBeanTri *s_seamTris;
static float s_seamScale;
static int s_seamExact;

static const float *seamPos(int v)
{
	return &s_seamTris[v / 3].p[(v % 3) * 3];
}

static long seamKey(int v, int k)
{
	return lroundf(seamPos(v)[k] * s_seamScale * SEAM_GRID);
}

static int seamCompare(const void *a, const void *b)
{
	const int x = *(const int *)a, y = *(const int *)b;
	if (s_seamExact) {
		const int c = memcmp(seamPos(x), seamPos(y), 3 * sizeof(float));
		if (c)
			return c;
	} else {
		for (int k = 0; k < 3; k++) {
			const long p = seamKey(x, k), q = seamKey(y, k);
			if (p != q)
				return p < q ? -1 : 1;
		}
	}
	return (x > y) - (x < y);
}

static int seamSame(int x, int y)
{
	if (s_seamExact)
		return !memcmp(seamPos(x), seamPos(y), 3 * sizeof(float));
	for (int k = 0; k < 3; k++)
		if (seamKey(x, k) != seamKey(y, k))
			return 0;
	return 1;
}

static uint8_t *seamBones(struct gevrBeanMesh *mesh, int v)
{
	return &mesh->tris[v / 3].bone[(v % 3) * 4];
}

static uint8_t *seamWeights(struct gevrBeanMesh *mesh, int v)
{
	return &mesh->tris[v / 3].weight[(v % 3) * 4];
}

/* 1 the neck, 2 the back, 0 anything else; a head's bones are its neck's but
 * the back's (emitHead()) */
static int seamClass(const struct gevrBeanMesh *mesh, int bone)
{
	if (s_seamExact)
		return bone < mesh->nbones && mesh->boneSk[bone] == GEVR_BEAN_SK_BACK ? 2 : 1;
	if (bone >= mesh->nbones)
		return 0;
	return mesh->boneSk[bone] == GEVR_BEAN_SK_NECK ? 1 : mesh->boneSk[bone] == GEVR_BEAN_SK_BACK ? 2 : 0;
}

/* the share of a corner's weight on the back against the neck, -1 when it
 * hangs from anything else */
static float seamShare(struct gevrBeanMesh *mesh, int v)
{
	const uint8_t *bone = seamBones(mesh, v), *weight = seamWeights(mesh, v);
	float n = 0.0f, b = 0.0f, other = 0.0f;
	for (int w = 0; w < 4; w++) {
		if (!weight[w])
			continue;
		const int c = seamClass(mesh, bone[w]);
		if (c == 1)
			n += weight[w];
		else if (c == 2)
			b += weight[w];
		else
			other += weight[w];
	}
	return n + b > 0.0f && other <= 0.0f ? b / (n + b) : -1.0f;
}

/* the corner hung from the neck and the back alone, the back's share given */
static void seamSetShare(struct gevrBeanMesh *mesh, int v, float share, int neckBone, int backBone)
{
	uint8_t *bone = seamBones(mesh, v), *weight = seamWeights(mesh, v);
	int slotNeck = -1, slotBack = -1;
	for (int w = 0; w < 4; w++) {
		const int c = seamClass(mesh, bone[w]);
		if (c == 1 && (slotNeck < 0 || (weight[w] && !weight[slotNeck])))
			slotNeck = w;
		else if (c == 2 && (slotBack < 0 || (weight[w] && !weight[slotBack])))
			slotBack = w;
	}
	if (slotNeck < 0) {
		slotNeck = slotBack == 0 ? 1 : 0;
		bone[slotNeck] = (uint8_t)neckBone;
	}
	if (slotBack < 0) {
		slotBack = slotNeck == 0 ? 1 : 0;
		bone[slotBack] = (uint8_t)backBone;
	}
	for (int w = 0; w < 4; w++)
		if (w != slotNeck && w != slotBack)
			weight[w] = 0;
	weight[slotBack] = (uint8_t)(share * 255.0f + 0.5f);
	weight[slotNeck] = (uint8_t)(255 - weight[slotBack]);
}

/* exact: a head's (the copies' mean share); else a body's at the rig's scale */
static void seamWeldMesh(const struct gevrBeanMesh *constMesh, float scale, int exact)
{
	struct gevrBeanMesh *mesh = (struct gevrBeanMesh *)constMesh;
	const int n = mesh->ntris * 3;
	int neckBone = -1, backBone = -1, welded = 0, smoothed = 0, ngroups = 0;
	if (mesh->seamsWelded || n <= 0)
		return;
	mesh->seamsWelded = 1;
	s_seamTris = mesh->tris;
	s_seamScale = scale;
	s_seamExact = exact;
	for (int b = 0; b < mesh->nbones; b++) {
		if (neckBone < 0 && seamClass(mesh, b) == 1)
			neckBone = b;
		if (backBone < 0 && seamClass(mesh, b) == 2)
			backBone = b;
	}
	int *order = malloc((size_t)n * sizeof(int)), *group = malloc((size_t)n * sizeof(int));
	if (!order || !group) {
		free(order);
		free(group);
		return;
	}
	for (int v = 0; v < n; v++)
		order[v] = v;
	qsort(order, (size_t)n, sizeof(int), seamCompare);
	for (int i = 0; i < n;) {
		int j = i + 1;
		while (j < n && seamSame(order[i], order[j]))
			j++;
		for (int k = i; k < j; k++)
			group[order[k]] = ngroups;
		if (exact && neckBone >= 0 && backBone >= 0) {
			/* DAB beanWeldNeckWeights(): the copies' mean share, unless one hangs
			 * from another bone (the corners of the triangles round a point stand
			 * there too: one share already, nothing to do) */
			float sum = 0.0f, lo = 1.0f, hi = 0.0f;
			int mixed = j - i < 2;
			for (int k = i; k < j && !mixed; k++) {
				const float share = seamShare(mesh, order[k]);
				mixed = share < 0.0f;
				sum += share;
				lo = fminf(lo, share);
				hi = fmaxf(hi, share);
			}
			for (int k = i; k < j && !mixed && hi - lo > 0.5f / 255.0f; k++) {
				seamSetShare(mesh, order[k], sum / (float)(j - i), neckBone, backBone);
				welded++;
			}
		} else if (!exact) {
			/* DAB beanWeldSeams(): each copy the first's bones and weights */
			const int from = order[i];
			for (int k = i + 1; k < j; k++) {
				uint8_t *bone = seamBones(mesh, order[k]), *weight = seamWeights(mesh, order[k]);
				if (!memcmp(bone, seamBones(mesh, from), 4) && !memcmp(weight, seamWeights(mesh, from), 4))
					continue;
				memcpy(bone, seamBones(mesh, from), 4);
				memcpy(weight, seamWeights(mesh, from), 4);
				welded++;
			}
		}
		ngroups++;
		i = j;
	}
	/* the back's share against the neck, smoothed where a point stands apart */
	float *sum = calloc((size_t)ngroups, sizeof(float)), *share = malloc((size_t)ngroups * sizeof(float));
	int *count = calloc((size_t)ngroups, sizeof(int)), *first = malloc((size_t)ngroups * sizeof(int));
	if (neckBone >= 0 && backBone >= 0 && sum && share && count && first) {
		for (int g = 0; g < ngroups; g++)
			first[g] = -1;
		for (int v = 0; v < n; v++)
			if (first[group[v]] < 0)
				first[group[v]] = v;
		for (int g = 0; g < ngroups; g++)
			share[g] = seamShare(mesh, first[g]);
		for (int t = 0; t < n; t += 3)
			for (int c = 0; c < 3; c++) {
				const int a = group[t + c], b = group[t + (c + 1) % 3];
				if (a == b || share[a] < 0.0f || share[b] < 0.0f)
					continue;
				sum[a] += share[b];
				count[a]++;
				sum[b] += share[a];
				count[b]++;
			}
		for (int v = 0; v < n; v++) {
			const int g = group[v];
			if (share[g] < 0.0f || count[g] < 4 || fabsf(share[g] - sum[g] / count[g]) <= 0.3f)
				continue;
			seamSetShare(mesh, v, sum[g] / count[g], neckBone, backBone);
			smoothed++;
		}
	}
	free(sum);
	free(share);
	free(count);
	free(first);
	free(order);
	free(group);
	if (welded || smoothed)
		GEVR_BEAN_LOG("gevr bean: %s: %d corners welded to their seam, %d neck weights smoothed\n",
		              exact ? "head" : "body", welded, smoothed);
}
