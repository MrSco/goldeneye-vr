#include "../src/gevr_bean.c"
int gevrRomIsVariant(void) { return 0; } /* GoldenEye: no hack campaign */
#include <assert.h>
#undef assert
#define assert(test)                                                                                         \
	do {                                                                                                     \
		if (!(test)) {                                                                                       \
			fprintf(stderr, "mapping assertion: %s at %d\n", #test, __LINE__);                               \
			exit(2);                                                                                         \
		}                                                                                                    \
	} while (0)

void gfx_static_mesh_release(GevrStaticMesh *mesh) { (void)mesh; }
int VrBeanModels = 1;
int VrGexGuns;
uintptr_t osVirtualToPhysical(void *p)
{
	return (uintptr_t)p;
}
void videoFreeCachedTexture(const void *p)
{
	(void)p;
}
void gevrBeanFontReset(void) {}
void gevrBeanWorldReset(void)
{
}
void *gevrBeanWorldRoom(void *g, int r, int *replaced)
{
	(void)r;
	*replaced = 0;
	return g;
}
void *gevrBeanWorldSky(void *g, int *drew)
{
	*drew = 0;
	return g;
}
Vtx *dynAllocateVertices(s32 count)
{
	return calloc(count, sizeof(Vtx));
}
void *dynAllocate(s32 size)
{
	return calloc(1, size);
}
extern void *beanNativeModel(const char *, const char *, int, int, int, ModelFileHeader *);

/* A node's rest: the group origins above it, as applyMesh adds them up. */
static void restOf(const ModelNode *n, float o[3])
{
	o[0] = o[1] = o[2] = 0;
	for (; n; n = n->Parent) {
		const coord3d *c = !n->Data                                   ? NULL
		                   : nodeOp(n) == MODELNODE_OPCODE_GROUP       ? &n->Data->Group.Origin
		                   : nodeOp(n) == MODELNODE_OPCODE_GROUPSIMPLE ? &n->Data->GroupSimple.Origin
		                                                               : NULL;
		for (int k = 0; c && k < 3; k++)
			o[k] += c->f[k];
	}
}

/* The vertices a registered list draws, in the model's rest space. */
static int swapVerts(const struct beanSwap *s, float (**out)[3])
{
	const Gfx *body = (const Gfx *)(uintptr_t)((const Gfx *)s->ours)[1].words.w1;
	float rest[3];
	int n = 0;
	restOf(s->node, rest);
	*out = NULL;
	for (int c = 0; (body[c].words.w0 >> 24) != (G_ENDDL & 255); c++) {
		if ((body[c].words.w0 >> 24) != (G_VTX & 255))
			continue;
		const Vtx *v = (const Vtx *)(uintptr_t)body[c].words.w1;
		int count = (int)((body[c].words.w0 & 0xffff) / sizeof(Vtx));
		*out = realloc(*out, (size_t)(n + count) * sizeof(**out));
		assert(*out);
		for (int j = 0; j < count; j++, n++)
			for (int k = 0; k < 3; k++)
				(*out)[n][k] = v[j].v.ob[k] + rest[k];
	}
	return n;
}

/* A gun's untoggled native lists, which Bean's gun stands in for, and whether
 * any of them could carry it (on the root matrix, off the joints). */
static void nativeGun(ModelFileHeader *h, ModelNode *n, float (**out)[3], int *count, int *candidates,
                      int *handLists)
{
	for (; n; n = n->Next) {
		int op = nodeOp(n);
		if ((op == MODELNODE_OPCODE_DL || op == MODELNODE_OPCODE_DLCOLLISION) && n->Data) {
			int joint = 0, simple = 0, owner = 1;
			for (const ModelNode *up = n->Parent; up; up = up->Parent) {
				int upOp = nodeOp(up);
				simple |= upOp == MODELNODE_OPCODE_GROUPSIMPLE;
				if (owner && upOp == MODELNODE_OPCODE_GROUP && up->Data)
					joint = up->Data->Group.JointID != 0;
				owner &= upOp != MODELNODE_OPCODE_GROUP && upOp != MODELNODE_OPCODE_HEADER &&
				         upOp != MODELNODE_OPCODE_GROUPSIMPLE;
			}
			*handLists += !joint && !simple && gunHandNode(h, n);
			if (!joint && !simple && !listToggled(h, n)) {
				Vertex *v;
				int vc, type;
				Gfx *primary;
				float rest[3];
				dlVerts(n, &v, &vc, &type, &primary);
				restOf(n, rest);
				*candidates += nodeMatrix(n) == nodeMatrix(h->RootNode);
				*out = realloc(*out, (size_t)(*count + vc) * sizeof(**out));
				assert(*out);
				for (int j = 0; j < vc; j++, (*count)++)
					for (int k = 0; k < 3; k++)
						(*out)[*count][k] = v[j].coord.AsArray[k] + rest[k];
			}
		}
		ModelNode *child = n->Child;
		if (!child && op == MODELNODE_OPCODE_HEADER && n->Data)
			child = n->Data->Header.FirstGroupNode;
		if (!child && op == MODELNODE_OPCODE_GROUP && n->Data)
			child = n->Data->Group.ChildGroupNode;
		nativeGun(h, child, out, count, candidates, handLists);
	}
}

static double signedVolume(float (*p)[3], int n)
{
	double o[3] = {0}, v = 0;
	for (int i = 0; i < n; i++)
		for (int k = 0; k < 3; k++)
			o[k] += p[i][k] / n;
	for (int i = 0; i + 2 < n; i += 3) {
		double a[3], b[3], c[3];
		for (int k = 0; k < 3; k++) {
			a[k] = p[i][k] - o[k];
			b[k] = p[i + 1][k] - o[k];
			c[k] = p[i + 2][k] - o[k];
		}
		v += a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0]);
	}
	return v / 6;
}

