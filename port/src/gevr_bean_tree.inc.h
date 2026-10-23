/* Instanced pine UVs adapted from DAB gebean.c beanTreeUvs (aae28c8,
 * port/licenses/dabs-mod.txt). Run on local geometry before its world matrix. */
/* A tree's buffers carry no UVs: its branch cards (stride 20: position,
 * normal, colour) and its trunk (stride 28: position, normal, tangent,
 * binormal, colour) have them made by a shader the level file does not hold.
 * They are made here from the geometry instead.
 *
 * A card is four vertices of its own, two at the trunk and two at the tip, in
 * one of two orders; the branch picture has its base at the right edge
 * (u 1) and its tip at the left. The trunk is wrapped once round its axis,
 * the bark's 1:2 picture keeping its shape.
 */
struct beantree {
	float axis[2];   // the buffer's middle in plan: the trunk's axis
	float radius;    // the trunk's, at its foot
	float ymin;
};

static int readTreeVtx(const uint8_t *gpu, const struct beanVb *vb, uint32_t i, struct beanVtx *v)
{
	if (!readVtx(gpu, vb, i, 1, 0, v)) return 0;
	v->argb = colourArgb(be32(gpu + vb->off + i * vb->stride + vb->stride - 4));
	v->hasuv = 0;
	return 1;
}

static void beanTreeMeasure(const uint8_t *gpu, const struct beanVb *vb, struct beantree *tree)
{
	double sum[2] = {0, 0};
	double rsum = 0;
	int rn = 0;

	tree->ymin = 1e30f;

	for (uint32_t i = 0; i < vb->count; i++) {
		const uint8_t *p = gpu + vb->off + i * vb->stride;

		sum[0] += bef(p);
		sum[1] += bef(p + 8);

		if (bef(p + 4) < tree->ymin) {
			tree->ymin = bef(p + 4);
		}
	}

	tree->axis[0] = vb->count ? sum[0] / vb->count : 0;
	tree->axis[1] = vb->count ? sum[1] / vb->count : 0;

	for (uint32_t i = 0; i < vb->count; i++) {
		const uint8_t *p = gpu + vb->off + i * vb->stride;

		if (bef(p + 4) < tree->ymin + 1) {
			const float dx = bef(p) - tree->axis[0];
			const float dz = bef(p + 8) - tree->axis[1];

			rsum += sqrtf(dx * dx + dz * dz);
			rn++;
		}
	}

	tree->radius = rn && rsum > 0 ? rsum / rn : 50;
}

static float beanTreeDistance(const uint8_t *gpu, const struct beanVb *vb, const struct beantree *tree, uint32_t i)
{
	const uint8_t *p = gpu + vb->off + i * vb->stride;
	const float dx = bef(p) - tree->axis[0];
	const float dz = bef(p + 8) - tree->axis[1];

	return sqrtf(dx * dx + dz * dz);
}

static void beanTreeUvs(const uint8_t *gpu, const struct beanVb *vb, const struct beantree *tree,
		const uint32_t *idx, struct beanVtx *v)
{
	if (vb->stride == 20) {
		for (int k = 0; k < 3; k++) {
			const uint32_t card = idx[k] & ~3u;
			float near0, near1;

			if (card + 3 >= vb->count) {
				v[k].uv[0] = v[k].uv[1] = 0;
				continue;
			}

			near0 = beanTreeDistance(gpu, vb, tree, card) + beanTreeDistance(gpu, vb, tree, card + 1);
			near1 = beanTreeDistance(gpu, vb, tree, card + 2) + beanTreeDistance(gpu, vb, tree, card + 3);

			// The pair nearer the trunk is the branch's base
			v[k].uv[0] = ((idx[k] & 2) != 0) == (near1 < near0) ? 1 : 0;
			v[k].uv[1] = (idx[k] & 1) ? 0 : 1;
		}
	} else {
		const float tile = 2 * 2 * 3.14159265358979323846f * tree->radius; // one picture's height
		int onaxis[3];
		float lo = 1, hi = 0, sum = 0;
		int n = 0;

		for (int k = 0; k < 3; k++) {
			const float dx = v[k].pos[0] - tree->axis[0];
			const float dz = v[k].pos[2] - tree->axis[1];

			onaxis[k] = dx * dx + dz * dz < tree->radius * tree->radius * 0.0625f;
			v[k].uv[0] = onaxis[k] ? 0 : atan2f(dz, dx) / (2 * 3.14159265358979323846f) + 0.5f;
			v[k].uv[1] = (tree->ymin - v[k].pos[1]) / tile;

			if (!onaxis[k]) {
				lo = v[k].uv[0] < lo ? v[k].uv[0] : lo;
				hi = v[k].uv[0] > hi ? v[k].uv[0] : hi;
			}
		}

		// Across the seam, carry the low side round
		for (int k = 0; k < 3; k++) {
			if (!onaxis[k]) {
				if (hi - lo > 0.5f && v[k].uv[0] < 0.5f) {
					v[k].uv[0] += 1;
				}

				sum += v[k].uv[0];
				n++;
			}
		}

		// The cone's tip is on the axis: under the middle of its own face
		for (int k = 0; k < 3; k++) {
			if (onaxis[k]) {
				v[k].uv[0] = n ? sum / n : 0;
			}
		}
	}
}
