/* Exercise the actual model dispatchers, not only the replacement registry. */
void modelApplyRenderModeType1(ModelRenderData *r) { (void)r; }
void modelApplyRenderModeType2(ModelRenderData *r) { (void)r; }
void modelApplyRenderModeType3(ModelRenderData *r, bool p) { (void)r; (void)p; }
void modelApplyRenderModeType4(ModelRenderData *r, bool p) { (void)r; (void)p; }
void modelApplyCullMode(ModelRenderData *r) { (void)r; }
/* PRODUCTION MODEL DISPATCH */
static void materialPathChecks(void)
{
    Gfx native[4], secondary[1], output[32];
    gSPEndDisplayList(native); gSPEndDisplayList(secondary);
    union ModelRoData ro = {0};
    ModelNode node = {0}; node.Data = &ro;
    Model model = {0};
    for (int collision = 0; collision < 2; collision++) {
        node.Opcode = collision ? MODELNODE_OPCODE_DLCOLLISION : MODELNODE_OPCODE_DL;
        memset(&ro, 0, sizeof(ro));
        if (collision) {
            ro.DisplayListCollisions.Primary = native;
            ro.DisplayListCollisions.Secondary = secondary;
            ro.DisplayListCollisions.ModelType = 4;
            collisionRw.gdl = native;
        } else {
            ro.DisplayList.Primary = native;
            ro.DisplayList.Secondary = secondary;
            ro.DisplayList.ModelType = 4;
        }
        swapAdd(&node, native, secondary, 4);
        for (int flags = 1; flags <= 3; flags++) {
            ModelRenderData r = {0}; r.gdl = output; r.flags = flags;
            if (collision) modelRenderNodeDl(&r, &model, &node);
            else modelRenderNodeGundl(&r, &model, &node);
            int draws = 0;
            for (Gfx *g = output; g < r.gdl; g++)
                if ((g->words.w0 >> 24) == (G_DL & 255)) {
                    assert(g->words.w1 == (uintptr_t)secondary);
                    draws++;
                }
            assert(draws == 1); /* flags=2 glass appears, flags=3 never doubles it. */
        }
        assert(!gevrBeanTransparentGdl(&node, NULL, &model, 2)); /* shattered/hidden */
        s_nswap = 0; swapIndexRebuild();
    }
    /* Native chrome setup survives, vertices carry normals and opaque alpha. */
    node.Opcode = MODELNODE_OPCODE_DL;
    memset(&ro, 0, sizeof(ro));
    gDPSetTextureImage(&native[0], G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, (void *)0x05001000);
    gSPSetGeometryMode(&native[1], G_LIGHTING | G_TEXTURE_GEN);
    gSPVertex(&native[2], (void *)0x04000000, 3, 0);
    gSPEndDisplayList(&native[3]);
    ro.DisplayList.Primary = native; ro.DisplayList.BaseAddr = native;
    struct gevrEmitVert v[3] = {0};
    v[0].p[0] = -.5f; v[1].p[0] = 10; v[2].p[1] = 10;
    for (int i = 0; i < 3; i++) v[i].n[i] = 1;
    void *list = NULL, *verts = NULL;
    assert(emitChrome(v, 3, &node, &list, &verts));
    assert(!memcmp(list, native, 2 * sizeof(Gfx)));
    Vtx *nv = verts;
    assert(nv[0].v.ob[0] == -1);
    for (int i = 0; i < 3; i++) {
        assert(nv[i].n.n[i] == 127 && nv[i].n.a == 255);
        assert(!nv[i].v.tc[0] && !nv[i].v.tc[1]); /* generated at draw time */
    }
    free(list); free(verts);
    puts("Bean glass render passes and native chrome setup passed");

    /* A monitor's screen (DAB xblamesh.c xblaMeshNodeIsLiveScreen()): part 0
     * registered blank under Bean's mesh is drawn with the list its programme
     * wrote, at its own model type, while propobj.c names the monitor; Bean's
     * blank otherwise, and the native list with Bean off. */
    {
        Gfx programme[1];
        ModelNode *switches[1] = { &node };
        ModelFileHeader header = {0};
        gSPEndDisplayList(programme);
        node.Opcode = MODELNODE_OPCODE_DLCOLLISION;
        memset(&ro, 0, sizeof(ro));
        ro.DisplayListCollisions.Primary = native;
        ro.DisplayListCollisions.ModelType = 1;
        ro.DisplayListCollisions.RwDataIndex = 0;
        header.Switches = switches;
        header.numSwitches = 1;
        model.obj = &header;
        model.datas = (u32 *)&collisionRw;
        swapAdd(&node, native, NULL, 1); /* s_blank */
        for (int named = 0; named < 2; named++)
            for (int written = 0; written < 2; written++) {
                ModelRenderData r = {0}; r.gdl = output; r.flags = 1;
                collisionRw.gdl = written ? programme : native;
                gevrBeanSetScreens(named ? &model : NULL);
                modelRenderNodeDl(&r, &model, &node);
                gevrBeanSetScreens(NULL);
                int drew = 0;
                for (Gfx *g = output; g < r.gdl; g++)
                    if ((g->words.w0 >> 24) == (G_DL & 255))
                        drew = g->words.w1 == (uintptr_t)programme ? 1 : g->words.w1 == (uintptr_t)s_blank ? 2 : 3;
                assert(drew == (named && written ? 1 : 2));
            }
        gevrBeanSetEnabled(0);
        {
            ModelRenderData r = {0}; r.gdl = output; r.flags = 1;
            collisionRw.gdl = programme;
            modelRenderNodeDl(&r, &model, &node);
            assert(r.gdl > output && output[r.gdl - output - 1].words.w1 == (uintptr_t)programme);
        }
        gevrBeanSetEnabled(1);
        s_nswap = 0; swapIndexRebuild();
        model.obj = NULL; model.datas = NULL;
        puts("Bean monitor screens: a programme's list is the game's while its monitor is drawn");
    }
}
