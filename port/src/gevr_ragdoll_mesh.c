/* Per-snapshot native seams, following CE's per-instance display-list strategy.
 * Shared model definitions and living characters are never patched. */
#include "gevr_ragdoll_internal.h"
#include "model.h"
#include "dyn.h"
#include "gevr_bean.h"
#include "gbi_extension.h"

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
    /* positions from the original, blood stains (alpha) as painted since */
    for(int j=0;j<m->nvertices;j++) {
        const s32 paint=m->rw->Vertices[j].a;
        m->rw->Vertices[j]=m->original[j];
        m->rw->Vertices[j].a=paint;
    }
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
    if(part<0 || part>=rd->count || rd->parent[part]<0 || g_RdSkelHuman.cutradius[rd->skel[part]]<=0 || (rd->cuts&(1u<<part)))return 0;
    /* A nested list could still load the other piece's matrices. Leave that
     * model joined rather than allow a cut with an unpatched seam. */
    for(int i=0;i<rd->nmeshes;i++)for(int j=0;j<rd->meshes[i].ncommands;j++)
        if(((rd->meshes[i].originalCommands[j].words.w0>>24)&255)==(G_DL&255))return 0;
    rd->cuts|=1u<<part;
    for(int i=0;i<rd->nmeshes;i++)rdPatch(rd,&rd->meshes[i]);
    rdBuildCaps(rd);
    pdphysRagdollDetach(rd->handle,part);
    return 1;
}

/* ---- CE's cut caps (dismember.c:463-1180, 1636-1776) ----
 * Every triangle of the body's native lists is gathered as drawn after the
 * seams were patched, its vertices welded by their death-pose position. On
 * each piece, a hole's border is the edges used once on that piece that
 * another piece also uses, or open edges right at a cut. Each border loop
 * is closed with a fan from its centre; holes the mesh can't describe get
 * CE's small disc. Bean visuals draw their own skin, so they get discs. */
#define RD_WELD 0.5f /* world units: vertices this close are the same */

typedef struct RdEdge { int a, b, piece; RdCapVtx va, vb; } RdEdge;
typedef struct RdCapBuild { float (*pos)[3]; int nverts, maxverts; RdEdge *edges; int nedges, maxedges; } RdCapBuild;

static int rdWeld(RdCapBuild *mb, const float p[3])
{
    for (int i = 0; i < mb->nverts; i++)
        if (fabsf(mb->pos[i][0]-p[0]) < RD_WELD && fabsf(mb->pos[i][1]-p[1]) < RD_WELD && fabsf(mb->pos[i][2]-p[2]) < RD_WELD)
            return i;
    if (mb->nverts == mb->maxverts) return -1;
    memcpy(mb->pos[mb->nverts], p, sizeof(mb->pos[0]));
    return mb->nverts++;
}

typedef struct RdSlot { int id; RdCapVtx v; } RdSlot;
static void rdEdge(RdCapBuild *mb, const RdSlot *x, const RdSlot *y, int piece)
{
    if (x->id < 0 || y->id < 0 || x->id == y->id || mb->nedges == mb->maxedges) return;
    RdEdge *e = &mb->edges[mb->nedges++];
    const int swap = x->id > y->id;
    e->a = swap ? y->id : x->id; e->b = swap ? x->id : y->id;
    e->va = swap ? y->v : x->v; e->vb = swap ? x->v : y->v;
    e->piece = piece;
}

static int rdEdgeCmp(const void *x, const void *y)
{
    const RdEdge *a = x, *b = y;
    return a->a != b->a ? (a->a < b->a ? -1 : 1) : a->b != b->b ? (a->b < b->b ? -1 : 1) : 0;
}

static int rdFind(int *uf, int x) { while (uf[x] != x) x = uf[x] = uf[uf[x]]; return x; }

static int rdToLocal(const RdBody *rd, int matrix, const float world[3], s16 out[3])
{
    Mtxf inverse; float p[3] = { world[0], world[1], world[2] };
    if (!rdInverse(&rd->bind[matrix], &inverse)) return 0;
    rdTransform(&inverse, p, 1);
    for (int k = 0; k < 3; k++) out[k] = (s16)fmaxf(-32768, fminf(32767, roundf(p[k])));
    return 1;
}

