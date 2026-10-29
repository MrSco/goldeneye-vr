static void registryChecks(void)
{
    ModelNode nodes[MAX_SWAP + 1] = {0};
    ModelFileHeader owners[2] = {0};
    Gfx lists[2] = {0};
    s_nswap = 0;
    swapIndexRebuild();
    for (int i=0; i<MAX_SWAP; i++) {
        s_registrationOwner = &owners[i & 1];
        swapAdd(&nodes[i], NULL, &lists[i & 1], 0);
    }
    assert(s_nswap == MAX_SWAP);
    assert(!swapOf(&nodes[MAX_SWAP]));
    for (int i=0; i<MAX_SWAP; i++)
        assert(swapOf(&nodes[i])->ours == &lists[i & 1]);
    gevrBeanForgetModel(&owners[0]);
    for (int i=0; i<MAX_SWAP; i++)
        assert((swapOf(&nodes[i]) != NULL) == (i & 1));
    for (int i=0; i<MAX_SWAP; i+=2) swapAdd(&nodes[i], NULL, &lists[0], 0);
    gevrBeanForgetModelStorage(nodes, sizeof(nodes));
    assert(s_nswap == 0 && !swapOf(&nodes[1]));
    s_registrationOwner = NULL;
}

static const Model *cutTarget;
static int cutActive(const Model *model) { return model == cutTarget; }
static int cutPiece(const Model *model,int matrix) { (void)model;return matrix; }
static int cutRemap(const Model *model,int owner,int matrix,float p[3],float n[3])
{ (void)model;(void)matrix;(void)p;(void)n;return owner; }

/* Cut caps on Bean's skin: two fans of eight triangles meet at a shared
 * rim (separate vertices with the same source data, as the skin repeats
 * them per triangle). On different pieces, each side's border is the
 * whole rim; an open rim on a triangle across the cut counts too, a
 * stray open edge does not. */
static void capChecks(void)
{
    struct beanSkinVert verts[16 * 3];
    memset(verts, 0, sizeof(verts));
    int tripiece[16], triother[16];
    for (int side = 0; side < 2; side++)
        for (int i = 0; i < 8; i++) {
            struct beanSkinVert *v = &verts[(side * 8 + i) * 3];
            for (int c = 0; c < 3; c++) { v[c].weight[0] = 255; v[c].matrix[0] = 0; }
            v[0].p[0][1] = side ? -5.0f : 5.0f; /* each side's own centre */
            v[1].p[0][0] = cosf(i * 0.785398f) * 10; v[1].p[0][2] = sinf(i * 0.785398f) * 10;
            v[2].p[0][0] = cosf((i + 1) * 0.785398f) * 10; v[2].p[0][2] = sinf((i + 1) * 0.785398f) * 10;
            if (i == 7) { v[2].p[0][0] = verts[(side * 8) * 3 + 1].p[0][0]; v[2].p[0][2] = verts[(side * 8) * 3 + 1].p[0][2]; }
            tripiece[side * 8 + i] = side ? 3 : 0;
            triother[side * 8 + i] = -1;
        }
    struct beanSkin skin = {0};
    skin.verts = verts; skin.nverts = 48;
    const struct beanCapCache *cache = beanCapLoops(&skin, 0x4, tripiece, triother);
    assert(cache && cache->nloops == 2);
    for (int l = 0; l < 2; l++) {
        assert(cache->loops[l].nedges == 8);
        assert(cache->loops[l].other == (cache->loops[l].piece ? 0 : 3));
    }
    assert(beanCapLoops(&skin, 0x4, tripiece, triother) == cache); /* cached */
    /* One side only, open at the cut: its triangles straddle piece 3 */
    struct beanSkin half = {0};
    half.verts = verts; half.nverts = 24;
    for (int i = 0; i < 8; i++) triother[i] = 3;
    cache = beanCapLoops(&half, 0x4, tripiece, triother);
    assert(cache && cache->nloops == 1 && cache->loops[0].nedges == 8 && cache->loops[0].other == 3);
    for (int i = 0; i < 8; i++) triother[i] = -1;
    cache = beanCapLoops(&half, 0x8, tripiece, triother);
    assert(cache && cache->nloops == 0);
    /* Welds are owned by the level allocation arena. */
}