/* Rigid replacements against the cartridge: Bean's first-person gun is
 * GoldenEye's model at 4.7 times in GoldenEye's frame (DAB gebean.c:775), it
 * hangs on an untoggled list (gebean.c:12966), the glove goes with GoldenEye's
 * hand, and no fit turns a model inside out (gebean.c:8304). */
static void checkRigid(ModelFileHeader *h, const struct gevrBeanMesh *mesh, int kind, const struct beanFit *fit,
                       const char *name)
{
	if (!fit || (kind != BEAN_GUN && kind != BEAN_PROP) || mesh->triggerCuffs ||
	    !strcmp(fit->source, "prop/helicopter"))
		return;
	float (*bean)[3] = NULL, (*native)[3] = NULL;
	int nb = 0, nn = 0, candidates = 0, handLists = 0, lists = 0, handBlanks = 0, glove = 0, wanted = 0;
	ModelNode *anchor = NULL, *hand = NULL;
	for (int t = 0; t < mesh->ntris; t++)
		glove |= kind == BEAN_GUN && triWanted(mesh, &mesh->tris[t], kind, 0) && gunGloveTri(mesh, &mesh->tris[t]);
	if (kind == BEAN_GUN)
		nativeGun(h, h->RootNode, &native, &nn, &candidates, &handLists);
	for (int i = 0; i < s_nswap; i++) {
		handBlanks += kind == BEAN_GUN && s_swap[i].ours == s_blank && gunHandNode(h, s_swap[i].node);
		if (s_swap[i].ours == s_blank)
			continue;
		/* The gun's own list is registered first, then the glove's. */
		if (kind == BEAN_GUN && lists++)
			hand = s_swap[i].node;
		else if (kind == BEAN_GUN)
			anchor = s_swap[i].node;
		float (*v)[3];
		int n = swapVerts(&s_swap[i], &v);
		bean = realloc(bean, (size_t)(nb + n) * sizeof(*bean));
		assert(bean);
		memcpy(bean + nb, v, (size_t)n * sizeof(*v));
		nb += n;
		free(v);
	}
	assert(nb > 0 && nb % 3 == 0);
	if (kind == BEAN_GUN) {
		assert(anchor && lists <= 2 && (!listToggled(h, anchor) || !candidates));
		/* Bean's glove takes GoldenEye's hand; without one the hand stays. */
		if (glove && handLists)
			assert(hand && gunHandNode(h, hand));
		else
			assert(!hand && !handBlanks);
	}

	/* Same orientation as the release: a mirrored fit winds the other way. */
	float (*source)[3] = malloc((size_t)mesh->ntris * 3 * sizeof(*source));
	assert(source);
	for (int t = 0; t < mesh->ntris; t++) {
		const struct gevrBeanTri *tri = &mesh->tris[t];
		if (!triWanted(mesh, tri, kind, 0) || (kind == BEAN_GUN && gunGloveTri(mesh, tri) && !hand))
			continue;
		memcpy(source[wanted * 3], tri->p, 9 * sizeof(float));
		wanted++;
	}
	double expected = signedVolume(source, wanted * 3) * fit->scale * fit->scale * fit->scale;
	double drawn = signedVolume(bean, nb);
	if (wanted * 3 == nb && fabs(expected) > 1000.0)
		assert(drawn / expected > 0.5 && drawn / expected < 2.0);

	double top = -1, nearest = 0;
	if (kind == BEAN_GUN) {
		/* SKEL_TOP binds at 4.7 times GoldenEye's root group (DAB gebean.c:787). */
		for (int b = 0; b < mesh->nbones; b++)
			if (!strcmp(mesh->boneName[b], "SKEL_TOP")) {
				float at[3], root[3];
				fitPoint(fit, mesh->boneBind[b], at);
				restOf(h->RootNode, root);
				top = sqrtf((at[0] - root[0]) * (at[0] - root[0]) + (at[1] - root[1]) * (at[1] - root[1]) +
				            (at[2] - root[2]) * (at[2] - root[2]));
				assert(top < 1.5);
			}
		/* Every vertex of GoldenEye's gun has Bean's nearby. */
		for (int i = 0; i < nn; i++) {
			float best = 1e30f;
			for (int j = 0; j < nb; j++) {
				float d = 0;
				for (int k = 0; k < 3; k++)
					d += (native[i][k] - bean[j][k]) * (native[i][k] - bean[j][k]);
				if (d < best)
					best = d;
			}
			nearest += sqrtf(best) / nn;
		}
		/* Bean's watch-alone magnet file (DAB gebean.c:510) lacks GoldenEye's arm. */
		assert(nearest < (strcmp(name, "GwatchmagnetattractZ") ? 30.0 : 60.0));
	}
	printf("PLACED %s nearest %.1f volume %.3f top %.2f glove %d\n", name, nearest,
	       fabs(expected) > 1000.0 && wanted * 3 == nb ? drawn / expected : 0.0, top, hand != NULL);
	free(source);
	free(bean);
	free(native);
}