/* CE ragdollAddDiscCap: the cut joint, 0.7 of the cut radius */
static void rdDiscCap(RdBody *rd, int cut, int childside)
{
    if (rd->ncaps == RD_MAXCAPS) return;
    const int frame = childside ? cut : rd->parent[cut], matrix = rd->primary[frame];
    const float r = g_RdSkelHuman.cutradius[rd->skel[cut]] * rd->torso * 0.7f;
    float n[3], u[3], v[3], l = 0;
    for (int k = 0; k < 3; k++) { n[k] = rd->initial[cut].m[3][k] - rd->initial[rd->parent[cut]].m[3][k]; l += n[k]*n[k]; }
    l = sqrtf(l); if (l < 1e-6f) return;
    for (int k = 0; k < 3; k++) n[k] /= l;
    if (fabsf(n[1]) < 0.9f) { u[0] = n[2]; u[1] = 0; u[2] = -n[0]; } else { u[0] = 0; u[1] = -n[2]; u[2] = n[1]; }
    l = sqrtf(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]);
    for (int k = 0; k < 3; k++) u[k] /= l;
    v[0] = n[1]*u[2]-n[2]*u[1]; v[1] = n[2]*u[0]-n[0]*u[2]; v[2] = n[0]*u[1]-n[1]*u[0];
    RdCap *cap = &rd->caps[rd->ncaps];
    cap->edges = rdAlloc(rd, 8 * sizeof(*cap->edges));
    if (!cap->edges || !rdToLocal(rd, matrix, rd->initial[cut].m[3], cap->centre)) return;
    float rim[8][3];
    for (int i = 0; i < 8; i++) for (int k = 0; k < 3; k++)
        rim[i][k] = rd->initial[cut].m[3][k] + (u[k]*cosf(i*0.785398f) + v[k]*sinf(i*0.785398f)) * r;
    for (int i = 0; i < 8; i++) {
        cap->edges[i][0].mtx = cap->edges[i][1].mtx = (s16)matrix;
        if (!rdToLocal(rd, matrix, rim[i], cap->edges[i][0].v) || !rdToLocal(rd, matrix, rim[(i+1)%8], cap->edges[i][1].v)) return;
    }
    cap->matrix = matrix; cap->numedges = 8; cap->cut = cut; cap->childside = childside;
    rd->ncaps++; rd->ndiscs++;
}

