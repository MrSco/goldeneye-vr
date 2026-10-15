/* Heads share the body's neck matrix only after model attachment. Ordinary
 * Bean heads blend their lower ring to the back; outfit heads are cut out
 * of the body and rotated from its spine onto GoldenEye's upright neck. */
static int emitHead(const struct gevrBeanMesh *mesh, int kind, void **gdl, void **vtx)
{
	if (!mesh->haveBind[GEVR_BEAN_SK_NECK])
		return 0;
	struct gevrEmitVert *vertices = calloc(mesh->ntris * 3, sizeof(*vertices));
	struct beanSkinVert *skin = calloc(mesh->ntris * 3, sizeof(*skin));
	if (!vertices || !skin) {
		free(vertices);
		free(skin);
		return 0;
	}
	float rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	if (kind == BEAN_HEADBODY && mesh->haveBind[GEVR_BEAN_SK_BACK]) {
		float spine[3], up[3] = {0, 1, 0}, length = 0;
		for (int k = 0; k < 3; k++) {
			spine[k] = mesh->bind[2][k] - mesh->bind[1][k];
			length += spine[k] * spine[k];
		}
		length = sqrtf(length);
		if (length > 1e-6f) {
			for (int k = 0; k < 3; k++)
				spine[k] /= length;
			rotBetween(spine, up, rotation);
		}
	}
	int count = 0;
	for (int t = 0; t < mesh->ntris; t++) {
		const struct gevrBeanTri *tri = &mesh->tris[t];
		if (!triWanted(mesh, tri, kind, 0))
			continue;
		for (int v = 0; v < 3; v++) {
			float d[3], p[3], n[3];
			for (int k = 0; k < 3; k++)
				d[k] = (tri->p[v * 3 + k] - mesh->bind[2][k]) * 0.213f;
			applyR(rotation, d, p);
			applyR(rotation, tri->n + v * 3, n);
			fillEmit(tri, tri->tex >= 0 && mesh->texCut[tri->tex], &vertices[count], p, n, v);
			for (int w = 0; w < 4; w++) {
				int bone = tri->bone[v * 4 + w];
				int back =
				    kind == BEAN_HEAD && bone < mesh->nbones && mesh->boneSk[bone] == GEVR_BEAN_SK_BACK;
				memcpy(skin[count].p[w], p, sizeof(p));
				memcpy(skin[count].n[w], n, sizeof(n));
				skin[count].matrix[w] = back ? -3 : -2;
				skin[count].weight[w] = tri->weight[v * 4 + w];
			}
			if (!(skin[count].weight[0] | skin[count].weight[1] | skin[count].weight[2] |
			      skin[count].weight[3]))
				skin[count].weight[0] = 255;
			count++;
		}
	}
	s_emitSkin = skin;
	int ok = count && gevrBeanEmitList(mesh, vertices, count, 1, 0, 1, 1, gdl, vtx);
	if (ok && s_emittedSkin)
		s_emittedSkin->head = 1;
	s_emitSkin = NULL;
	free(vertices);
	free(skin);
	return ok;
}
