/* Exercise the actual model dispatchers, not only the replacement registry. */
void modelApplyRenderModeType1(ModelRenderData *r) { (void)r; }
void modelApplyRenderModeType2(ModelRenderData *r) { (void)r; }
void modelApplyRenderModeType3(ModelRenderData *r, bool p) { (void)r; (void)p; }
void modelApplyRenderModeType4(ModelRenderData *r, bool p) { (void)r; (void)p; }
void modelApplyCullMode(ModelRenderData *r) { (void)r; }
/* vtxstore.c: objDeform's wreck copies (one at most here) */
static const void *s_wreckCopy;
s32 vtxstoreIsWreckCopy(const void *vertices) { return vertices && vertices == s_wreckCopy; }
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
            gevrBeanBeginFrame();   /* each pass on its own, as a prop drawn in that pass alone */
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
        /* the opaque pass then the translucent one in a frame: a list with translucent
           batches (glass) is drawn in both, as the walls behind it must not cover it; an
           opaque-only one once (a destroyed prop's scorched copy is not covered by a
           clean one) */
        /* ... split over the passes: the opaque pass its solid batches, the
           translucent one its translucent ones (GEVR_NODE_PASS 1 and 2, then 0) */
        for (int opaqueOnly = 0; opaqueOnly < 2; opaqueOnly++) {
            int draws = 0;
            swapOf(&node)->opaqueOnly = opaqueOnly;
            swapOf(&node)->emitted = secondary;
            gevrBeanBeginFrame();
            for (int flags = 1; flags <= 2; flags++) {
                ModelRenderData r = {0}; r.gdl = output; r.flags = flags;
                if (collision) modelRenderNodeDl(&r, &model, &node);
                else modelRenderNodeGundl(&r, &model, &node);
                int marks = 0, pass = -1;
                for (Gfx *g = output; g < r.gdl; g++) {
                    draws += (g->words.w0 >> 24) == (G_DL & 255);
                    if ((g->words.w0 >> 24) == G_GEVR_NODE_STATE && (g->words.w0 & 0xff) == GEVR_NODE_PASS) {
                        if (!marks++) pass = (int)g->words.w1;
                        else assert(g->words.w1 == 0);
                    }
                }
                assert(opaqueOnly ? !marks : marks == 2 && pass == flags);
            }
            assert(draws == (opaqueOnly ? 1 : 2));
        }
        swapOf(&node)->emitted = NULL;
        gevrBeanBeginFrame();
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

    /* A destroyed prop (objDeform's copy in the vertex store) whose Bean list
     * cannot follow it - here nothing was emitted to copy: the game's own
     * wreck, never the Bean prop whole (Statue's helicopter, 2026-10-27). The
     * opaque then the translucent pass in one frame: the second draws the
     * game's translucent part, not a clean Bean list over the wreck (the Dam
     * drums of 2026-10-14 again). */
    {
        Vertex wreck[1] = {0};
        Gfx glass[1];
        gSPEndDisplayList(native); gSPEndDisplayList(glass);
        node.Opcode = MODELNODE_OPCODE_DLCOLLISION;
        memset(&ro, 0, sizeof(ro));
        ro.DisplayListCollisions.Primary = native;
        ro.DisplayListCollisions.Secondary = glass;
        ro.DisplayListCollisions.ModelType = 4;
        ro.DisplayListCollisions.numVertices = 1;
        collisionRw.gdl = native;
        collisionRw.Vertices = wreck;
        swapAdd(&node, native, secondary, 4);
        for (int wrecked = 0; wrecked < 2; wrecked++) {
            s_wreckCopy = wrecked ? wreck : NULL;
            gevrBeanBeginFrame();
            for (int flags = 1; flags <= 2; flags++) {
                ModelRenderData r = {0}; r.gdl = output; r.flags = flags;
                modelRenderNodeDl(&r, &model, &node);
                int bean = 0, game = 0, pane = 0;
                for (Gfx *g = output; g < r.gdl; g++)
                    if ((g->words.w0 >> 24) == (G_DL & 255)) {
                        bean += g->words.w1 == (uintptr_t)secondary;
                        game += g->words.w1 == (uintptr_t)native;
                        pane += g->words.w1 == (uintptr_t)glass;
                    }
                if (flags == 1)
                    assert(wrecked ? game == 1 && !bean : bean == 1 && !game);
                else   /* whole, Bean's list (drawn once, in the opaque pass) owns the glass */
                    assert(wrecked ? pane == 1 && !bean : !pane && !game);
            }
        }
        /* a part Bean draws in another node's list (the CCTV mount) keeps its
         * blank: the game's wreck would lie over the whole Bean prop */
        s_nswap = 0; swapIndexRebuild();
        swapAdd(&node, native, NULL, 4);
        s_wreckCopy = wreck;
        gevrBeanBeginFrame();
        for (int flags = 1; flags <= 2; flags++) {
            ModelRenderData r = {0}; r.gdl = output; r.flags = flags;
            modelRenderNodeDl(&r, &model, &node);
            for (Gfx *g = output; g < r.gdl; g++)
                if ((g->words.w0 >> 24) == (G_DL & 255))
                    assert(g->words.w1 != (uintptr_t)native && g->words.w1 != (uintptr_t)glass);
        }
        s_wreckCopy = NULL;
        collisionRw.Vertices = NULL;
        s_nswap = 0; swapIndexRebuild();
        puts("Bean wrecks: a destroyed prop with no Bean copy draws the game's wreck");
    }
    /* The CCTV (v0.5.14-test.16): objDeform crumples its mount, the first
     * collision list, which Bean draws in the camera's list. The camera's list
     * shows the crumpling where its vertices lie on the mount (100 below, by
     * the two lists' rest offsets) and stays as it is on the camera. */
    {
        union ModelRoData hostRo = {0}, guestRo = {0};
        ModelNode host = {0}, guest = {0};
        Vertex hostStock[3] = {{{0, 0, 0}}, {{10, 0, 0}}, {{0, 10, 0}}};
        Vertex guestStock[3] = {{{0, 0, 0}}, {{10, 0, 0}}, {{0, 10, 0}}}, guestWreck[3];
        Gfx hostList[3], guestList[3], emitted[2], unused[1];
        Vtx bean[2] = {{{{1, 1, 0}, 0, {0, 0}, {200, 200, 200, 255}}},
                       {{{1, 1, -100}, 0, {0, 0}, {200, 200, 200, 255}}}};
        Gfx *lists[2] = {hostList, guestList};
        for (int l = 0; l < 2; l++) {
            lists[l][0].words.w0 = ((uintptr_t)(G_VTX & 255) << 24) | (3 * sizeof(Vertex));
            lists[l][0].words.w1 = (uintptr_t)SPSEGMENT_MODEL_VTX << 24;
            lists[l][1].words.w0 = (uintptr_t)(G_TRI1 & 255) << 24;
            lists[l][1].words.w1 = (0u << 16) | (10u << 8) | 20u;
            gSPEndDisplayList(&lists[l][2]);
        }
        gSPEndDisplayList(unused);
        for (int i = 0; i < 3; i++) {
            hostStock[i].r = hostStock[i].g = hostStock[i].b = 200;
            guestStock[i].r = guestStock[i].g = guestStock[i].b = 200;
            guestWreck[i] = guestStock[i];
            guestWreck[i].coord.y += 30;   /* pressed and pushed about */
            guestWreck[i].r = guestWreck[i].g = guestWreck[i].b = 0;   /* blackened */
        }
        host.Opcode = guest.Opcode = MODELNODE_OPCODE_DLCOLLISION;
        host.Data = &hostRo; guest.Data = &guestRo;
        hostRo.DisplayListCollisions.Primary = hostList;
        hostRo.DisplayListCollisions.Vertices = hostStock;
        hostRo.DisplayListCollisions.numVertices = 3;
        guestRo.DisplayListCollisions.Primary = guestList;
        guestRo.DisplayListCollisions.Vertices = guestStock;
        guestRo.DisplayListCollisions.numVertices = 3;
        emitted[0].words.w0 = ((uintptr_t)(G_VTX & 255) << 24) | (2 * sizeof(Vtx));
        emitted[0].words.w1 = (uintptr_t)bean;
        gSPEndDisplayList(&emitted[1]);
        swapAdd(&guest, guestList, NULL, 1);
        swapAdd(&host, hostList, unused, 1);
        struct beanSwap *hs = swapOf(&host);
        hs->emitted = emitted; hs->emittedVtx = bean; hs->emittedVerts = 2; hs->emittedCommands = 2;
        struct beanWreckSource sources[2] = {{&guest, {0, 0, -100}}, {&host, {0, 0, 0}}};
        hs->wreckGuest = swapOf(&guest);
        hs->wreckSources = sources;
        hs->nwreckSources = 2;
        guestRwNode = &guest;
        collisionRw.Vertices = hostStock;
        guestRw.Vertices = guestStock;
        assert(gevrBeanWreck(&host, &model) == 0 && !gevrBeanScorchedGdl(&host, &model));
        guestRw.Vertices = guestWreck;
        s_wreckCopy = guestWreck;
        assert(gevrBeanWreck(&host, &model) == 2 && gevrBeanWreck(&guest, &model) == 1);
        gevrBeanBeginFrame();
        Gfx *copy = gevrBeanScorchedGdl(&host, &model);
        assert(copy && copy[0].words.w1 != (uintptr_t)bean);
        const Vtx *drawn = (const Vtx *)copy[0].words.w1;
        assert(drawn[0].v.ob[1] == 1 && drawn[0].v.cn[0] == 200);   /* the camera: as it was */
        assert(drawn[1].v.ob[1] == 31 && drawn[1].v.cn[0] == 0);    /* the mount: crumpled, black */
        s_wreckCopy = NULL;
        guestRwNode = NULL;
        collisionRw.Vertices = NULL;
        guestRw.Vertices = NULL;
        s_nswap = 0; swapIndexRebuild();
        puts("Bean wrecks: a list Bean draws in another shows its crumpling there (the CCTV mount)");
    }
}