void rdBuildCaps(RdBody *rd)
{
    rd->ncaps = rd->ndiscs = 0;
    if (!rd->cuts) return;
    unsigned char covered[RD_PARTS][2] = {{0}};
    RdCapBuild mb = {0};
    int *uf = NULL;
    mb.maxverts = 4096; mb.maxedges = 3 * 4096;
    mb.pos = malloc(mb.maxverts * sizeof(*mb.pos));
    mb.edges = malloc(mb.maxedges * sizeof(*mb.edges));
    if (!mb.pos || !mb.edges || gevrBeanDrawsModel(rd->model.obj)) goto done;

    for (int i = 0; i < rd->nmeshes; i++) {
        RdMesh *m = &rd->meshes[i];
        const int piece = rdPiece(rd, rd->owner[m->owner]);
        RdSlot slots[16];
        for (int s = 0; s < 16; s++) slots[s].id = -1;
        int matrix = m->owner;
        for (int c = 0; c < m->ncommands; c++) {
            const Gfx *g = &m->commands[c];
            const unsigned op = (g->words.w0 >> 24) & 255;
            if (op == (G_ENDDL & 255)) break;
            if (op == (G_MTX & 255)) {
                matrix = (g->words.w1 & 0xffffff) / sizeof(Mtx);
            } else if (op == (G_VTX & 255)) {
                const int first = (g->words.w1 & 0xffffff) / sizeof(Vertex), n = ((g->words.w0 >> 20) & 15) + 1, v0 = (g->words.w0 >> 16) & 15;
                for (int k = 0; k < n && v0 + k < 16; k++) {
                    RdSlot *s = &slots[v0 + k];
                    s->id = -1;
                    if (first + k < 0 || first + k >= m->nvertices || matrix < 0 || matrix >= rd->matrices) continue;
                    float p[3];
                    for (int j = 0; j < 3; j++) { s->v.v[j] = m->rw->Vertices[first + k].coord.AsArray[j]; p[j] = s->v.v[j]; }
                    s->v.mtx = (s16)matrix;
                    rdTransform(&rd->bind[matrix], p, 1);
                    s->id = rdWeld(&mb, p);
                }
            } else if (op == (G_TRI1 & 255) || op == (G_TRI4 & 255)) {
                const int n = op == (G_TRI1 & 255) ? 1 : 4;
                for (int t = 0; t < n; t++) {
                    unsigned x, y, z;
                    if (n == 1) { x = ((g->words.w1 >> 16) & 255) / 10; y = ((g->words.w1 >> 8) & 255) / 10; z = (g->words.w1 & 255) / 10; }
                    else {
                        x = (g->words.w1 >> (t*8)) & 15; y = (g->words.w1 >> (t*8+4)) & 15; z = (g->words.w0 >> (t*4)) & 15;
                        if (!x && !y && !z) continue;
                    }
                    if (x >= 16 || y >= 16 || z >= 16) continue;
                    rdEdge(&mb, &slots[x], &slots[y], piece);
                    rdEdge(&mb, &slots[y], &slots[z], piece);
                    rdEdge(&mb, &slots[z], &slots[x], piece);
                }
            }
        }
    }
    qsort(mb.edges, mb.nedges, sizeof(*mb.edges), rdEdgeCmp);
    uf = malloc((mb.nverts + 1) * sizeof(int));
    if (!uf) goto done;

    for (int pieceRoot = -1; pieceRoot < rd->count; pieceRoot++) {
        if (pieceRoot >= 0 && !(rd->cuts & (1u << pieceRoot))) continue;
        const int piece = pieceRoot < 0 ? 0 : pieceRoot + 1;
        RdEdge **border = malloc((mb.nedges + 1) * sizeof(*border));
        int *group = malloc((mb.nedges + 1) * sizeof(int)), nborder = 0;
        if (!border || !group) { free(border); free(group); continue; }
        for (int e = 0; e < mb.nedges;) {
            const int a = mb.edges[e].a, b = mb.edges[e].b;
            RdEdge *mine = NULL; int nmine = 0, others = 0;
            for (; e < mb.nedges && mb.edges[e].a == a && mb.edges[e].b == b; e++) {
                if (mb.edges[e].piece == piece) { nmine++; mine = &mb.edges[e]; } else others++;
            }
            if (nmine != 1) continue;
            int use = others > 0;
            /* An already-open edge: only right at a cut on this piece's side */
            for (int j = 0; j < rd->count && !use; j++) {
                if (!(rd->cuts & (1u << j)) || (rdPiece(rd, j) != piece && rdPiece(rd, rd->parent[j]) != piece)) continue;
                const float reach = g_RdSkelHuman.cutradius[rd->skel[j]] * rd->torso * 1.6f;
                int near = 1;
                for (int k = 0; k < 2 && near; k++) {
                    const float *q = mb.pos[k ? b : a], *c = rd->initial[j].m[3];
                    near = sqrtf((q[0]-c[0])*(q[0]-c[0]) + (q[1]-c[1])*(q[1]-c[1]) + (q[2]-c[2])*(q[2]-c[2])) <= reach;
                }
                use = near;
            }
            if (use) border[nborder++] = mine;
        }
        /* Each border edge belongs to the nearest cut on this piece's side,
         * so holes that touch (both hips on the pelvis) stay separate */
        for (int i = 0; i < nborder; i++) {
            float mid[3], best = 1e30f;
            for (int k = 0; k < 3; k++) mid[k] = (mb.pos[border[i]->a][k] + mb.pos[border[i]->b][k]) * 0.5f;
            group[i] = -1;
            for (int j = 0; j < rd->count; j++) {
                if (!(rd->cuts & (1u << j)) || (rdPiece(rd, j) != piece && rdPiece(rd, rd->parent[j]) != piece)) continue;
                float dist = 0;
                for (int k = 0; k < 3; k++) { float x = mid[k] - rd->initial[j].m[3][k]; dist += x*x; }
                if (dist < best) { best = dist; group[i] = j; }
            }
        }
        for (int cut = 0; cut < rd->count; cut++) {
            if (!(rd->cuts & (1u << cut))) continue;
            for (int i = 0; i < mb.nverts; i++) uf[i] = i;
            for (int i = 0; i < nborder; i++) if (group[i] == cut) uf[rdFind(uf, border[i]->a)] = rdFind(uf, border[i]->b);
            for (int i = 0; i < nborder && rd->ncaps < RD_MAXCAPS; i++) {
                if (group[i] != cut) continue;
                const int root = rdFind(uf, border[i]->a);
                int seen = 0, cnt = 0;
                for (int j = 0; j < i && !seen; j++) seen = group[j] == cut && rdFind(uf, border[j]->a) == root;
                if (seen) continue;
                float centre[3] = {0, 0, 0}, ring = 0;
                for (int j = i; j < nborder; j++) if (group[j] == cut && rdFind(uf, border[j]->a) == root) {
                    for (int k = 0; k < 3; k++) centre[k] += mb.pos[border[j]->a][k] + mb.pos[border[j]->b][k];
                    cnt += 2;
                }
                if (cnt < 6) continue; /* a stray edge, not a hole */
                for (int k = 0; k < 3; k++) centre[k] /= cnt;
                const int childside = rdPiece(rd, cut) == piece, frame = childside ? cut : rd->parent[cut];
                if (rdPiece(rd, frame) != piece) continue;
                float normal[3], l = 0;
                for (int k = 0; k < 3; k++) {
                    normal[k] = rd->initial[rd->parent[cut]].m[3][k] - rd->initial[cut].m[3][k];
                    if (!childside) normal[k] = -normal[k];
                    l += normal[k]*normal[k];
                }
                l = sqrtf(l);
                RdCap *cap = &rd->caps[rd->ncaps];
                cap->edges = rdAlloc(rd, (cnt / 2) * sizeof(*cap->edges));
                if (!cap->edges || l < 1e-6f) continue;
                cap->numedges = 0;
                for (int j = i; j < nborder; j++) if (group[j] == cut && rdFind(uf, border[j]->a) == root) {
                    cap->edges[cap->numedges][0] = border[j]->va;
                    cap->edges[cap->numedges][1] = border[j]->vb;
                    cap->numedges++;
                    for (int k = 0; k < 2; k++) {
                        const float *q = mb.pos[k ? border[j]->b : border[j]->a];
                        ring += sqrtf((q[0]-centre[0])*(q[0]-centre[0]) + (q[1]-centre[1])*(q[1]-centre[1]) + (q[2]-centre[2])*(q[2]-centre[2]));
                    }
                }
                ring /= cnt;
                /* A slight bulge outwards, so it reads as a surface, not a hole */
                for (int k = 0; k < 3; k++) centre[k] += normal[k] / l * ring * 0.2f;
                cap->matrix = rd->primary[frame]; cap->cut = cut; cap->childside = childside;
                if (!rdToLocal(rd, cap->matrix, centre, cap->centre)) continue;
                rd->ncaps++;
                covered[cut][childside] = 1;
            }
        }
        free(border); free(group);
    }
done:
    free(uf); free(mb.pos); free(mb.edges);
    for (int cut = 0; cut < rd->count; cut++) if (rd->cuts & (1u << cut)) {
        if (!covered[cut][1]) rdDiscCap(rd, cut, 1);
        if (!covered[cut][0]) rdDiscCap(rd, cut, 0);
    }
}

