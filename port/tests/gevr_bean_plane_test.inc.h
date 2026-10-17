/* A flying prop whose rotors precede its non-root hull, plus a distant hull. */
static void planeChecks(void)
{
    ModelNode root={0},rotor={0},hull={0},rotorDl={0},hullDl={0},farDl={0},lod={0};
    union ModelRoData rootData={0},rotorData={0},hullData={0},rotorList={0},hullList={0},farList={0},lodData={0};
    ModelFileHeader header={0};Gfx native[1];gSPEndDisplayList(native);
    Vertex rotorVertices[3]={0},hullVertices[3]={0},farVertices[3]={0};
    rotorVertices[1].coord.x=100;hullVertices[1].coord.x=1000;farVertices[1].coord.x=2000;
    root.Opcode=MODELNODE_OPCODE_HEADER;root.Data=&rootData;root.Child=&rotor;
    rootData.Header.MatrixIndex=0;
    rotor.Opcode=MODELNODE_OPCODE_GROUPSIMPLE;rotor.Data=&rotorData;rotor.Parent=&root;rotor.Next=&hull;rotor.Child=&rotorDl;
    rotorData.GroupSimple.Group1=1;
    hull.Opcode=MODELNODE_OPCODE_GROUP;hull.Data=&hullData;hull.Parent=&root;hull.Child=&hullDl;
    hullData.Group.MatrixID0=2;
    rotorDl.Opcode=hullDl.Opcode=farDl.Opcode=MODELNODE_OPCODE_DLCOLLISION;
    rotorDl.Data=&rotorList;hullDl.Data=&hullList;farDl.Data=&farList;
    rotorDl.Parent=&rotor;hullDl.Parent=&hull;farDl.Parent=&lod;
    hullDl.Next=&lod;lod.Parent=&hull;lod.Child=&farDl;lod.Opcode=MODELNODE_OPCODE_LOD;lod.Data=&lodData;
    lodData.LOD.MinDistance=100;lodData.LOD.Affects=&farDl;
    rotorList.DisplayListCollisions.Vertices=rotorVertices;rotorList.DisplayListCollisions.numVertices=3;rotorList.DisplayListCollisions.Primary=native;
    hullList.DisplayListCollisions.Vertices=hullVertices;hullList.DisplayListCollisions.numVertices=3;hullList.DisplayListCollisions.Primary=native;
    farList.DisplayListCollisions.Vertices=farVertices;farList.DisplayListCollisions.numVertices=3;farList.DisplayListCollisions.Primary=native;
    header.RootNode=&root;header.numMatrices=3;
    struct gevrBeanTri triangle={0};triangle.alpha=255;triangle.cuff=-1;triangle.tex=-1;
    triangle.p[3]=100;triangle.p[7]=100;
    for(int i=0;i<3;i++){triangle.weight[i*4]=255;triangle.n[i*3+2]=1;memset(&triangle.rgba[i*4],255,4);}
    struct gevrBeanMesh mesh={0};mesh.tris=&triangle;mesh.ntris=1;
    struct beanFit fit={0};fit.source="prop/plane";fit.scale=1;
    for(int i=0;i<3;i++){fit.perm[i]=i;fit.sign[i]=1;}
    applyMesh(&header,&mesh,BEAN_PROP,&fit);
    struct beanSwap *near=swapOf(&hullDl),*far=swapOf(&farDl);
    assert(near && near->ours!=s_blank);
    assert(far && far->ours!=s_blank);
    assert(((Gfx *)far->ours)[1].words.w1==((Gfx *)near->ours)[1].words.w1);
    assert(!swapOf(&rotorDl)); /* no replacement rotor geometry: keep native propellers */
    gevrBeanForgetModel(&header);
    puts("PASS: articulated prop hull uses its body matrix and survives distant LOD, native propellers retained");
}
