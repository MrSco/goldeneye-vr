/* Production door transforms, collider adapter and Jolt, with synthetic props. */
static PropRecord moverProp;
static DoorRecord moverDoor;
static ObjectRecord moverPlatform;
static collision_data moverCollision;
static Model moverModel;
static ModelFileHeader moverHeader;
static ModelNode moverRoot,moverBoxNode;
static union ModelRoData moverBox;
static BoundPadRecord moverPad;

static void moverInit(int door,struct bbox bounds)
{
    rdMoversReset();
    memset(&moverProp,0,sizeof(moverProp));memset(&moverDoor,0,sizeof(moverDoor));
    memset(&moverPlatform,0,sizeof(moverPlatform));memset(&moverCollision,0,sizeof(moverCollision));
    memset(&moverPad,0,sizeof(moverPad));memset(&moverModel,0,sizeof(moverModel));
    memset(&moverRoot,0,sizeof(moverRoot));memset(&moverBoxNode,0,sizeof(moverBoxNode));
    moverHeader.RootNode=&moverRoot;moverRoot.Child=&moverBoxNode;
    moverBoxNode.Opcode=MODELNODE_OPCODE_BBOX;moverBoxNode.Data=&moverBox;
    moverBox.BoundingBox.Bounds=bounds;moverModel.obj=&moverHeader;
    moverProp.type=door?PROP_TYPE_DOOR:PROP_TYPE_OBJ;moverProp.flags=PROPFLAG_ENABLED;
    moverProp.obj=door?(ObjectRecord *)&moverDoor:&moverPlatform;
    moverProp.obj->prop=&moverProp;moverProp.obj->model=&moverModel;
    moverProp.obj->flags=PROPFLAG_00000100;moverProp.obj->mtx=identity();
    moverProp.obj->ptr_allocated_collisiondata_block=&moverCollision;
    memset(moverProp.rooms,255,sizeof(moverProp.rooms));moverProp.rooms[0]=1;
    moverCollision.edges=4;moverDoor.bbox=moverBox.BoundingBox;
    moverDoor.maxFrac=moverDoor.perimFrac=1;moverPad.up.y=1;moverPad.look.z=1;
    g_CurrentSetup.boundpads=&moverPad;
    activeProps=&moverProp;
}

static void moverSphere(RdBody *b,float x,float y,float z,int pieces)
{
    memset(b,0,sizeof(*b));b->count=pieces;b->used=1;
    pdphysRagdollPart parts[2]={0};
    for(int i=0;i<pieces;i++) {
        parts[i].parent=-1;parts[i].shape=PDPHYS_SHAPE_SPHERE;parts[i].radius=10;parts[i].mass=5;
        Mtxf joint=identity();joint.m[3][0]=x+i*500;joint.m[3][1]=y;joint.m[3][2]=z;
        memcpy(parts[i].jointmtx,&joint,sizeof(joint));memcpy(parts[i].a,joint.m[3],3*sizeof(float));
        b->radius[i]=10;b->parent[i]=i?0:-1;
    }
    if(pieces>1)b->cuts=2;
    b->handle=pdphysRagdollCreate(parts,pieces);assert(b->handle>=0);
    int room=1;pdphysRagdollSetRooms(b->handle,&room,1);
}
static void moverSteps(RdBody *b,int count,int ticks,float dt)
{
    for(int i=0;i<ticks;i++){rdMoversUpdate(dt,b,count);pdphysStep(dt);}
}
static void moverFree(RdBody *b,int count)
{
    rdMoversReset();activeProps=NULL;
    for(int i=0;i<count;i++)pdphysRagdollDestroy(b[i].handle);
}
static void moverPosition(RdBody *b,int part,float p[3]) { pdphysRagdollGetPartPos(b->handle,part,p); }

