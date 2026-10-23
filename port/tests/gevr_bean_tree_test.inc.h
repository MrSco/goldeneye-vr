static void putFloat(uint8_t *p, float f)
{
    uint32_t u; memcpy(&u,&f,4); put32(p,u);
}
static void treeChecks(void)
{
    struct beanDraw draws[4]={{.vb=7,.inst=-1},{.vb=8,.inst=-1},{.vb=7,.inst=0},{.vb=9,.inst=-1}};
    markTreeBuffers(draws,4);
    assert(draws[0].tree && draws[2].tree && !draws[1].tree && !draws[3].tree);
    uint8_t gpu[8*28]={0};
    struct beanVb vb={.stride=20,.count=8};
    /* Two opposite branch cards keep their shared axis at zero. */
    const float p[8][3]={{1,0,0},{1,1,0},{5,0,0},{5,1,0},
                         {-5,0,0},{-5,1,0},{-1,0,0},{-1,1,0}};
    for(int i=0;i<8;i++) {
        for(int k=0;k<3;k++) putFloat(gpu+i*20+k*4,p[i][k]);
        put32(gpu+i*20+16,0xff123456);
    }
    struct beantree tree; beanTreeMeasure(gpu,&vb,&tree);
    assert(fabsf(tree.axis[0])<1e-6f);
    uint32_t idx[3]={0,1,2}; struct beanVtx v[3];
    for(int c=0;c<3;c++) assert(readTreeVtx(gpu,&vb,idx[c],&v[c]));
    beanTreeUvs(gpu,&vb,&tree,idx,v);
    assert(v[0].uv[0]==1 && v[1].uv[0]==1 && v[2].uv[0]==0);
    assert(v[0].uv[1]==1 && v[1].uv[1]==0 && v[2].uv[1]==1);
    idx[0]=4;idx[1]=6;idx[2]=7;
    for(int c=0;c<3;c++) assert(readTreeVtx(gpu,&vb,idx[c],&v[c]));
    beanTreeUvs(gpu,&vb,&tree,idx,v);
    assert(v[0].uv[0]==0 && v[1].uv[0]==1 && v[2].uv[0]==1);
    /* Trunks: tangent/binormal at +16/+20, colour at +24. The old rigid
     * reader interpreted tangent as UVs and binormal as purple tint. */
    vb.stride=28; vb.count=3; memset(gpu,0,sizeof(gpu));
    const float bark[3][3]={{-1,0,.01f},{-1,0,-.01f},{0,10,0}};
    for(int i=0;i<3;i++) {
        for(int k=0;k<3;k++) putFloat(gpu+i*28+k*4,bark[i][k]);
        put32(gpu+i*28+16,0x12345678); put32(gpu+i*28+20,0xff00ff00);
        put32(gpu+i*28+24,0xff123456);
        assert(readTreeVtx(gpu,&vb,i,&v[i]) && v[i].argb==0xff563412);
    }
    tree.axis[0]=tree.axis[1]=0; tree.ymin=0;tree.radius=1;
    idx[0]=0;idx[1]=1;idx[2]=2; beanTreeUvs(gpu,&vb,&tree,idx,v);
    assert(fabsf(v[0].uv[0]-v[1].uv[0])<.01f);
    assert(fabsf(v[2].uv[0]-(v[0].uv[0]+v[1].uv[0])*.5f)<1e-6f);
    assert(v[2].uv[1]<0);
    float matrix[16]={0,0,-1,0, 0,1,0,0, 1,0,0,0, 100,20,300,1};
    float before[2]; memcpy(before,v[0].uv,sizeof(before));
    applyInst(v[0].pos,matrix); assert(!memcmp(before,v[0].uv,sizeof(before)));

    struct gevrBeanTri tris[2]={0};
    struct gevrBeanMesh m={.tris=tris,.ntris=2,.ntex=2}; uint8_t alpha[MAX_TEX]={1,1};
    tris[0].tex=0;tris[1].tex=1;
    tris[0].uv[1]=tris[0].uv[3]=-2;tris[0].uv[5]=-1.001f;
    tris[1].uv[1]=0;tris[1].uv[3]=tris[1].uv[5]=3;
    clampLevelCutouts(&m,alpha);
    assert(m.texClamp[0]==2 && m.texClampBase[0][1]==-2 && !m.texClamp[1]);
    puts("Pines: first/instanced copies, both card orders, bark colour/seam/tip, repeated texture boundaries passed");
}
