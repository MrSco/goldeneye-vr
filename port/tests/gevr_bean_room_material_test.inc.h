static void roomMaterialChecks(void)
{
    struct gevrBeanMesh m = {.ntex = 2, .roomMaterial = 1};
    Vtx tile[16];
    m.standin[0] = m.standin[1] = tile;
    m.texW[0] = m.texH[0] = m.texW[1] = m.texH[1] = 16;
    struct gevrEmitVert v[9] = {0};
    for (int i = 0; i < 9; i++) {
        v[i].alpha = 255; memset(v[i].rgba, 255, 4);
        v[i].p[0] = i; v[i].p[1] = i % 3;
    }
    /* Low texture index paints on a higher-index base. Include both a
     * cutout and the road's vertex-alpha layer, in reverse input order. */
    for (int i = 0; i < 3; i++) { v[i].decal = 1; v[i].cut = 1; }
    for (int i = 3; i < 6; i++) { v[i].decal = 1; v[i].alpha = 254; }
    for (int i = 6; i < 9; i++) v[i].tex = 1;
    void *dl, *raw;
    assert(gevrBeanEmitList(&m, v, 9, 0, 0, 0, 1, &dl, &raw));
    assert(((Vtx *)raw)[0].v.ob[0] == 6); /* Base precedes both paint layers. */
    int modes = 0, cut = 0, xlu = 0, cycle = 0;
    for (Gfx *g = dl; (g->words.w0 >> 24) != (G_ENDDL & 255); g++) {
        unsigned op = g->words.w0 >> 24;
        if (op == (G_SETOTHERMODE_H & 255) && ((g->words.w0 >> 8) & 255) == G_MDSFT_CYCLETYPE)
            cycle = g->words.w1 == G_CYC_2CYCLE;
        if (op != G_GEVR_ROOM_STATE) continue;
        uint32_t plain = gevrBeanRoomMode(g->words.w1, 0);
        uint32_t fog = gevrBeanRoomMode(g->words.w1, 1);
        assert((plain & 0xcccc0000) == G_RM_PASS);
        assert((fog & 0xcccc0000u) == 0xc8000000u); /* SDK fog blender bits. */
        assert((plain & 0xffff) == (fog & 0xffff)); /* Fog preserves depth/alpha. */
        if ((plain & ZMODE_DEC) == ZMODE_DEC) {
            if (plain & CVG_X_ALPHA) { cut++; assert(plain & Z_UPD); }
            else { xlu++; assert(!(plain & Z_UPD)); }
        }
        modes++;
    }
    assert(cycle && modes == 3 && cut == 1 && xlu == 1);
    free(dl); free(raw);
    /* The merged float-world path must put an opaque same-image paint layer
     * after its base and retain decal depth despite material/spatial batching. */
    m.worldVertices = m.spatialSort = 1;
    for (int i = 0; i < 6; i++) {
        v[i].tex=0; v[i].alpha=255; v[i].cut=0;
        v[i].decal=i<3;
        v[i].uv[0]=i<3 ? .25f : 0;
    }
    assert(gevrBeanEmitList(&m,v,6,0,0,0,1,&dl,&raw));
    const GevrWorldVertex *world=raw;
    assert(world[0].tc[0]==0 && world[3].tc[0]>0);
    modes=0;
    for (Gfx *g=dl;(g->words.w0>>24)!=(G_ENDDL&255);g++) {
        if ((g->words.w0>>24)!=G_GEVR_ROOM_STATE) continue;
        uint32_t mode=gevrBeanRoomMode(g->words.w1,0);
        assert((mode & Z_UPD) && (mode & Z_CMP));
        assert((mode & ZMODE_DEC)==(modes ? ZMODE_DEC : ZMODE_OPA));
        modes++;
    }
    assert(modes==2);
    free(dl); free(raw);
    puts("Room materials: base before paint, cutout/translucent decals, fog preserves depth and alpha passed");
}
