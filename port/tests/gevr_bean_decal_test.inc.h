/* Coincident room faces may share an image but sample/paint it differently.
 * Keep both faces; the later paint must use decal depth, not fight its base. */
static void levelSameTextureDecalChecks(void)
{
    struct gevrBeanTri tris[2] = {0};
    struct gevrBeanMesh mesh = {.tris=tris,.ntris=2,.ntex=1};
    uint8_t texAlpha[MAX_TEX] = {0}, pass[2] = {0};
    const float p[9] = {0,0,0, 100,0,0, 0,100,0};
    const float uv[6] = {0,0, 1,0, 0,1};
    memcpy(tris[0].p,p,sizeof(p)); memcpy(tris[0].uv,uv,sizeof(uv));
    memset(tris[0].rgba,255,sizeof(tris[0].rgba)); tris[0].alpha=255;
    tris[1]=tris[0];

    /* Different UV phase, same image and average shade: the old classifier
     * left both draws opaque, unlike a layer using a different image. */
    for (int v=0;v<3;v++) tris[1].uv[v*2]+=0.25f;
    markLevelDecals(&mesh,pass,2,texAlpha,0.531318270f); /* Facility scale */
    assert(!(tris[0].flags & GEVR_BEAN_F_DECAL));
    assert(tris[1].flags & GEVR_BEAN_F_DECAL);

    /* Equal corner UVs modulo one repeat can still change the interior:
     * this layer repeats twice across the triangle. */
    tris[1]=tris[0]; tris[1].uv[2]=2;
    markLevelDecals(&mesh,pass,2,texAlpha,1);
    assert(tris[1].flags & GEVR_BEAN_F_DECAL);

    /* A cyclic corner order and a whole-image translation are equivalent. */
    for (int v=0;v<3;v++) {
        int src=(v+1)%3;
        memcpy(tris[1].p+v*3,tris[0].p+src*3,3*sizeof(float));
        memcpy(tris[1].uv+v*2,tris[0].uv+src*2,2*sizeof(float));
        tris[1].uv[v*2]+=2; tris[1].uv[v*2+1]-=1;
    }
    markLevelDecals(&mesh,pass,2,texAlpha,1);
    assert(!(tris[0].flags & GEVR_BEAN_F_DECAL) && !(tris[1].flags & GEVR_BEAN_F_DECAL));

    /* Untextured faces do not acquire layers from unused UV attributes. */
    tris[1]=tris[0]; tris[0].tex=tris[1].tex=-1; tris[1].uv[0]+=0.25f;
    markLevelDecals(&mesh,pass,2,texAlpha,1);
    assert(!(tris[0].flags & GEVR_BEAN_F_DECAL) && !(tris[1].flags & GEVR_BEAN_F_DECAL));
    tris[0].tex=0;

    /* Equal brightness can still be a different colour. */
    tris[1]=tris[0];
    for (int v=0;v<3;v++) {
        tris[0].rgba[v*4]=100; tris[0].rgba[v*4+1]=200;
        tris[1].rgba[v*4]=200; tris[1].rgba[v*4+1]=100;
    }
    markLevelDecals(&mesh,pass,2,texAlpha,1);
    assert(!(tris[0].flags & GEVR_BEAN_F_DECAL) && (tris[1].flags & GEVR_BEAN_F_DECAL));

    /* Adjacent triangles sharing only an edge are ordinary opaque geometry. */
    tris[1]=tris[0];
    const float adjacent[9] = {100,0,0, 100,100,0, 0,100,0};
    memcpy(tris[1].p,adjacent,sizeof(adjacent));
    for (int v=0;v<3;v++) tris[1].uv[v*2]+=0.25f;
    markLevelDecals(&mesh,pass,2,texAlpha,1);
    assert(!(tris[0].flags & GEVR_BEAN_F_DECAL) && !(tris[1].flags & GEVR_BEAN_F_DECAL));

    /* Opposite-winding sheets retain their authored front/back surfaces. */
    tris[1]=tris[0];
    memcpy(tris[1].p+3,tris[0].p+6,3*sizeof(float));
    memcpy(tris[1].p+6,tris[0].p+3,3*sizeof(float));
    tris[1].uv[0]+=0.25f;
    markLevelDecals(&mesh,pass,2,texAlpha,1);
    assert(!(tris[0].flags & GEVR_BEAN_F_DECAL) && !(tris[1].flags & GEVR_BEAN_F_DECAL));
    puts("Bean coincident same-image UV/tint layers use decal depth; equivalent, adjacent and opposite faces are preserved");
}

/* Archives' bulletin board (DAB markDecals() painted): a blended cork drawn,
 * corner for corner on its wall, after blended papers lying on it. The
 * release's blended pass writes no depth and shows the board bare; kept,
 * the papers fought the cork as two decals pulled the same reach. */
static void levelPaintTri(struct gevrBeanTri *t, const float *p, int tex)
{
    memset(t,0,sizeof(*t));
    memcpy(t->p,p,9*sizeof(float));
    memset(t->rgba,255,sizeof(t->rgba));
    t->tex=(int16_t)tex; t->alpha=255;
}