/* A disc is only for a side no Bean rim covers this view */
static int rdBeanCovers(const RdBody *rd, const RdCap *cap)
{
    const int child = rdPiece(rd, cap->cut), parent = rdPiece(rd, rd->parent[cap->cut]);
    for (int i = 0; i < rd->nbean; i++)
        if (cap->childside ? rd->bean[i].piece == child && rd->bean[i].other == parent
                           : rd->bean[i].piece == parent && rd->bean[i].other == child)
            return 1;
    return 0;
}

static Gfx *rdFan(const RdBody *rd, const RdCap *cap, Gfx *gdl)
{
    Vtx *vertices=dynAllocate((size_t)cap->numedges*3*sizeof(Vtx));
    if(!vertices)return gdl;
    memset(vertices,0,(size_t)cap->numedges*3*sizeof(Vtx));
    int current=-1;
    for(int e=0;e<cap->numedges;e++) {
        for(int k=0;k<3;k++) {
            Vtx *v=&vertices[e*3+k];
            const int matrix=k ? cap->edges[e][k-1].mtx : cap->matrix;
            const s16 *xyz=k ? cap->edges[e][k-1].v : cap->centre;
            for(int j=0;j<3;j++)v->v.ob[j]=xyz[j];
            v->v.cn[0]=k?90:120; v->v.cn[1]=6; v->v.cn[2]=6; v->v.cn[3]=(u8)rd->alpha;
            if(matrix!=current) {
                gSPMatrix(gdl++,(void *)(uintptr_t)(0x03000000u+matrix*sizeof(Mtx)),G_MTX_MODELVIEW|G_MTX_LOAD|G_MTX_NOPUSH);
                current=matrix;
            }
            gSPVertex(gdl++,v,1,k);
        }
        gSP1Triangle(gdl++,0,1,2,0);
    }
    return gdl;
}