static void movingCollisionChecks(void)
{
    assert(pdphysInit(rdLog));rdWorldInit();
    RdBody *b=calloc(48,sizeof(*b));float p[3],other[3],hit[3];
    const float dt=1.0f/60;
    /* A sliding slab pushes a resting body; the endpoint stops exactly. */
    moverInit(1,(struct bbox){.xmin=-10,.xmax=10,.ymin=0,.ymax=200,.zmin=-80,.zmax=80});
    moverDoor.frac=100;moverSphere(b,50,10,0,1);moverSteps(b,1,120,dt);
    for(int i=1;i<=80;i++){moverDoor.openPosition=i/100.0f;moverSteps(b,1,1,dt);}
    moverPosition(b,0,p);assert(p[0]>95);
    moverSteps(b,1,90,dt);
    float from[3]={0,150,0},to[3]={200,150,0};
    assert(pdphysRaycastAnyRoom(from,to,hit,NULL,NULL));assert(fabsf(hit[0]-70)<0.2f);
    /* Current shrinking bounds follow the clipping geometry, not a closed slab. */
    moverDoor.doorFlags=DOORFLAG_CLIP_TO_BBOX;moverDoor.openPosition=0.5f;
    moverDoor.bbox.Bounds.xmin=0; /* half of this 20-unit slab remains */
    moverSteps(b,1,1,dt);
    assert(pdphysRaycastAnyRoom(from,to,hit,NULL,NULL));assert(fabsf(hit[0]-50)<0.2f);
    moverCollision.edges=0;moverSteps(b,1,1,dt);
    assert(!pdphysRaycastAnyRoom(from,to,hit,NULL,NULL));
    moverFree(b,1);

    /* Articulated Caverns doors use the native ajar collision height. */
    for(int type=DOORTYPE_EYE;type<=DOORTYPE_IRIS;type++) {
        moverInit(1,(struct bbox){.xmin=-80,.xmax=80,.ymin=0,.ymax=200,.zmin=-5,.zmax=5});
        moverDoor.doorType=type;moverCollision.bottom=0;moverCollision.top=50;
        const float xz[4][2]={{-80,-5},{80,-5},{80,5},{-80,5}};
        for(int i=0;i<4;i++)memcpy(moverCollision.polygon[i].f,xz[i],sizeof(xz[i]));
        moverSphere(b,0,10,-100,1);moverSteps(b,1,1,dt);
        from[0]=to[0]=0;from[1]=to[1]=100;from[2]=-50;to[2]=50;
        assert(!pdphysRaycastAnyRoom(from,to,hit,NULL,NULL));
        from[1]=to[1]=25;assert(pdphysRaycastAnyRoom(from,to,hit,NULL,NULL));
        assert(fabsf(hit[2]+5)<0.2f);moverFree(b,1);
    }

    /* Hinge motion uses the game's real pivot and pushes instead of rebuilding. */
    moverInit(1,(struct bbox){.xmin=0,.xmax=120,.ymin=0,.ymax=150,.zmin=-5,.zmax=5});
    moverDoor.doorType=DOORTYPE_SWINGING;moverPad.bbox.xmin=0;
    moverSphere(b,70,10,-35,1);moverSteps(b,1,150,dt);
    for(int i=1;i<=90;i++){moverDoor.openPosition=i;moverSteps(b,1,1,dt);}
    moverPosition(b,0,p);assert(hypotf(p[0]-70,p[2]+35)>20);
    from[0]=-50;from[1]=100;from[2]=-60;to[0]=50;to[1]=100;to[2]=-60;
    assert(pdphysRaycastAnyRoom(from,to,hit,NULL,NULL));assert(fabsf(hit[0]+5)<0.2f);
    /* 180-degree quaternion branch and reflected/nonuniform native scale. */
    moverDoor.openPosition=180;moverDoor.mtx.m[0][0]=2;moverDoor.mtx.m[2][2]=-3;
    moverSteps(b,1,1,dt);moverSteps(b,1,60,dt);
    moverPosition(b,0,p);assert(fabsf(p[0])<1000 && fabsf(p[2])<1000);
    from[0]=-100;from[1]=100;from[2]=-50;to[0]=-100;to[1]=100;to[2]=50;
    assert(pdphysRaycastAnyRoom(from,to,hit,NULL,NULL));assert(fabsf(hit[2]+15)<0.2f);
    moverFree(b,1);

    /* Reusing a prop slot with another object discards the old shape. */
    moverInit(0,(struct bbox){.xmin=-20,.xmax=20,.ymin=0,.ymax=100,.zmin=-20,.zmax=20});
    moverSphere(b,0,10,-100,1);moverSteps(b,1,1,dt);
    ObjectRecord replacement=moverPlatform;replacement.runtime_pos.x=100;moverProp.obj=&replacement;
    moverSteps(b,1,1,dt);
    from[0]=to[0]=0;from[1]=to[1]=50;from[2]=-50;to[2]=50;
    assert(!pdphysRaycastAnyRoom(from,to,hit,NULL,NULL));
    from[0]=to[0]=100;assert(pdphysRaycastAnyRoom(from,to,hit,NULL,NULL));
    activeProps=NULL;moverSteps(b,1,1,dt);assert(!pdphysRaycastAnyRoom(from,to,hit,NULL,NULL));
    moverFree(b,1);

    /* A settled body rides ascent, descent, a horizontal trip and stops.
     * Its detached piece off the platform receives no platform impulse. */
    moverInit(0,(struct bbox){.xmin=-180,.xmax=180,.ymin=-10,.ymax=0,.zmin=-180,.zmax=180});
    moverPlatform.runtime_pos.y=200;moverSphere(b,0,210,0,2);moverSteps(b,1,300,dt);
    assert(pdphysRagdollIsAsleep(b->handle));
    for(int leg=0;leg<3;leg++) {
        for(int i=0;i<120;i++) {
            if(leg==0)moverPlatform.runtime_pos.y+=1;
            if(leg==1)moverPlatform.runtime_pos.y-=1;
            if(leg==2)moverPlatform.runtime_pos.x+=1;
            moverSteps(b,1,1,dt);moverPosition(b,0,p);
            assert(fabsf(p[1]-moverPlatform.runtime_pos.y-10)<3);
            assert(fabsf(p[0]-moverPlatform.runtime_pos.x)<3);
        }
        moverSteps(b,1,90,dt);
    }
    moverPosition(b,1,other);assert(fabsf(other[0]-500)<1 && fabsf(other[1]-10)<2);
    /* Pause cannot advance a collider or change its velocity. */
    moverPosition(b,0,p);moverPlatform.runtime_pos.y+=50;rdMoversUpdate(0,b,1);
    moverPosition(b,0,other);assert(!memcmp(p,other,sizeof(p)));moverPlatform.runtime_pos.y-=50;
    /* Disable/remove the support: a sleeping rider must fall. */
    moverSteps(b,1,240,dt);assert(pdphysRagdollIsAsleep(b->handle));
    moverProp.flags=0;moverSteps(b,1,100,dt);moverPosition(b,0,p);assert(fabsf(p[1]-10)<2);
    moverFree(b,1);

    /* Props in an overlapping but unrelated room cannot catch the body. */
    moverInit(0,(struct bbox){.xmin=-180,.xmax=180,.ymin=-10,.ymax=0,.zmin=-180,.zmax=180});
    moverProp.rooms[0]=2;moverPlatform.runtime_pos.y=200;moverSphere(b,0,260,0,1);
    moverSteps(b,1,180,dt);moverPosition(b,0,p);assert(fabsf(p[1]-10)<2);
    moverFree(b,1);

    /* The configured maximum rides together, including multi-tick frames. */
    moverInit(0,(struct bbox){.xmin=-180,.xmax=180,.ymin=-10,.ymax=0,.zmin=-180,.zmax=180});
    moverPlatform.runtime_pos.y=200;
    for(int i=0;i<48;i++)moverSphere(b+i,(i%8-3.5f)*30,210,(i/8-2.5f)*30,1);
    moverSteps(b,48,200,dt);
    for(int leg=0;leg<3;leg++)for(int tick=0;tick<30;tick++) {
        float step=leg==2?6:2,frameDt=leg==2?0.1f:1.0f/30;
        moverPlatform.runtime_pos.y+=leg==1?-step:step;
        moverSteps(b,48,1,frameDt);
        for(int i=0;i<48;i++){moverPosition(b+i,0,p);assert(fabsf(p[1]-moverPlatform.runtime_pos.y-10)<3);}
    }
    moverFree(b,48);free(b);pdphysShutdown();
    puts("PASS: sliding/clipped/hinged doors, reflected scale, lift ascent/descent/horizontal/stops, sleeping riders, detached pieces, pause, support removal, room filtering and 48 riders at 60/30/10 Hz");
}

