/* A level's faces back to back (markLevelSides(), DAB writeLeaf()): a sheet's
 * face and a fighting face cull their backs (GEVR_ROOM_FACES); a fighting
 * face is drawn again, its back alone (GEVR_ROOM_BACKS), with the same
 * corners; a model list's sides are not read. */
static void roomSideChecks(void)
{
    struct gevrBeanMesh m = {.ntex = 1, .roomMaterial = 1};
    Vtx tile[16];
    m.standin[0] = tile;
    m.texW[0] = m.texH[0] = 16;
    struct gevrEmitVert v[9] = {0};
    for (int i = 0; i < 9; i++) {
        v[i].alpha = 255; memset(v[i].rgba, 255, 4);
        v[i].p[0] = (float)(i * 10); v[i].p[1] = (float)((i % 3) * 7); v[i].p[2] = (float)(i / 3);
    }
    for (int i = 0; i < 3; i++) v[i].side = GEVR_BEAN_SIDE_FIGHTS;
    for (int i = 3; i < 6; i++) v[i].side = GEVR_BEAN_SIDE_BACKED;
    void *dl, *raw;
    for (int room = 1; room >= 0; room--) {
        m.roomMaterial = room;
        assert(gevrBeanEmitList(&m, v, 9, 0, 0, 0, 1, &dl, &raw));
        int faces = 0, backs = 0, tris = 0, backTris = 0, inBacks = 0, cleared = 0, fightTris = 0;
        uint32_t faceMode = 0;
        const Vtx *loaded = NULL;
        int fight[2][3][3];
        for (Gfx *g = dl; (g->words.w0 >> 24) != (G_ENDDL & 255); g++) {
            unsigned op = g->words.w0 >> 24;
            if (op == G_GEVR_ROOM_STATE) {
                faces += (g->words.w0 & GEVR_ROOM_FACES) != 0;
                backs += (g->words.w0 & GEVR_ROOM_BACKS) != 0;
                inBacks = (g->words.w0 & GEVR_ROOM_BACKS) != 0;
                /* the back pass keeps its face's own mode */
                if (inBacks) assert(g->words.w1 == faceMode);
                else faceMode = (uint32_t)g->words.w1;
            }
            /* the cull a level's list leaves is cleared at its end */
            if (op == (G_CLEARGEOMETRYMODE & 255) && g->words.w1 == G_CULL_BOTH)
                cleared++;
            if (op == (G_VTX & 255))
                loaded = (const Vtx *)(uintptr_t)g->words.w1;
            if (op == (G_TRI1 & 255)) {
                const unsigned corner[3] = {(unsigned)(g->words.w1 >> 16 & 255) / 10,
                                            (unsigned)(g->words.w1 >> 8 & 255) / 10, (unsigned)(g->words.w1 & 255) / 10};
                const Vtx *t = &loaded[corner[0]];
                tris++;
                backTris += inBacks;
                /* the fighting triangle is the one at z 0 */
                if (t->v.ob[2] == 0 && fightTris < 2) {
                    for (int k = 0; k < 3; k++)
                        for (int c = 0; c < 3; c++)
                            fight[fightTris][k][c] = loaded[corner[k]].v.ob[c];
                    fightTris++;
                }
            }
        }
        if (room) {
            /* the sheet's and the fighting face's batches, then the fighting face's back */
            assert(faces == 2 && backs == 1 && tris == 4 && backTris == 1 && cleared == 1);
            assert(fightTris == 2 && !memcmp(fight[0], fight[1], sizeof(fight[0])));
        } else {
            assert(faces == 0 && backs == 0 && tris == 3 && cleared == 0);
        }
        free(dl); free(raw);
    }
    puts("Room sides: a sheet and a fighting face cull their backs; the fighting face's back drawn again, pushed");
}

static void roomMaterialChecks(void)
{
    roomSideChecks();
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
