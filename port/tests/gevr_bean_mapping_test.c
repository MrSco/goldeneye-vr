#include "../src/gevr_bean.c"
#include <assert.h>
#undef assert
#define assert(test)                                                                                         \
	do {                                                                                                     \
		if (!(test)) {                                                                                       \
			fprintf(stderr, "mapping assertion: %s at %d\n", #test, __LINE__);                               \
			exit(2);                                                                                         \
		}                                                                                                    \
	} while (0)

int VrBeanModels = 1;
uintptr_t osVirtualToPhysical(void *p)
{
	return (uintptr_t)p;
}
void videoFreeCachedTexture(const void *p)
{
	(void)p;
}
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
		if (s_swap[i].ours == s_blank)
			continue;
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
	printf("MAPPED %s %d lists %d skinned %d bones\n", argv[2], lists, skin, mesh->nbones);
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
	gevrBeanMeshFree(mesh);
	gevrBeanReset();
	free(native);
	return 0;
}
