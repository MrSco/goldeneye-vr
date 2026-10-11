/* Weighted bind-pose mapping follows DAB's importer (port/licenses/dabs-mod.txt).
 * Pose and commands are copied into the frame arena: model definitions are
 * shared by instances, and the renderer consumes the lists after this walk. */
static int beanInverse3(const float a[9], float out[9])
{
	float d = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) +
	          a[2] * (a[3] * a[7] - a[4] * a[6]);
	if (!isfinite(d) || fabsf(d) < 1e-12f)
		return 0;
	out[0] = (a[4] * a[8] - a[5] * a[7]) / d;
	out[1] = (a[2] * a[7] - a[1] * a[8]) / d;
	out[2] = (a[1] * a[5] - a[2] * a[4]) / d;
	out[3] = (a[5] * a[6] - a[3] * a[8]) / d;
	out[4] = (a[0] * a[8] - a[2] * a[6]) / d;
	out[5] = (a[2] * a[3] - a[0] * a[5]) / d;
	out[6] = (a[3] * a[7] - a[4] * a[6]) / d;
	out[7] = (a[1] * a[6] - a[0] * a[7]) / d;
	out[8] = (a[0] * a[4] - a[1] * a[3]) / d;
	return 1;
}

void *gevrBeanRenderGdl(void *node, void *current, Model *model)
{
	struct beanSwap *swap = activeSwap(node);
	struct beanSkin *skin = swap ? swap->skin : NULL;
	if (swap && swap->original && !current)
		return NULL;
	if (gevrBeanIsEnabled() && swap && swap->deferredMatrix && !skin) {
		int matrix = nodeMatrix(node);
		if (matrix >= 0) {
			Gfx *wrapper = beanFrameAllocate(3 * sizeof(Gfx));
			if (wrapper) {
				gSPMatrix(wrapper, (void *)(uintptr_t)(0x03000000u + matrix * sizeof(Mtx)),
				          G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
				gSPDisplayList(wrapper + 1, swap->ours);
				gSPEndDisplayList(wrapper + 2);
				return wrapper;
			}
		}
	}
	if (!gevrBeanIsEnabled() || !skin || !model || !model->render_pos || !model->obj)
		return gevrBeanShownGdl(node, current);
	int count = model->obj->numMatrices;
	int anchorIndex = skin->anchor >= 0 ? skin->anchor : nodeMatrix(node);
	if (anchorIndex < 0 || anchorIndex >= count)
		return current;
	ModelNode *neck = node;
	while (neck && nodeOp(neck) != MODELNODE_OPCODE_GROUP)
		neck = neck->Parent;
	int backIndex = neck ? nodeMatrix(neck->Parent) : -1;
	const float (*anchor)[4] = model->render_pos[anchorIndex].pos.m;
	float linear[9], inverse[9];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			linear[i * 3 + j] = anchor[j][i];
	if (!beanInverse3(linear, inverse))
		return current;
	Vtx *vertices = beanFrameAllocate((size_t)skin->nverts * sizeof(Vtx));
	Gfx *commands = beanFrameAllocate((size_t)(skin->ncommands + 3) * sizeof(Gfx));
	if (!vertices || !commands)
		return current;
	memcpy(vertices, skin->vtx, skin->nverts * sizeof(Vtx));
	for (int v = 0; v < skin->nverts; v++) {
		const struct beanSkinVert *src = &skin->verts[v];
		float p[3] = {0}, n[3] = {0}, total = 0;
		for (int w = 0; w < 4; w++) {
			int matrix = src->matrix[w];
			int back = matrix == -3;
			if (matrix == -2)
				matrix = anchorIndex;
			if (back)
				matrix = backIndex >= 0 ? backIndex : anchorIndex;
			if (!src->weight[w] || matrix < 0 || matrix >= count)
				continue;
			const float (*m)[4] = model->render_pos[matrix].pos.m;
			float weight = src->weight[w];
			float local[3];
			for (int k = 0; k < 3; k++)
				local[k] = src->p[w][k] + (back && neck ? neck->Data->Group.Origin.f[k] : 0);
			for (int k = 0; k < 3; k++) {
				p[k] += weight * (local[0] * m[0][k] + local[1] * m[1][k] + local[2] * m[2][k] + m[3][k]);
				n[k] += weight * (src->n[w][0] * m[0][k] + src->n[w][1] * m[1][k] + src->n[w][2] * m[2][k]);
			}
			total += weight;
		}
		if (total <= 0)
			continue;
		for (int k = 0; k < 3; k++)
			p[k] = p[k] / total - anchor[3][k];
		float local[3], normal[3];
		applyR(inverse, p, local);
		applyR(inverse, n, normal);
		float length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
		for (int k = 0; k < 3; k++) {
			vertices[v].v.ob[k] = posClamp(local[k]);
			vertices[v].n.n[k] = length > 1e-6f ? (s8)(normal[k] * 127.0f / length) : 0;
		}
	}
	gSPMatrix(commands, (void *)(uintptr_t)(0x03000000u + anchorIndex * sizeof(Mtx)),
	          G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
	memcpy(commands + 1, skin->gdl, skin->ncommands * sizeof(Gfx));
	for (int c = 1; c <= skin->ncommands; c++) {
		if ((commands[c].words.w0 >> 24) == (G_VTX & 255)) {
			uintptr_t offset = commands[c].words.w1 - (uintptr_t)skin->vtx;
			if (offset < (uintptr_t)skin->nverts * sizeof(Vtx))
				commands[c].words.w1 = osVirtualToPhysical((uint8_t *)vertices + offset);
		}
	}
	return commands;
}