/* CE ragdollRenderCaps: rim vertices load with the mesh's own matrices, so
 * the cap stays on the edge however the limb bends */
Gfx *rdDrawCaps(RdBody *rd,Gfx *gdl)
{
    if(!rd->ncaps && !rd->nbean)return gdl;
    gDPPipeSync(gdl++);
    gSPTexture(gdl++,0,0,0,G_TX_RENDERTILE,G_OFF);
    gDPSetCycleType(gdl++,G_CYC_1CYCLE);
    gDPSetCombineMode(gdl++,G_CC_SHADE,G_CC_SHADE);
    gSPClearGeometryMode(gdl++,G_CULL_BOTH|G_LIGHTING|G_TEXTURE_GEN|G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(gdl++,G_ZBUFFER|G_SHADE|G_SHADING_SMOOTH);
    if(rd->alpha<255) { gDPSetRenderMode(gdl++,G_RM_AA_ZB_XLU_SURF,G_RM_AA_ZB_XLU_SURF2); }
    else { gDPSetRenderMode(gdl++,G_RM_AA_ZB_OPA_SURF,G_RM_AA_ZB_OPA_SURF2); }
    for(int i=0;i<rd->ncaps;i++)
        if(!rd->nbean || !rdBeanCovers(rd,&rd->caps[i]))gdl=rdFan(rd,&rd->caps[i],gdl);
    /* Bean's rims: a fan from their centre, in the skin's anchor space */
    for(int i=0;i<rd->nbean;i++) {
        RdCap cap={0};
        cap.matrix=rd->bean[i].matrix; cap.numedges=rd->bean[i].nedges;
        cap.edges=dynAllocate((size_t)cap.numedges*sizeof(*cap.edges));
        if(!cap.edges)continue;
        float centre[3]={0,0,0};
        for(int e=0;e<cap.numedges;e++)for(int k=0;k<2;k++) {
            cap.edges[e][k].mtx=(s16)cap.matrix;
            for(int j=0;j<3;j++){cap.edges[e][k].v[j]=rd->bean[i].rim[e][k][j];centre[j]+=rd->bean[i].rim[e][k][j];}
        }
        for(int j=0;j<3;j++)cap.centre[j]=(s16)roundf(centre[j]/(cap.numedges*2));
        gdl=rdFan(rd,&cap,gdl);
    }
    return gdl;
}
