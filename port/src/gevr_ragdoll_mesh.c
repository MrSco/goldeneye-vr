/* Per-snapshot native seams, following CE's per-instance display-list strategy.
 * Shared model definitions and living characters are never patched. */
#include "gevr_ragdoll_internal.h"
#include "model.h"
#include "dyn.h"

static Gfx *rdResolveGdl(ModelRoData_DisplayList_CollisionRecord *ro,Gfx *g)
{
    uintptr_t p=(uintptr_t)g;
    return p && p<0x10000000u ? (Gfx *)((u8 *)ro->BaseAddr+(p&0xffffff)) : g;
}

int rdCopyMeshes(RdBody *rd)
{
    int visits=0;
    for(ModelNode *n=rd->model.obj->RootNode;n;n=rdNext(&rd->model,n)) {
        if(++visits>RD_NODES)return 0;
        if((n->Opcode&255)!=MODELNODE_OPCODE_DLCOLLISION)continue;
        ModelRoData_DisplayList_CollisionRecord *ro=&n->Data->DisplayListCollisions;
        ModelRwData_DisplayList_CollisionRecord *rw=&modelGetNodeRwData(&rd->model,n)->DisplayListCollisions;
        if(!rw->gdl || !rw->Vertices || ro->numVertices<=0)continue;
        if(rd->nmeshes==64)return 0;
        Gfx *src=rdResolveGdl(ro,rw->gdl);
        int count=0;
        for(;count<8192;count++)if(((src[count].words.w0>>24)&255)==(G_ENDDL&255)){count++;break;}
        if(count>=8192)return 0;
        RdMesh *m=&rd->meshes[rd->nmeshes++];
        m->node=n; m->rw=rw; m->ncommands=count; m->nvertices=ro->numVertices;
        m->owner=modelFindNodeMtxIndex(n,0);
        if(m->owner<0 || m->owner>=rd->matrices)return 0;
        m->commands=rdAlloc(rd,(size_t)count*sizeof(Gfx));
        m->originalCommands=rdAlloc(rd,(size_t)count*sizeof(Gfx));
        m->original=rdAlloc(rd,(size_t)m->nvertices*sizeof(Vertex));
        Vertex *vertices=rdAlloc(rd,(size_t)m->nvertices*sizeof(Vertex));
        if(!m->commands || !m->originalCommands || !m->original || !vertices)return 0;
        memcpy(m->commands,src,(size_t)count*sizeof(Gfx));
        memcpy(m->originalCommands,src,(size_t)count*sizeof(Gfx));
        memcpy(m->original,rw->Vertices,(size_t)m->nvertices*sizeof(Vertex));
        memcpy(vertices,m->original,(size_t)m->nvertices*sizeof(Vertex));
        rw->gdl=m->commands; rw->Vertices=vertices;
    }
    return 1;
}

static void rdPatch(RdBody *rd,RdMesh *m)
{
    memcpy(m->commands,m->originalCommands,(size_t)m->ncommands*sizeof(Gfx));
    memcpy(m->rw->Vertices,m->original,(size_t)m->nvertices*sizeof(Vertex));
    int matrix=m->owner;
    for(int i=0;i<m->ncommands;i++) {
        Gfx *g=&m->commands[i]; unsigned op=(g->words.w0>>24)&255;
        if(op==(G_MTX&255)) {
            matrix=(g->words.w1&0xffffff)/sizeof(Mtx);
            if(matrix>=rd->matrices)continue;
            if(rdPiece(rd,rd->owner[matrix])!=rdPiece(rd,rd->owner[m->owner]))
                g->words.w1=(g->words.w1&~(uintptr_t)0xffffff)|(m->owner*sizeof(Mtx));
        } else if(op==(G_VTX&255) && matrix>=0 && matrix<rd->matrices) {
            int first=(g->words.w1&0xffffff)/sizeof(Vertex);
            int n=((g->words.w0>>20)&15)+1;
            for(int j=first;j<first+n && j<m->nvertices;j++) {
                if(j<0)continue;
                float p[3],normal[3]={0};
                for(int k=0;k<3;k++)p[k]=m->original[j].coord.AsArray[k];
                gevrRagdollRemap(&rd->model,m->owner,matrix,p,normal);
                for(int k=0;k<3;k++)m->rw->Vertices[j].coord.AsArray[k]=(s16)fmaxf(-32768,fminf(32767,roundf(p[k])));
            }
        }
    }
}

