#include "../src/gevr_bean.c"
#include "../src/gevr_static_mesh_build.inc.h"
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
	if ((body[0].words.w0 >> 24) == G_GEVR_MESH) body = (void *)body[1].words.w1;
	float rest[3];
	int n = 0;
	restOf(s->node, rest);
	*out = NULL;
	for (int c = 0; (body[c].words.w0 >> 24) != (G_ENDDL & 255); c++) {
        unsigned op=body[c].words.w0>>24;
        if (op!=(G_VTX&255) && op!=G_GEVR_WORLD_VTX) continue;
		const Vtx *v = (const Vtx *)(uintptr_t)body[c].words.w1;
		int count = (int)((body[c].words.w0 & 0xffff) / (op==G_GEVR_WORLD_VTX ? sizeof(GevrWorldVertex) : sizeof(Vtx)));
		*out = realloc(*out, (size_t)(n + count) * sizeof(**out));
		assert(*out);
		for (int j = 0; j < count; j++, n++)
			for (int k = 0; k < 3; k++)
				(*out)[n][k] = (op==G_GEVR_WORLD_VTX ? ((const GevrWorldVertex *)v)[j].p[k] : v[j].v.ob[k]) + rest[k];
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

/* GoldenEye's hand lists still drawn as they are (no replacement registered) */
static int nativeHandLists(ModelFileHeader *h, ModelNode *n)
{
	int count = 0;
	for (; n; n = n->Next) {
		int op = nodeOp(n);
		if ((op == MODELNODE_OPCODE_DL || op == MODELNODE_OPCODE_DLCOLLISION) && n->Data && gunHandNode(h, n) &&
		    !swapOf(n)) {
			Vertex *v;
			int vc, type;
			Gfx *primary;
			dlVerts(n, &v, &vc, &type, &primary);
			count += vc > 0;
		}
		ModelNode *child = n->Child;
		if (!child && op == MODELNODE_OPCODE_HEADER && n->Data)
			child = n->Data->Header.FirstGroupNode;
		if (!child && op == MODELNODE_OPCODE_GROUP && n->Data)
			child = n->Data->Group.ChildGroupNode;
		count += nativeHandLists(h, child);
	}
	return count;
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
		/* The gun's own list is registered first, then its moving parts, each
		 * on its own matrix's untoggled list, then the glove's. */
		if (kind == BEAN_GUN && !lists++) {
			anchor = s_swap[i].node;
		} else if (kind == BEAN_GUN && gunHandNode(h, s_swap[i].node)) {
			assert(!hand);
			hand = s_swap[i].node;
		} else if (kind == BEAN_GUN) {
			assert(!hand && !listToggled(h, s_swap[i].node) && nodeMatrix(s_swap[i].node) != nodeMatrix(anchor));
		}
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
		assert(anchor && (!listToggled(h, anchor) || !candidates));
		/* Bean's glove takes GoldenEye's hand; without one the hand stays. */
		if (glove && handLists)
			assert(hand && gunHandNode(h, hand));
		else
			assert(!hand && !handBlanks);
		/* Bean's glove brings its own index finger: none of GoldenEye's hand
		 * stays, its trigger finger on a joint of its own included */
		if (glove && handLists)
			assert(!nativeHandLists(h, h->RootNode));
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

static int influenceCompare(const void *a,const void *b)
{
    const struct beanSkinVert *x=a,*y=b;
    int c=memcmp(x->p,y->p,sizeof(x->p));if(c)return c;
    c=memcmp(x->n,y->n,sizeof(x->n));if(c)return c;
    c=memcmp(x->matrix,y->matrix,sizeof(x->matrix));if(c)return c;
    c=memcmp(x->weight,y->weight,sizeof(x->weight));if(c)return c;
    return memcmp(&x->tint,&y->tint,sizeof(x->tint));
}

static void posedBodyCheck(ModelFileHeader *header,struct beanSkin *body)
{
    Model model={0};model.obj=header;
    model.render_pos=calloc(header->numMatrices,sizeof(*model.render_pos));assert(model.render_pos);
    for(int pose=0;pose<3;pose++) {
        for(int b=0;b<header->numMatrices;b++) {
            float (*m)[4]=model.render_pos[b].pos.m;
            float angle=.17f*(b+1)*(pose+1),s=sinf(angle),c=cosf(angle);
            memset(m,0,sizeof(model.render_pos[b].pos.m));
            m[0][0]=c*(1+.02f*b);m[0][1]=s*(1+.02f*b);
            m[1][0]=-s;m[1][1]=c;m[2][2]=1+.01f*b;m[3][3]=1;
            m[3][0]=100+3*b;m[3][1]=-80+pose*7+b;m[3][2]=60-2*b;
        }
        Gfx *g=gevrBeanWholeModelGdl(&model);assert(g && g[1].words.w0>>24==G_GEVR_SKIN);
        GevrSkinDraw *draw=(void*)g[1].words.w1;
        const GevrSkinVertex *vertices=draw->mesh->vertices;
        const float (*anchor)[4]=model.render_pos[body->anchor].pos.m;
        for(int v=0;v<body->nverts;v++) {
            float expected[3]={0},local[3]={0},total=0;
            for(int w=0;w<4;w++) {
                int weight=body->verts[v].weight[w];if(!weight)continue;
                const float (*m)[4]=model.render_pos[body->verts[v].matrix[w]].pos.m;
                const float *p=body->verts[v].p[w];
                const float *rows=draw->palette.rows[vertices[v].bone[w]];
                for(int k=0;k<3;k++) {
                    expected[k]+=weight*(m[0][k]*p[0]+m[1][k]*p[1]+m[2][k]*p[2]+m[3][k]);
                    local[k]+=weight*(rows[k*4]*p[0]+rows[k*4+1]*p[1]+rows[k*4+2]*p[2]+rows[k*4+3]);
                }
                total+=weight;
            }
            assert(total>0);
            for(int k=0;k<3;k++) {
                float actual=anchor[3][k];
                for(int j=0;j<3;j++)actual+=anchor[j][k]*local[j]/total;
                assert(fabsf(actual-expected[k]/total)<.02f);
            }
        }
        beanFrameReset();
    }
    free(model.render_pos);
}

static void combinedBodyCheck(ModelFileHeader *header,struct gevrBeanMesh *mesh,int kind,const struct beanFit *fit)
{
    if(kind!=BEAN_CHR || mesh->watchArm)return;
    int count=0,parts=0;
    for(int i=0;i<s_nswap;i++)if(s_swap[i].skin){
        int duplicate=0;
        for(int j=0;j<i;j++)duplicate|=s_swap[j].skin==s_swap[i].skin;
        if(!duplicate){count+=s_swap[i].skin->nverts;parts++;}
    }
    if(parts<4)return;
    struct beanSkinVert *before=calloc(count,sizeof(*before));assert(before);
    int n=0;
    for(int i=0;i<s_nswap;i++)if(s_swap[i].skin){
        int duplicate=0;
        for(int j=0;j<i;j++)duplicate|=s_swap[j].skin==s_swap[i].skin;
        if(duplicate)continue;
        memcpy(before+n,s_swap[i].skin->verts,s_swap[i].skin->nverts*sizeof(*before));n+=s_swap[i].skin->nverts;
    }
    qsort(before,count,sizeof(*before),influenceCompare);
    /* Reset the registry/storage but keep the independently loaded source mesh. */
    gevrBeanReset();s_want=s_available=1;
    FILE *f=fopen("gevr_body_batch.txt","w");assert(f);fputs("1",f);fclose(f);
    applyMesh(header,mesh,kind,fit);remove("gevr_body_batch.txt");
    int afterCount=0,afterParts=0;
    for(int i=0;i<s_nswap;i++)if(s_swap[i].skin){
        int duplicate=0;
        for(int j=0;j<i;j++)duplicate|=s_swap[j].skin==s_swap[i].skin;
        if(!duplicate){afterCount+=s_swap[i].skin->nverts;afterParts++;}
    }
    /* Switched bodies intentionally retain their per-part draws. */
    if(afterParts==1) {
        assert(afterCount==count);
        struct beanSkin *body=NULL;
        for(int i=0;i<s_nswap;i++)if(s_swap[i].skin)body=s_swap[i].skin;
        struct beanSkinVert *after=malloc(afterCount*sizeof(*after));assert(after);
        memcpy(after,body->verts,afterCount*sizeof(*after));
        qsort(after,afterCount,sizeof(*after),influenceCompare);
        for(int i=0;i<count;i++)assert(!influenceCompare(before+i,after+i));
        assert(beanSkinMesh(body) && body->mesh->numIndices==(unsigned)count);
        posedBodyCheck(header,body);
        printf("COMBINED %d parts -> 1, %d identical weighted corners, %u material draws, 3 affine poses\n",parts,count,body->mesh->numRuns);
        free(after);
    } else assert(afterParts==parts && afterCount==count);
    free(before);
}

/* GoldenEye's skeletons by the name the model header gives (the game's own
 * are compiled from assets/embedded/skeletons); only these few are told apart. */
ModelSkeleton skeleton_cctv, skeleton_eyelid_door, skeleton_iris_door, skeleton_door, skeleton_prop_weapon;
static ModelSkeleton s_otherSkeleton;
static ModelSkeleton *skeletonNamed(const char *name)
{
	return !strcmp(name, "cctv") ? &skeleton_cctv : !strcmp(name, "eyelid_door") ? &skeleton_eyelid_door
	     : !strcmp(name, "iris_door") ? &skeleton_iris_door : !strcmp(name, "door") ? &skeleton_door
	     : !strcmp(name, "prop_weapon") ? &skeleton_prop_weapon : !strcmp(name, "none") ? NULL : &s_otherSkeleton;
}

/* A screen's width along its first edge and across it (beanFindScreens()'s axes). */
static void screenExtent(const Vertex *q, const struct beanScreen *s, float out[2])
{
	for (int a = 0; a < 2; a++) {
		float lo = 1e30f, hi = -1e30f;
		for (int c = 0; c < 4; c++) {
			float on = 0;
			for (int k = 0; k < 3; k++)
				on += (q[c].coord.AsArray[k] - s->origin[k]) * s->axis[a][k];
			lo = on < lo ? on : lo;
			hi = on > hi ? on : hi;
		}
		out[a] = hi - lo;
	}
}

/* GoldenEye's monitor screens under Bean's mesh (gevr_bean_screens.inc.h, DAB
 * gebeanBuildRigid() and xblamesh.c): the panes of beanVertexDrops[] are not
 * drawn where Bean has them (black behind a screen, else left out), a screen
 * list the programme wrote is the game's while its monitor is named and Bean's
 * otherwise, and a fitted screen takes the programme's corners. */
static void checkScreens(ModelFileHeader *h, const struct gevrBeanMesh *mesh, int kind, const struct beanFit *fit,
                         const char *name)
{
	if (kind != BEAN_PROP)
		return;
	const __typeof__(s_screenCounts) counts = s_screenCounts; /* applyMesh's, as its log gives them */
	int panes = 0, panesDrawn = 0, handed = 0, fitted = 0, recess = 0, mainScreen = 0;
	float pane[2] = {0, 0}, quad[2] = {0, 0};
	struct beanScreen screens[4];
	int numscreens = beanFindScreens(h, screens);
	struct beanSwap *main = NULL;
	for (int i = 0; i < s_nswap && !main; i++)
		if (s_swap[i].owner == h && s_swap[i].ours != s_blank)
			main = &s_swap[i];
	for (int d = 0; d < mesh->ndraws; d++) {
		int shared = 0; /* draws share a buffer: count its vertices once */
		for (int e = 0; e < d; e++)
			shared |= mesh->draws[e].state && mesh->draws[e].vboff == mesh->draws[d].vboff;
		for (uint32_t v = 0; !shared && mesh->draws[d].state && v < mesh->draws[d].nverts; v++)
			panes += (mesh->draws[d].state[v] & GEVR_BEAN_V_DROPPED) != 0;
	}
	/* The panes' triangles alone through the production emitter: Bean's grey
	 * card is drawn only black, 2% of its screen's size behind it, or not at all */
	if (panes && fit && main && mesh->triSrc) {
		struct gevrBeanMesh only = *mesh;
		struct beanScreenBuild sb;
		struct beanRigidBinding binding;
		void *gdl = NULL, *vtx = NULL;
		only.tris = calloc(mesh->ntris, sizeof(*only.tris));
		only.triSrc = calloc(mesh->ntris, sizeof(*only.triSrc));
		assert(only.tris && only.triSrc);
		only.ntris = 0;
		for (int t = 0; t < mesh->ntris; t++)
			if (mesh->triSrc[t].pane) {
				only.tris[only.ntris] = mesh->tris[t];
				only.triSrc[only.ntris++] = mesh->triSrc[t];
			}
		memset(&binding, 0, sizeof(binding));
		beanScreensBuild(&sb, h, mesh, fit, &binding, main->node);
		s_screenBuild = &sb;
		if (only.ntris && emitRigid(&only, BEAN_PROP, 0, main->node, fit, NULL, -1, NULL, -1, &gdl, &vtx)) {
			for (const Gfx *g = gdl; (g->words.w0 >> 24) != (G_ENDDL & 255); g++) {
				if ((g->words.w0 >> 24) != (G_VTX & 255))
					continue;
				const Vtx *v = (const Vtx *)(uintptr_t)g->words.w1;
				for (int j = 0; j < (int)((g->words.w0 & 0xffff) / sizeof(Vtx)); j++, panesDrawn++) {
					float best = 1e30f, behind = 0;
					assert(!(v[j].v.cn[0] | v[j].v.cn[1] | v[j].v.cn[2]));
					for (int i = 0; i < numscreens; i++) {
						float w = 0;
						for (int k = 0; k < 3; k++)
							w += (v[j].v.ob[k] - screens[i].origin[k]) * screens[i].axis[2][k];
						if (fabsf(w + screens[i].size * 0.02f) < best) {
							best = fabsf(w + screens[i].size * 0.02f);
							behind = w;
						}
					}
					assert(best <= 1.0f && behind < 0);
				}
			}
			free(gdl);
			free(vtx);
		}
		s_screenBuild = NULL;
		beanScreensFinish(&sb, NULL, NULL, NULL, 0);
		free(only.tris);
		free(only.triSrc);
	}

	Model model = {0};
	u32 datas[64] = {0};
	int rw = 0;
	model.obj = h;
	model.datas = datas;
	for (int i = 0; i < numscreens; i++) {
		ModelNode *n = h->Switches[screens[i].part];
		Vertex *verts, moved[4];
		int count, type;
		Gfx *primary;
		dlVerts(n, &verts, &count, &type, &primary);
		/* The programme's corners: Bean's screen when it has one of its own */
		memcpy(moved, verts, sizeof(moved));
		recess |= gevrBeanScreenQuad(&model, n, moved) << screens[i].part;
		if (memcmp(moved, verts, sizeof(moved))) {
			fitted |= 1 << screens[i].part;
			if (!pane[0]) {
				screenExtent(moved, &screens[i], pane);
				screenExtent(verts, &screens[i], quad);
			}
			/* a hundredth of the screen's size in front of its plane */
			for (int c = 0; c < 4; c++) {
				float w = 0;
				for (int k = 0; k < 3; k++)
					w += (moved[c].coord.AsArray[k] - screens[i].origin[k]) * screens[i].axis[2][k];
				assert(fabsf(w - screens[i].size * 0.01f) <= 1.0f);
			}
			s_want = 0; /* Bean off: GoldenEye's quad */
			memcpy(moved, verts, sizeof(moved));
			assert(!gevrBeanScreenQuad(&model, n, moved) && !memcmp(moved, verts, sizeof(moved)));
			s_want = 1;
		}
		/* A list the programme wrote this frame is the game's (DAB
		 * xblaMeshNodeIsLiveScreen()), only while its monitor is named */
		struct beanSwap *s = swapOf(n);
		if (nodeOp(n) != MODELNODE_OPCODE_DLCOLLISION || !s || rw + 4 > 64)
			continue;
		ModelRwData_DisplayList_CollisionRecord *record = (void *)&datas[rw];
		n->Data->DisplayListCollisions.RwDataIndex = (u16)rw;
		rw += sizeof(*record) / 4;
		Gfx programme[1];
		gSPEndDisplayList(programme);
		record->gdl = primary;
		gevrBeanSetScreens(&model);
		assert(gevrBeanShownGdl(n, primary) == s->ours && gevrBeanShownModelType(n, type) == 0);
		record->gdl = programme;
		assert(gevrBeanShownGdl(n, programme) == programme && gevrBeanShownModelType(n, type) == type);
		assert(gevrBeanRenderGdl(n, programme, &model) == programme);
		/* the mesh's own list stays Bean's (a CCTV's mesh hangs on its part 0,
		 * but only a monitor is ever named) */
		mainScreen |= main && main->node == n;
		assert(!main || main->node == n || gevrBeanShownGdl(main->node, main->original) == main->ours);
		gevrBeanSetScreens(NULL);
		assert(gevrBeanShownGdl(n, programme) == s->ours && gevrBeanShownModelType(n, type) == 0);
		s_want = 0;
		assert(gevrBeanShownGdl(n, programme) == programme && gevrBeanShownModelType(n, type) == type);
		s_want = 1;
		record->gdl = NULL;
		handed++;
	}
	printf("SCREENS %s backing %d recess %d fitted %d panes %d screens %d handed %d drawn %d main %d pane %.0fx%.0f "
	       "quad %.0fx%.0f\n", name, counts.numbacking, counts.numrecess, fitted, panes, numscreens, handed, panesDrawn,
	       mainScreen, pane[0], pane[1], quad[0], quad[1]);
	assert(fitted == counts.screenfit && recess == counts.screenrecess);
}

int main(int argc, char **argv)
{
	assert(argc == 9);
	ModelFileHeader header;
	void *native = beanNativeModel(argv[1], argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), &header);
	assert(native);
	header.Skeleton = skeletonNamed(argv[8]);
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
    FILE *legacy=fopen("gevr_body_batch.txt","w");assert(legacy);fputs("0",legacy);fclose(legacy);
	applyMesh(&header, mesh, kind, fitForName(argv[2]));
    remove("gevr_body_batch.txt");
	/* DAB beanGunFlashDraws(): a gun pickup's painted muzzle flash, left out */
	if (kind == BEAN_PROP && header.Skeleton == &skeleton_prop_weapon && mesh->draws && mesh->triSrc) {
		const uint64_t flash = beanGunFlashDraws(mesh);
		int flashTris = 0;
		for (int t = 0; t < mesh->ntris; t++) {
			const int draw = mesh->triSrc[t].draw;
			flashTris += draw >= 0 && draw < 64 && (flash >> draw & 1);
		}
		/* what was laid into the lists is every wanted triangle but the flash's */
		int emitted = 0, expected = 0;
		for (int i = 0; i < s_nswap; i++) {
			float (*v)[3];
			if (s_swap[i].ours != s_blank) {
				emitted += swapVerts(&s_swap[i], &v) / 3;
				free(v);
			}
		}
		s_flashMesh = mesh;
		s_flashDraws = flash;
		for (int t = 0; t < mesh->ntris; t++)
			expected += triWanted(mesh, &mesh->tris[t], BEAN_PROP, 0);
		s_flashMesh = NULL;
		s_flashDraws = 0;
		printf("FLASH %s draws %d mask %llx tris %d emitted %d expected %d\n", argv[2], mesh->ndraws,
		       (unsigned long long)flash, flashTris, emitted, expected);
		assert(!flash || emitted == expected);
	}
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
            /* the body on the root's list; a moving part on its own matrix's
             * untoggled list (DAB gebeanBuildFirstPerson()) */
            assert(gunHandNode(&header, node) || nodeMatrix(node) == nodeMatrix(header.RootNode) ||
                   !listToggled(&header, node));
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
            unsigned groups=0;
            for(int c=0;c<pose->ncommands;c++) groups += (pose->gdl[c].words.w0>>24)==G_GEVR_ROOM_STATE;
            assert(pose->mesh->numRuns<=groups); /* Tint loads must never split a material draw. */
            const GevrSkinVertex *gpu=pose->mesh->vertices;
            for(int v=0;v<pose->nverts;v++) assert(!memcmp(&gpu[v].tint,&pose->verts[v].tint,sizeof(GevrVertexTint)));
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
    /* DAB fpRound: only the rocket launcher has a build for an empty tube,
     * on the same list, smaller by its round */
    assert(gevrBeanHasRound(&header) == !strcmp(argv[2], "GrocketlaunchZ"));
    for (int i = 0; i < s_nswap; i++)
        if (s_swap[i].spent) {
            struct beanSwap loaded = s_swap[i], spent = s_swap[i];
            float (*a)[3], (*b)[3];
            spent.ours = spent.spent;
            const int na = swapVerts(&loaded, &a), nb = swapVerts(&spent, &b);
            printf("ROUND %s loaded %d spent %d vertices\n", argv[2], na, nb);
            assert(nb > 0 && na - nb >= 3 * 100);
            free(a);
            free(b);
        }
    checkScreens(&header, mesh, kind, fitForName(argv[2]), argv[2]);
    if (kind == BEAN_GUN) {
        Model model = {0}; model.obj = &header;
        model.render_pos = calloc(header.numMatrices, sizeof(*model.render_pos));
        float root[4][4] = {{0,2,0,0},{-2,0,0,0},{0,0,2,0},{17,23,41,1}};
        assert(gevrBeanPreviewPose(&model, root));
        /* the watch draws the root's lists with the root at the origin */
        float origin[3]; restOf(header.RootNode,origin);
        for(int i=0;i<s_nswap;i++) {
            const struct beanSwap *s=&s_swap[i]; if(s->ours==s_blank) continue;
            int matrix=nodeMatrix(s->node); float rest[3]; restOf(s->node,rest);
            for(int k=0;k<3;k++) rest[k]-=origin[k];
            float (*mv)[4]=model.render_pos[matrix].pos.m;
            for(int k=0;k<3;k++) {
                float expected=root[3][k]; for(int j=0;j<3;j++) expected+=rest[j]*root[j][k];
                assert(fabsf(mv[3][k]-expected)<1e-3f);
            }
            Gfx *body=(void *)((Gfx *)s->ours)[1].words.w1;
            assert((body[0].words.w0>>24)==G_GEVR_MESH);
            GevrStaticMesh *gpu=(void *)body[0].words.w1;
            assert(gpu->numIndices && gpu->numRuns);
            /* The retained VBO's first point lands where the fitted list did. */
            const Vtx *v=gpu->vertices; int index=gpu->indices[0];
            for(int k=0;k<3;k++) {
                float actual=mv[3][k],expected=root[3][k];
                for(int j=0;j<3;j++) { actual+=v[index].v.ob[j]*mv[j][k]; expected+=(v[index].v.ob[j]+rest[j])*root[j][k]; }
                assert(fabsf(actual-expected)<.02f);
            }
        }
        s_want=0; assert(!gevrBeanPreviewPose(&model,root)); s_want=1;
        free(model.render_pos);
    }

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
		int clock[3] = {0}, glass = 0;
		for (int i = 0; i < s_nswap; i++)
			if (s_swap[i].skin) {
                struct beanSkin *pose = s_swap[i].skin;
                uintptr_t texture = 0;
                for (int c=0;c<pose->ncommands;c++) {
                    Gfx *g=&pose->gdl[c]; unsigned op=g->words.w0>>24;
                    if(op==(G_SETTIMG&255)) texture=g->words.w1;
                    if(op==(G_TRI1&255)&&texture==(uintptr_t)mesh->standin[11]) glass++;
                }
				for (int v = 0; v < pose->nverts; v++)
					for (int w = 0; w < 4; w++)
						if (pose->verts[v].weight[w] && pose->verts[v].matrix[w] >= 1 &&
						    pose->verts[v].matrix[w] <= 3)
							clock[pose->verts[v].matrix[w] - 1]++;
			}
		assert(clock[0] && clock[1] && clock[2]);
		assert(glass == 135); /* The six outfits do not multiply the glass. */
	}
	if (!strcmp(argv[2], "GtriggerZ") || !strcmp(argv[2], "GwatchlaserZ")) {
		/* the body, six outfit sleeves, and the pressing hand on GoldenEye's
		 * palm and finger (Switches[6]), which the headset's grip draws alone */
		assert(lists == 9 && mesh->triggerCuffs);
		int finger = 0;
		for (int i = 0; i < s_nswap; i++)
			finger += s_swap[i].ours != s_blank && nodeMatrix(s_swap[i].node) == nodeMatrix(header.Switches[6]);
		assert(finger == 1);
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
	combinedBodyCheck(&header,mesh,kind,fitForName(argv[2]));
	gevrBeanMeshFree(mesh);
	gevrBeanReset();
	free(native);
	return 0;
}