static int levelPaintRun(struct gevrBeanTri *tris, int n, const uint8_t *pass, const uint8_t *texAlpha, int *painted)
{
    struct gevrBeanMesh mesh = {.tris=tris,.ntris=n,.ntex=3};
    markLevelDecals(&mesh,pass,n,texAlpha,1);
    *painted=fixPlainLevelFaces(&mesh);
    return mesh.ntris;
}

static void levelPaintedChecks(void)
{
    static const float wallA[9] = {0,0,0, 100,0,0, 100,100,0};
    static const float wallB[9] = {0,0,0, 100,100,0, 0,100,0};
    static const float paper[9] = {20,20,0.25f, 60,20,0.25f, 20,60,0.25f};
    struct gevrBeanTri tris[5];
    uint8_t texAlpha[MAX_TEX] = {0};
    int painted;
    /* wall (opaque), paper (blended), then the cork twin of the wall (blended) */
    uint8_t pass[5] = {0, 0, LDECAL_PASS_BLEND, LDECAL_PASS_BLEND, LDECAL_PASS_BLEND};
#define BOARD() do { levelPaintTri(&tris[0],wallA,0); levelPaintTri(&tris[1],wallB,0); \
        levelPaintTri(&tris[2],paper,1); levelPaintTri(&tris[3],wallA,2); levelPaintTri(&tris[4],wallB,2); } while (0)

    BOARD();
    assert(levelPaintRun(tris,5,pass,texAlpha,&painted)==4 && painted==1);
    for (int i=0;i<4;i++) assert(tris[i].tex!=1);
    /* the cork still lies on its wall as a decal */
    assert((tris[2].flags & GEVR_BEAN_F_DECAL) && (tris[3].flags & GEVR_BEAN_F_DECAL));
    assert(!(tris[0].flags & GEVR_BEAN_F_DECAL) && !(tris[1].flags & GEVR_BEAN_F_DECAL));

    /* A cork with alpha in its picture shows the paper through it. */
    BOARD(); texAlpha[2]=1;
    assert(levelPaintRun(tris,5,pass,texAlpha,&painted)==5 && painted==0);
    texAlpha[2]=0;

    /* ... or with alpha in one of its vertices. */
    BOARD(); tris[3].rgba[7]=200;
    assert(levelPaintRun(tris,5,pass,texAlpha,&painted)==5 && painted==0);

    /* A black cover is a cast shadow, drawn see-through (markLevelShadows()). */
    BOARD();
    for (int t=3;t<5;t++) for (int v=0;v<3;v++) memset(tris[t].rgba+v*4,0,3);
    assert(levelPaintRun(tris,5,pass,texAlpha,&painted)==5 && painted==0);

    /* An opaque-pass paper is under no blended paint. */
    BOARD(); pass[2]=0;
    assert(levelPaintRun(tris,5,pass,texAlpha,&painted)==5 && painted==0);
    pass[2]=LDECAL_PASS_BLEND;

    /* A cover drawn in the opaque pass (a twin) comes before the blended paper. */
    BOARD(); pass[3]=pass[4]=LDECAL_PASS_BLEND|LDECAL_PASS_TWIN;
    assert(levelPaintRun(tris,5,pass,texAlpha,&painted)==5 && painted==0);
    pass[3]=pass[4]=LDECAL_PASS_BLEND;

    /* Drawn after the cork, the paper is painted on it. */
    BOARD();
    { struct gevrBeanTri p=tris[2]; tris[2]=tris[3]; tris[3]=tris[4]; tris[4]=p; }
    assert(levelPaintRun(tris,5,pass,texAlpha,&painted)==5 && painted==0);

    /* A paper hanging off the cork's edge onto the bare wall stays. */
    {
        static const float corkA[9] = {0,0,0, 50,0,0, 50,100,0};
        static const float corkB[9] = {0,0,0, 50,100,0, 0,100,0};
        static const float over[9] = {20,20,0.25f, 80,20,0.25f, 20,60,0.25f};
        BOARD(); levelPaintTri(&tris[2],over,1); levelPaintTri(&tris[3],corkA,2); levelPaintTri(&tris[4],corkB,2);
        assert(levelPaintRun(tris,5,pass,texAlpha,&painted)==5 && painted==0);
    }

    /* A cork facing away from the paper (the wall's back) hides nothing. */
    BOARD();
    for (int t=3;t<5;t++) {
        float c[3]; memcpy(c,tris[t].p+3,sizeof(c));
        memcpy(tris[t].p+3,tris[t].p+6,sizeof(c)); memcpy(tris[t].p+6,c,sizeof(c));
    }
    assert(levelPaintRun(tris,5,pass,texAlpha,&painted)==5 && painted==0);
#undef BOARD
    puts("Bean blended faces a later opaque blended face paints over wholly are left out (Archives' bulletin board)");
}