int main(int argc, char **argv)
{
	assert(argc == 8);
	ModelFileHeader header;
	void *native = beanNativeModel(argv[1], argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), &header);
	assert(native);
	char source[192];
	int kind = sourceForName(argv[2], source, sizeof(source));
	assert(kind);
	struct gevrBeanMesh *mesh = NULL;
	int loaded = gevrBeanLoad(argv[6], 1, &mesh);
	if (!loaded) {
		printf("FALLBACK %s\n", argv[2]);
		free(native);
		return 0;
	}
	s_want = s_available = 1;
	applyMesh(&header, mesh, kind, fitForName(argv[2]));
	int lists = 0, skin = 0;
	for (int i = 0; i < s_nswap; i++) {
		ModelNode *node = s_swap[i].node;
		Gfx *primary = nodeOp(node) == MODELNODE_OPCODE_DL ? node->Data->DisplayList.Primary
		                                                     : node->Data->DisplayListCollisions.Primary;
		int type = nodeOp(node) == MODELNODE_OPCODE_DL ? node->Data->DisplayList.ModelType
		                                                : node->Data->DisplayListCollisions.ModelType;
		assert(primary == s_swap[i].original && type == s_swap[i].origType);
		assert(!primary || ((uintptr_t)primary >> 24) == 5);
		if (s_swap[i].ours == s_blank)
			continue;
        if (kind == BEAN_GUN) {
            assert(!gunFlashNode(&header, node));
            assert(gunHandNode(&header, node) || nodeMatrix(node) == nodeMatrix(header.RootNode));
        }
		lists++;
		skin += s_swap[i].skin != NULL;
		int matrix = nodeMatrix(s_swap[i].node);
		if (kind == BEAN_HEAD || kind == BEAN_HEADBODY) {
			assert(s_swap[i].deferredMatrix);
			continue;
		}
		assert(matrix >= 0 && matrix < header.numMatrices);
		assert(((Gfx *)s_swap[i].ours)->words.w0 >> 24 == (G_MTX & 255));
		assert(((Gfx *)s_swap[i].ours)->words.w1 == 0x03000000u + matrix * sizeof(Mtx));
		if (s_swap[i].skin) {
			struct beanSkin *pose = s_swap[i].skin;
            assert(beanSkinMesh(pose));
            assert(pose->mesh->numVertices == (unsigned)pose->nverts);
            assert(pose->mesh->numIndices == (unsigned)pose->nverts);
            assert(pose->mesh->numRuns > 0 && pose->nbones <= GEVR_SKIN_BONES);
			for (int v = 0; v < pose->nverts; v++)
				for (int w = 0; w < 4; w++) {
					if (!pose->verts[v].weight[w])
						continue;
					assert(pose->verts[v].matrix[w] >= 0 && pose->verts[v].matrix[w] < header.numMatrices);
					for (int k = 0; k < 3; k++)
						assert(isfinite(pose->verts[v].p[w][k]));
				}
		}
	}
	checkRigid(&header, mesh, kind, fitForName(argv[2]), argv[2]);

    if (!strcmp(argv[2], "CdjbondZ") || !strcmp(argv[2], "Cgreatguard2Z")) {
        /* Check actual cartridge joint identities against the mesh sections,
         * rather than merely accepting valid but wrong matrix indices. */
        static const int skForJoint[16] = {-1, 0, 1, 2, 4, 7, 5, 8, 6, 9, 10, 13, 11, 14, 12, 15};
        unsigned seen = 0;
        for (int i = 0; i < s_nswap; i++) {
            if (!s_swap[i].skin) continue;
            ModelNode *joint = s_swap[i].node;
            while (joint && nodeOp(joint) != MODELNODE_OPCODE_GROUP) joint = joint->Parent;
            assert(joint && joint->Data->Group.JointID < 16);
            int expected = skForJoint[joint->Data->Group.JointID], triangles = 0;
            assert(expected >= 0);
            seen |= 1u << expected;
            for (int t = 0; t < mesh->ntris; t++)
                if (triWanted(mesh, &mesh->tris[t], BEAN_CHR, 1) &&
                    (mesh->tris[t].sk == expected ||
                     (expected == GEVR_BEAN_SK_BASE && (mesh->tris[t].sk == GEVR_BEAN_SK_POS || !mesh->haveBind[mesh->tris[t].sk]))))
                    triangles++;
            assert(s_swap[i].skin->nverts == triangles * 3);
        }
        assert(seen == 0xfff7u); /* Base, back, neck and all four full limbs. */
    }
	printf("REGISTERED %s %d lists %d skinned %d bones\n", argv[2], lists, skin, mesh->nbones);
	if (!lists) {
		fprintf(stderr, "swaps %d, dominant bones:", s_nswap);
		int counts[16] = {0};
		for (int t = 0; t < mesh->ntris; t++)
			counts[mesh->tris[t].sk]++;
		for (int k = 0; k < 16; k++)
			fprintf(stderr, " %d:%d", k, counts[k]);
		fprintf(stderr, "\n");
	}
	assert(lists > 0);
	if (!strcmp(argv[2], "Csuit_lf_handZ")) {
		assert(lists == 7); /* One bare watch, six outfit switches. */
		int clock[3] = {0};
		for (int i = 0; i < s_nswap; i++)
			if (s_swap[i].skin) {
				struct beanSkin *pose = s_swap[i].skin;
				for (int v = 0; v < pose->nverts; v++)
					for (int w = 0; w < 4; w++)
						if (pose->verts[v].weight[w] && pose->verts[v].matrix[w] >= 1 &&
						    pose->verts[v].matrix[w] <= 3)
							clock[pose->verts[v].matrix[w] - 1]++;
			}
		assert(clock[0] && clock[1] && clock[2]);
	}
	if (!strcmp(argv[2], "GtriggerZ")) {
		assert(lists == 7 && mesh->triggerCuffs);
		int cuffs[6] = {0};
		for (int t = 0; t < mesh->ntris; t++)
			if (mesh->tris[t].cuff >= 0) {
				assert(mesh->tris[t].cuff < 6);
				cuffs[mesh->tris[t].cuff]++;
			}
		for (int c = 0; c < 6; c++)
			assert(cuffs[c]);
	}
	if (!strcmp(argv[2], "PhelicopterZ")) {
		assert(lists == 3); /* Body and two animated rotors. */
		int pilot = 0;
		for (int i = 0; i < s_nswap; i++) {
			Gfx *wrapper = s_swap[i].ours;
			if (wrapper == s_blank)
				continue;
			Gfx *body = (void *)wrapper[1].words.w1;
			if ((body[0].words.w0 >> 24) == (G_DL & 255) && (body[1].words.w0 >> 24) == (G_DL & 255))
				pilot++;
		}
		assert(pilot == 1);
	}
	if (!strcmp(argv[2], "PnintendologoZ")) {
        assert(mesh->nativeChrome && lists > 0);
        for (int i = 0; i < s_nswap; i++) {
            if (s_swap[i].ours == s_blank) continue;
            Gfx *wrapper = s_swap[i].ours;
            Gfx *list = (void *)wrapper[1].words.w1;
            int generated = 0, textured = 0, vertices = 0;
            for (Gfx *g = list; (g->words.w0 >> 24) != (G_ENDDL & 255); g++) {
                unsigned op = g->words.w0 >> 24;
                textured |= op == (G_SETTIMG & 255);
                generated |= op == (G_SETGEOMETRYMODE & 255) && (g->words.w1 & G_TEXTURE_GEN);
                if (op == (G_VTX & 255)) {
                    Vtx *v = (void *)g->words.w1;
                    unsigned count = (g->words.w0 & 0xffff) / sizeof(Vtx);
                    for (unsigned j = 0; j < count; j++) {
                        assert(v[j].n.a == 255);
                        assert(abs(v[j].n.n[0]) + abs(v[j].n.n[1]) + abs(v[j].n.n[2]) > 100);
                    }
                    vertices += count;
                }
            }
            assert(generated && textured && vertices == mesh->ntris * 3);
        }
		for (int t = 0; t < mesh->ntris; t++)
			for (int k = 0; k < 3; k++) assert(mesh->tris[t].rgba[k * 4 + 3] == 255);
	}
	gevrBeanMeshFree(mesh);
	gevrBeanReset();
	free(native);
	return 0;
}
