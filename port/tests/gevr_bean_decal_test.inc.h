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