static void poseChecks(void)
{
    capChecks();
    registryChecks();
	union ModelRoData groupData = {0};
	groupData.Group.MatrixIDs[0] = 0;
	ModelNode group = {0}, node = {0};
	group.Opcode = MODELNODE_OPCODE_GROUP;
	group.Data = &groupData;
	node.Opcode = MODELNODE_OPCODE_DL;
	node.Parent = &group;
	struct beanSkinVert influences[3] = {0};
	Vtx original[3] = {0};
	Gfx list[3];
	for (int i = 0; i < 3; i++) {
		original[i].v.tc[0] = 123;
		influences[i].weight[0] = 128;
		influences[i].weight[1] = 127;
		influences[i].matrix[0] = 0;
		influences[i].matrix[1] = 1;
		influences[i].p[1][0] = 10;
		influences[i].n[0][2] = influences[i].n[1][2] = 1;
	}
	gSPVertex(list, original, 3, 0);
	gSP1Triangle(list + 1, 0, 1, 2, 0);
	gSPEndDisplayList(list + 2);
	struct beanSkin skin = {
	    .verts = influences, .vtx = original, .gdl = list, .nverts = 3, .ncommands = 3, .anchor = 0};
	RenderPosView matrices[2] = {0};
	for (int i = 0; i < 2; i++)
		for (int k = 0; k < 4; k++)
			matrices[i].pos.m[k][k] = 1;
	matrices[0].pos.m[3][0] = 100;
	matrices[1].pos.m[3][0] = 120;
	matrices[1].pos.m[0][0] = 0;
	matrices[1].pos.m[0][1] = 1;
	matrices[1].pos.m[1][0] = -1;
	matrices[1].pos.m[1][1] = 0;
	ModelFileHeader header = {0};
	header.numMatrices = 2;
	Model model = {0};
	model.obj = &header;
	model.render_pos = matrices;
	s_nswap = 0;
	swapIndexRebuild();
	s_emittedSkin = &skin;
	swapAdd(&node, NULL, list, 0);
	Gfx *first = gevrBeanRenderGdl(&node, list, &model);
    assert((first[1].words.w0 >> 24) == G_GEVR_SKIN);
    GevrSkinDraw *firstDraw = (void *)first[1].words.w1;
    assert(!firstDraw->posedVertices);
    assert(firstDraw->mesh == skin.mesh && skin.mesh->numIndices == 3);
    Vtx *firstVertices = firstDraw->pose(firstDraw);
    assert(firstDraw->pose(firstDraw) == firstVertices);
	assert(first != list && firstVertices != original);
	assert(firstVertices[0].v.ob[0] == 10 && firstVertices[0].v.ob[1] == 5);
	assert(firstVertices[0].v.tc[0] == 123 && firstVertices[0].n.n[2] == 127);
    /* Dismemberment must choose one piece per triangle, even when its
     * corners favour different bones. Moving the other piece cannot stretch
     * the cut triangle. Other instances keep their shared GPU skin. */
    struct beanSkinVert saved[3];memcpy(saved,influences,sizeof(saved));
    influences[0].weight[0]=255;influences[0].weight[1]=0;
    influences[1].weight[0]=0;influences[1].weight[1]=255;
    cutTarget=&model;
    GevrBeanCuts cuts={cutActive,cutPiece,cutRemap};gevrBeanSetCuts(&cuts);
    Gfx *cutFirst=gevrBeanRenderGdl(&node,list,&model);
    assert((cutFirst[1].words.w0>>24)==(G_VTX&255));
    Vtx *cutVertices=(void *)cutFirst[1].words.w1;
    matrices[1].pos.m[3][0]+=500;
    Gfx *cutSecond=gevrBeanRenderGdl(&node,list,&model);
    Vtx *cutAgain=(void *)cutSecond[1].words.w1;
    for(int v=0;v<3;v++)for(int k=0;k<3;k++)assert(cutVertices[v].v.ob[k]==cutAgain[v].v.ob[k]);
    Model other=model;
    Gfx *uncut=gevrBeanRenderGdl(&node,list,&other);
    assert((uncut[1].words.w0>>24)==G_GEVR_SKIN);
    assert(((GevrSkinDraw *)(uintptr_t)uncut[1].words.w1)->mesh==firstDraw->mesh);
    matrices[1].pos.m[3][0]-=500;
    gevrBeanSetCuts(NULL);memcpy(influences,saved,sizeof(saved));
	/* Another instance may move before the first list reaches the GPU. */
	matrices[1].pos.m[3][0] = 200;
	Gfx *second = gevrBeanRenderGdl(&node, list, &model);
    GevrSkinDraw *secondDraw = (void *)second[1].words.w1;
    assert(secondDraw->mesh == firstDraw->mesh && !secondDraw->posedVertices);
    assert(secondDraw->palette.rows[1][3] == 100 && firstDraw->palette.rows[1][3] == 20);
    Vtx *secondVertices = secondDraw->pose(secondDraw);
	assert(secondVertices[0].v.ob[0] == 50 && firstVertices[0].v.ob[0] == 10);
	assert(list[0].words.w1 == (uintptr_t)original && original[0].v.ob[0] == 0);
    /* Palette and legacy CPU paths agree for rotated/scaled anchors, neck
     * routing, missing influences and position clamping. */
    influences[0].matrix[0] = -2;
    influences[0].matrix[1] = -3;
    influences[0].matrix[2] = 7; /* invalid: exclude from the weight total */
    influences[0].weight[2] = 50;
    influences[1].p[0][0] = -0.7f;
    influences[1].p[1][1] = 0.4f;
    influences[2].p[0][0] = influences[2].p[1][0] = 100000;
    groupData.Group.Origin.f[0] = 3;
    matrices[0].pos.m[0][0] = 0;
    matrices[0].pos.m[0][1] = 2;
    matrices[0].pos.m[1][0] = -3;
    matrices[0].pos.m[1][1] = 0;
    matrices[0].pos.m[2][2] = .5f;
    struct beanSkin paletteSkin = {
        .verts = influences, .vtx = original, .gdl = list, .nverts = 3, .ncommands = 3, .anchor = 0};
    struct beanSkin legacySkin = paletteSkin;
    legacySkin.meshTried = 1; /* force the unchanged legacy CPU renderer */
    s_swap[0].skin = &legacySkin;
    Gfx *legacy = gevrBeanRenderGdl(&node, list, &model);
    Vtx *legacyVertices = (void *)legacy[1].words.w1;
    s_swap[0].skin = &paletteSkin;
    Gfx *palette = gevrBeanRenderGdl(&node, list, &model);
    GevrSkinDraw *paletteDraw = (void *)palette[1].words.w1;
    Vtx *paletteVertices = paletteDraw->pose(paletteDraw);
    for (int i = 0; i < 3; i++) {
        assert(paletteVertices[i].v.tc[0] == legacyVertices[i].v.tc[0]);
        for (int k = 0; k < 3; k++) {
            assert(abs(paletteVertices[i].v.ob[k] - legacyVertices[i].v.ob[k]) <= 1);
            assert(abs(paletteVertices[i].n.n[k] - legacyVertices[i].n.n[k]) <= 1);
        }
    }
	gevrBeanSetEnabled(0);
	assert(gevrBeanRenderGdl(&node, list, &model) == list);
	gevrBeanSetEnabled(1);
	gevrBeanBeginFrame();
	assert((uintptr_t)beanFrameAllocate(300000) % 16 == 0);
	assert(firstVertices[0].v.ob[0] == 10);
	gevrBeanBeginFrame();
	assert(!beanFrameAllocate(BEAN_FRAME_LIMIT + 1));
	s_nswap = 0;
	swapIndexRebuild();
	/* Detached head data receives its matrix only after attachment. */
	swapAdd(&node, NULL, list, 0);
	s_swap[0].deferredMatrix = 1;
	groupData.Group.MatrixIDs[0] = 1;
	Gfx *head = gevrBeanRenderGdl(&node, list, &model);
	assert(head[0].words.w1 == 0x03000040u && head[1].words.w1 == (uintptr_t)list);
	s_nswap = 0;
	swapIndexRebuild();
}