static void guardLiftChecks(void)
{
    moverInit(0,(struct bbox){.xmin=-180,.xmax=180,.ymin=-10,.ymax=0,.zmin=-180,.zmax=180});
    moverPlatform.runtime_pos.y=200;
    for(int i=0;i<16;i++)sourceMatrices[i].pos.m[3][1]+=200;
    sourceProp.pos.y+=200;sourceChr.actiontype=ACT_DIE;VrRagdolls=1;VrBodiesStay=48;
    gevrRagdollCapture(&sourceChr);RdBody *body=newest();assert(body && body->used);
    for(int i=0;i<300;i++)gevrRagdollTick(1);
    assert(rdCut(body,3));
    for(int i=0;i<150;i++)gevrRagdollTick(1);
    for(int leg=0;leg<3;leg++) {
        float start[2][3];moverPosition(body,0,start[0]);moverPosition(body,3,start[1]);
        float floor=moverPlatform.runtime_pos.y,x=moverPlatform.runtime_pos.x;
        for(int tick=0;tick<90;tick++) {
            if(leg<2)moverPlatform.runtime_pos.y+=leg==0?2:-2;
            else moverPlatform.runtime_pos.x+=2;
            gevrRagdollTick(1);
            for(int part=0;part<2;part++) {
                float pos[3];moverPosition(body,part?3:0,pos);
                assert(fabsf((pos[1]-moverPlatform.runtime_pos.y)-(start[part][1]-floor))<10);
                assert(fabsf((pos[0]-moverPlatform.runtime_pos.x)-(start[part][0]-x))<12);
            }
        }
        for(int i=0;i<90;i++)gevrRagdollTick(1);
    }
    gevrRagdollReset();activeProps=NULL;VrRagdolls=0;
    for(int i=0;i<16;i++)sourceMatrices[i].pos.m[3][1]-=200;
    sourceProp.pos.y-=200;sourceChr.actiontype=ACT_STAND;
    puts("PASS: captured 15-part guard and severed arm ride lift ascent, descent, horizontal motion and stops through the game-clock adapter");
}
