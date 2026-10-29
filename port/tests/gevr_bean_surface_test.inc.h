/* Production sky preparation and emission, including the retained mesh path. */
#include "../src/gevr_bean_sky.inc.h"
static void surfaceChecks(void)
{
    struct gevrBeanTri tri[2] = {0};
    struct gevrBeanMesh mesh = {.tris=tri, .ntris=2, .ntex=2, .roomMaterial=2};
    mesh.standin[0]=(void *)1; mesh.standin[1]=(void *)2;
    mesh.texW[0]=mesh.texW[1]=mesh.texH[0]=mesh.texH[1]=256;
    tri[0].tex=0; tri[1].tex=1;
    float uv[6]={.5f,.5f, .9f,.5f, 1,.5f};
    memcpy(tri[1].uv,uv,sizeof(uv));
    tri[0].uv[0]=.9f; tri[0].uv[2]=1.1f; tri[0].uv[5]=1;
    beanSkyMaterials(&mesh);
    assert(tri[0].alpha==255 && tri[1].alpha<255 && !tri[1].alphatest);
    assert(tri[1].rgba[3]==255 && tri[1].rgba[7]>0 && tri[1].rgba[7]<255 && tri[1].rgba[11]==0);
    struct gevrEmitVert v[6]={0};
    for(int t=0;t<2;t++) for(int c=0;c<3;c++) {
        memcpy(v[t*3+c].uv,&tri[t].uv[c*2],2*sizeof(float));
        memcpy(v[t*3+c].rgba,&tri[t].rgba[c*4],4);
        v[t*3+c].tex=t; v[t*3+c].alpha=tri[t].alpha;
    }
    void *dl=NULL,*raw=NULL;
    assert(gevrBeanEmitList(&mesh,v,6,0,0,0,0,&dl,&raw));
    const Vtx *verts=raw;
    assert(verts[1].v.tc[0]>verts[0].v.tc[0]); /* dome U still crosses its seam */
    assert(verts[3].v.cn[3]==254 && verts[4].v.cn[3]>0 && verts[4].v.cn[3]<254 && !verts[5].v.cn[3]);
    int tiles=0, modes=0;
    for(Gfx *g=dl;(g->words.w0>>24)!=(G_ENDDL&255);g++) {
        if((g->words.w0>>24)==G_SETTILE && ((g->words.w1>>24)&7)==1) {
            int cap=tiles>=1;
            assert(((g->words.w1>>18)&3)==G_TX_CLAMP);
            assert(((g->words.w1>>8)&3)==(cap ? G_TX_CLAMP : G_TX_WRAP));
            tiles++;
        }
        if((g->words.w0>>24)==G_GEVR_ROOM_STATE) {
            assert(!(g->words.w1&(Z_CMP|Z_UPD)));
            if(modes++) assert(g->words.w1&FORCE_BL);
        }
    }
    assert(tiles==2 && modes==2);
    GevrStaticMesh *retained=gevrStaticMeshBuild(dl);
    assert(retained); gevrStaticMeshFree(retained);
    free(dl); free(raw);
    /* Retail caps with authored texture alpha stay the alpha-tested cut-out
     * they load as: no blend, no second fade (test.7: a white rim band). */
    mesh.texCut[1]=1; tri[1].alpha=255; tri[1].alphatest=1;
    memset(tri[1].rgba,255,sizeof(tri[1].rgba));
    beanSkyMaterials(&mesh);
    assert(tri[1].alpha==255 && tri[1].alphatest==1);
    for(int c=0;c<3;c++) assert(tri[1].rgba[c*4+3]==255);

    /* A forest wall repeats U, clamps V and shares one V origin even when
     * its neighbouring triangles start at opposite edges of the picture. */
    memset(v,0,sizeof(v)); mesh.roomMaterial=1;
    mesh.texClamp[0]=2; mesh.texClampBase[0][1]=-2;
    for(int i=0;i<6;i++) {
        v[i].tex=0; v[i].alpha=255; v[i].cut=1;
        memset(v[i].rgba,255,4);
        v[i].uv[0]=i%3*2;
        v[i].uv[1]=(i==0||i==4) ? -1 : -2;
    }
    assert(gevrBeanEmitList(&mesh,v,6,0,0,0,1,&dl,&raw));
    verts=raw;
    assert(verts[0].v.tc[1]==0 && verts[1].v.tc[1]==256*32);
    assert(verts[3].v.tc[1]==256*32 && verts[4].v.tc[1]==0);
    free(dl); free(raw);
    puts("Surface sky/forest: cap fade, authored alpha, clamp axes/shared UV origin and retained mesh passed");
}

/* The Runway body-binding regression needs weighted geometry: a listless
 * root must never steal the stand/body from its drawable position node. */
