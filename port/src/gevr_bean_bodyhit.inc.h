/*
 * Shots against the HD bodies (DAB xblamesh.c xblaMeshHitTest(), from Dab's
 * chr hit walk; port/licenses/dabs-mod.txt).
 *
 * GoldenEye shoots a guard at boxes, one on each part's matrix
 * (objecthandler.c's walk over the chr's hit entries), sized to the N64
 * body: the release's hair, shoulders and coats could be hit only where the
 * N64 body happened to be. Where the boxes miss, the Bean meshes the model
 * draws (its body and its head) are posed as gevr_bean_pose.inc.h poses them,
 * from the same matrices the boxes are read with, so the triangles land in
 * the shot's own (view) space, and the shot is tested against them. The
 * nearest hit's triangle names the matrix it hangs from - its corners'
 * heaviest - whose box gives the part (chr.c).
 */
struct bodyHitWalk {
	Model *model;
	float o[3], d[3];
	float best;
	int matrix;
	const struct beanSkin *seen[16];
	int nseen;
};

/* Moeller-Trumbore, both faces: the distance along d, in d's lengths */
static int bodyHitTriangle(const float o[3], const float d[3], const float a[3], const float b[3], const float c[3],
                           float *t)
{
	float e1[3], e2[3], p[3], q[3], s[3];
	for (int k = 0; k < 3; k++) {
		e1[k] = b[k] - a[k];
		e2[k] = c[k] - a[k];
		s[k] = o[k] - a[k];
	}
	p[0] = d[1] * e2[2] - d[2] * e2[1];
	p[1] = d[2] * e2[0] - d[0] * e2[2];
	p[2] = d[0] * e2[1] - d[1] * e2[0];
	const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
	if (fabsf(det) < 1e-9f)
		return 0;
	const float inv = 1.0f / det;
	const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
	if (u < 0.0f || u > 1.0f)
		return 0;
	q[0] = s[1] * e1[2] - s[2] * e1[1];
	q[1] = s[2] * e1[0] - s[0] * e1[2];
	q[2] = s[0] * e1[1] - s[1] * e1[0];
	const float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
	if (v < 0.0f || u + v > 1.0f)
		return 0;
	*t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
	return *t > 0.0f;
}

static void bodyHitSkin(struct bodyHitWalk *w, const struct beanSkin *skin, ModelNode *node)
{
	Model *model = w->model;
	const int count = model->obj->numMatrices;
	const int anchorIndex = skin->anchor >= 0 ? skin->anchor : nodeMatrix(node);
	if (!skin->verts || anchorIndex < 0 || anchorIndex >= count)
		return;
	ModelNode *neck = node;
	while (neck && nodeOp(neck) != MODELNODE_OPCODE_GROUP)
		neck = neck->Parent;
	const int backIndex = neck ? nodeMatrix(neck->Parent) : -1;
	for (int t = 0; t + 2 < skin->nverts; t += 3) {
		float c[3][3], hit;
		int scoreM[12], scoreW[12], ns = 0, ok = 1;
		for (int k = 0; k < 3 && ok; k++) {
			const struct beanSkinVert *src = &skin->verts[t + k];
			float p[3] = { 0, 0, 0 }, total = 0;
			for (int wi = 0; wi < 4; wi++) {
				int matrix = src->matrix[wi];
				const int back = matrix == -3;
				if (matrix == -2)
					matrix = anchorIndex;
				if (back)
					matrix = backIndex >= 0 ? backIndex : anchorIndex;
				if (!src->weight[wi] || matrix < 0 || matrix >= count)
					continue;
				const float weight = src->weight[wi];
				float local[3];
				for (int j = 0; j < 3; j++)
					local[j] = src->p[wi][j] + (back && neck ? neck->Data->Group.Origin.f[j] : 0.0f);
				const float (*m)[4] = model->render_pos[matrix].pos.m;
				for (int j = 0; j < 3; j++)
					p[j] += weight * (local[0] * m[0][j] + local[1] * m[1][j] + local[2] * m[2][j] + m[3][j]);
				total += weight;
				int at;
				for (at = 0; at < ns && scoreM[at] != matrix; at++) {}
				if (at == ns && ns < 12) {
					scoreM[ns] = matrix;
					scoreW[ns++] = 0;
				}
				if (at < ns)
					scoreW[at] += src->weight[wi];
			}
			if (total <= 0.0f)
				ok = 0;
			else
				for (int j = 0; j < 3; j++)
					c[k][j] = p[j] / total;
		}
		if (!ok || !bodyHitTriangle(w->o, w->d, c[0], c[1], c[2], &hit) || hit >= w->best)
			continue;
		int heaviest = 0;
		for (int at = 1; at < ns; at++)
			if (scoreW[at] > scoreW[heaviest])
				heaviest = at;
		w->best = hit;
		w->matrix = ns ? scoreM[heaviest] : anchorIndex;
	}
}

/* every skin the model draws, its body's and its head's, each once */
static void bodyHitNodes(struct bodyHitWalk *w, ModelNode *node, int depth)
{
	for (; node && depth < 64; node = node->Next) {
		const struct beanSwap *s = activeSwap(node);
		if (s && s->skin) {
			int seen = 0;
			for (int i = 0; i < w->nseen && !seen; i++)
				seen = w->seen[i] == s->skin;
			if (!seen && w->nseen < (int)(sizeof(w->seen) / sizeof(w->seen[0]))) {
				w->seen[w->nseen++] = s->skin;
				bodyHitSkin(w, s->skin, node);
			}
		}
		bodyHitNodes(w, node->Child, depth + 1);
	}
}

int gevrBeanChrRayHit(void *chrModel, const float origin[3], const float dir[3], float *t, int *matrix)
{
	Model *model = chrModel;
	struct bodyHitWalk w;
	if (!gevrBeanIsEnabled() || !model || !model->obj || !model->render_pos || !origin || !dir)
		return 0;
	memset(&w, 0, sizeof(w));
	w.model = model;
	memcpy(w.o, origin, sizeof(w.o));
	memcpy(w.d, dir, sizeof(w.d));
	w.best = 1e30f;
	w.matrix = -1;
	bodyHitNodes(&w, model->obj->RootNode, 0);
	if (w.matrix < 0)
		return 0;
	*t = w.best;
	*matrix = w.matrix;
	return 1;
}