int rdCut(RdBody *rd,int part)
{
    if(part<0 || part>=rd->count || rd->parent[part]<0 || rd->joint[part]<4 || (rd->cuts&(1u<<part)))return 0;
    /* A nested list could still load the other piece's matrices. Leave that
     * model joined rather than allow a cut with an unpatched seam. */
    for(int i=0;i<rd->nmeshes;i++)for(int j=0;j<rd->meshes[i].ncommands;j++)
        if(((rd->meshes[i].originalCommands[j].words.w0>>24)&255)==(G_DL&255))return 0;
    rd->cuts|=1u<<part;
    for(int i=0;i<rd->nmeshes;i++)rdPatch(rd,&rd->meshes[i]);
    pdphysRagdollDetach(rd->handle,part);
    return 1;
}

Gfx *rdDrawCaps(RdBody *rd,Gfx *gdl)
{
    if(!rd->cuts)return gdl;
    gDPPipeSync(gdl++);
    gSPTexture(gdl++,0,0,0,G_TX_RENDERTILE,G_OFF);
    gDPSetCycleType(gdl++,G_CYC_1CYCLE);
    gDPSetCombineMode(gdl++,G_CC_SHADE,G_CC_SHADE);
    gSPClearGeometryMode(gdl++,G_CULL_BOTH|G_LIGHTING|G_TEXTURE_GEN|G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(gdl++,G_ZBUFFER|G_SHADE|G_SHADING_SMOOTH);
    if(rd->alpha<255) { gDPSetRenderMode(gdl++,G_RM_AA_ZB_XLU_SURF,G_RM_AA_ZB_XLU_SURF2); }
    else { gDPSetRenderMode(gdl++,G_RM_AA_ZB_OPA_SURF,G_RM_AA_ZB_OPA_SURF2); }
    for(int part=0;part<rd->count;part++)if(rd->cuts&(1u<<part)) {
        int parent=rd->parent[part];
        float normal[3],u[3],v[3],length=0;
        for(int k=0;k<3;k++){normal[k]=rd->initial[part].m[3][k]-rd->initial[parent].m[3][k];length+=normal[k]*normal[k];}
        length=sqrtf(length); if(length<1e-6f)continue;
        for(int k=0;k<3;k++)normal[k]/=length;
        float axis[3]={fabsf(normal[0])<0.8f?1:0,0,fabsf(normal[0])<0.8f?0:1};
        u[0]=normal[1]*axis[2]-normal[2]*axis[1]; u[1]=normal[2]*axis[0]-normal[0]*axis[2]; u[2]=normal[0]*axis[1]-normal[1]*axis[0];
        length=sqrtf(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]);
        for(int k=0;k<3;k++)u[k]/=length;
        v[0]=normal[1]*u[2]-normal[2]*u[1]; v[1]=normal[2]*u[0]-normal[0]*u[2]; v[2]=normal[0]*u[1]-normal[1]*u[0];
        for(int side=0;side<2;side++) {
            int matrix=rd->primary[side?parent:part]; Mtxf inverse;
            if(!rdInverse(&rd->bind[matrix],&inverse))continue;
            Vtx *vertices=dynAllocate(9*sizeof(Vtx)); if(!vertices)continue;
            memset(vertices,0,9*sizeof(Vtx));
            for(int j=0;j<9;j++) {
                float p[3]; float angle=(j-1)*6.28318530718f/8;
                for(int k=0;k<3;k++)p[k]=rd->initial[part].m[3][k]+(j?rd->radius[part]*(cosf(angle)*u[k]+sinf(angle)*v[k]):0);
                rdTransform(&inverse,p,1);
                for(int k=0;k<3;k++)vertices[j].v.ob[k]=(s16)fmaxf(-32768,fminf(32767,roundf(p[k])));
                vertices[j].v.cn[0]=j?90:120; vertices[j].v.cn[1]=6; vertices[j].v.cn[2]=6; vertices[j].v.cn[3]=(u8)rd->alpha;
            }
            gSPMatrix(gdl++,(void *)(uintptr_t)(0x03000000u+matrix*sizeof(Mtx)),G_MTX_MODELVIEW|G_MTX_LOAD|G_MTX_NOPUSH);
            gSPVertex(gdl++,vertices,9,0);
            for(int j=1;j<=8;j++)gSP1Triangle(gdl++,0,j,j==8?1:j+1,0);
        }
    }
    return gdl;
}