static void rigidSupportCase(int emptySupport, int collision)
{
    ModelNode root={0}, stand={0}, dish={0}, standDl={0}, dishDl={0};
    union ModelRoData rd={0}, sd={0}, dd={0}, sl={0}, dl={0};
    Vertex sv[3]={0}, dv[3]={0}; Gfx native[1]; gSPEndDisplayList(native);
    root.Opcode=MODELNODE_OPCODE_HEADER; root.Data=&rd; root.Child=&stand;
    rd.Header.MatrixIndex=emptySupport ? 1 : 0;
    stand.Opcode=dish.Opcode=MODELNODE_OPCODE_GROUP;
    stand.Data=&sd;stand.Parent=&root;stand.Next=&dish;stand.Child=&standDl;
    dish.Data=&dd;dish.Parent=&root;dish.Child=&dishDl;
    sd.Group.MatrixID0=1;dd.Group.MatrixID0=2;dd.Group.Origin.y=100;
    standDl.Opcode=collision ? MODELNODE_OPCODE_DLCOLLISION : MODELNODE_OPCODE_DL;
    dishDl.Opcode=MODELNODE_OPCODE_DLCOLLISION;
    standDl.Data=&sl;standDl.Parent=&stand;dishDl.Data=&dl;dishDl.Parent=&dish;
    sv[1].coord.x=1000;dv[1].coord.x=100;
    if(collision) {
        sl.DisplayListCollisions.Vertices=emptySupport ? NULL : sv;
        sl.DisplayListCollisions.numVertices=emptySupport ? 0 : 3;
        sl.DisplayListCollisions.Primary=emptySupport ? NULL : native;
    } else {
        sl.DisplayList.Vertices=emptySupport ? NULL : sv;
        sl.DisplayList.numVertices=emptySupport ? 0 : 3;
        sl.DisplayList.Primary=emptySupport ? NULL : native;
    }
    dl.DisplayListCollisions.Vertices=dv;dl.DisplayListCollisions.numVertices=3;dl.DisplayListCollisions.Primary=native;
    ModelFileHeader header={0};header.RootNode=&root;header.numMatrices=3;
    struct gevrBeanTri tris[2]={0};
    struct gevrBeanMesh mesh={.tris=tris,.ntris=2,.nbones=2};
    strcpy(mesh.boneName[0],"SKEL_TOP");strcpy(mesh.boneName[1],"SKEL_YROT");mesh.boneBind[1][1]=100;
    for(int t=0;t<2;t++) {
        tris[t].alpha=255;tris[t].tex=-1;tris[t].cuff=-1;
        tris[t].p[3]=10;tris[t].p[7]=10;
        for(int c=0;c<3;c++) {
            tris[t].p[c*3+1]+=t*100;tris[t].bone[c*4]=t;tris[t].weight[c*4]=255;
            memset(&tris[t].rgba[c*4],255,4);
        }
    }
    struct beanFit fit={.source="prop/sevdish",.scale=1,.perm={0,1,2},.sign={1,1,1}};
    /* An unfitted model still needs native vertices to determine its size. */
    if(emptySupport) {
        void *g=NULL,*v=NULL;
        assert(!emitRigid(&mesh,BEAN_PROP,0,&standDl,NULL,NULL,-1,NULL,-1,&g,&v));
    }
    applyMesh(&header,&mesh,BEAN_PROP,&fit);
    struct beanSwap *support=swapOf(&standDl), *moving=swapOf(&dishDl);
    assert(support && support->ours!=s_blank && moving && moving->ours!=s_blank);
    assert(support->emittedVerts==3 && moving->emittedVerts==3);
    assert(!support->emittedWorld && !moving->emittedWorld);
    const Vtx *s=support->emittedVtx,*d=moving->emittedVtx;
    assert(s[0].v.ob[1]==0 && d[0].v.ob[1]==0); /* each in its own part's frame */
    assert((collision ? sl.DisplayListCollisions.Primary : sl.DisplayList.Primary)==(emptySupport ? NULL : native));
    assert(dl.DisplayListCollisions.Primary==native);
    /* Execute the real model dispatcher with no native support vertices/list.
     * A fitted Bean support must still draw through either native node kind. */
    if(emptySupport) {
        int want=s_want; s_want=1;
        Model model={0}; model.obj=&header;
        Gfx output[64]; ModelRenderData render={0}; render.gdl=output;render.flags=1;
        collisionRw.gdl=NULL;
        gevrBeanBeginFrame();
        if(collision) modelRenderNodeDl(&render,&model,&standDl);
        else modelRenderNodeGundl(&render,&model,&standDl);
        int draws=0;
        for(Gfx *g=output;g<render.gdl;g++) if((g->words.w0>>24)==(G_DL&255)) {
            assert(g->words.w1==(uintptr_t)support->ours);draws++;
        }
        assert(draws==1);
        s_want=0;assert(gevrBeanShownGdl(&standDl,NULL)==NULL);s_want=want;
    }
    gevrBeanForgetModel(&header);
}

static void rigidSupportChecks(void)
{
    for(int empty=0;empty<2;empty++) for(int collision=0;collision<2;collision++)
        rigidSupportCase(empty,collision);
    puts("Rigid supports: listless roots, empty native lists, moving-part matrices and production node dispatch passed");
}
