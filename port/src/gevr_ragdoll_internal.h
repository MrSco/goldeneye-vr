#ifndef GEVR_RAGDOLL_INTERNAL_H
#define GEVR_RAGDOLL_INTERNAL_H
#include "gevr_ragdoll.h"
#include <bondtypes.h>
#include <bondconstants.h>
#include <math.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "pdphysics.h"

#define RD_BODIES 24   /* Bodies stay's largest setting (chraction.c GEVR_BODIES_MAX) */
#define RD_PARTS PDPHYS_MAX_RAGDOLL_PARTS
#define RD_MATRICES 96
#define RD_NODES 512

/* Perfect Dark CE's human ragdoll skeleton (port/src/ragdoll.c g_RdSkelHuman).
 * PD's chr skeleton is GoldenEye's guard skeleton less its HEADER joint:
 * skeleton part p is the guard GROUP with JointID p+1 (1 pelvis, 2 chest,
 * 3 head, 4/5 upper arms, 6/7 forearms, 8/9 hands, 10/11 thighs, 12/13 shins,
 * 14/15 feet; gevr_bean_mapping_test checks these against the cartridge). */
#define RD_SKEL 15
typedef struct RdSkel {
    s8 parent[RD_SKEL];
    s8 hasbody[RD_SKEL];   /* 0: moves rigidly with its parent (hands, feet) */
    s8 child[RD_SKEL];     /* the part its bone runs to, -1 = none */
    s8 hingesign[RD_SKEL]; /* elbows and knees bend one way */
    float mass[RD_SKEL];   /* kg at RD_MASS_REFTORSO */
    float cutradius[RD_SKEL]; /* cap size (fraction of the torso), 0 = can't be cut */
} RdSkel;
extern const RdSkel g_RdSkelHuman;
#define RD_MASS_REFTORSO 41.0f

typedef struct RdMemory {
    struct RdMemory *next;
    unsigned retired;
    long double alignment;
    unsigned char bytes[];
} RdMemory;

typedef struct RdMesh {
    ModelNode *node;
    ModelRwData_DisplayList_CollisionRecord *rw;
    Vertex *original;
    Gfx *commands, *originalCommands;
    int ncommands, nvertices, owner;
} RdMesh;

/* CE's cut caps (dismember.c rdcap): a fan from the hole's centre to the
 * rim edges, each rim vertex drawn with the matrix and coordinates the mesh
 * uses there, so the cap stays on the edge of the hole */
#define RD_MAXCAPS 24
typedef struct RdCapVtx { s16 mtx, v[3]; } RdCapVtx;
typedef struct RdCap {
    int matrix, numedges;   /* the centre is drawn with this matrix */
    int cut, childside;     /* the body part cut off, and which side of it */
    s16 centre[3];
    RdCapVtx (*edges)[2];
} RdCap;

typedef struct RdBody {
    int handle, count, matrices, root, used;
    unsigned serial, cuts;
    float age, alpha;
    Model model;
    RdMemory *memory;
    RdMesh meshes[64];
    int nmeshes;
    ModelNode *nodes[RD_PARTS];
    int primary[RD_PARTS], parent[RD_PARTS], joint[RD_PARTS];
    int skel[RD_PARTS], body[RD_SKEL]; /* body index <-> CE skeleton part */
    int hitskel[20];                   /* hitpart -> skeleton part (BBOX nodes) */
    ModelNode *bbox[20];               /* the hit boxes, for blood stains */
    int bboxHit[20], bboxSkel[20], nbbox;
    int owner[RD_MATRICES];
    float radius[RD_PARTS], torso;
    Mtxf skelInitial[RD_SKEL];         /* every skeleton joint at death, hands and feet too */
    unsigned lastpush;
    float colroompos[3];
    int asleep, onscreen, crossed;
    u8 visrooms[16];
    RdCap caps[RD_MAXCAPS];
    int ncaps, ndiscs;
    /* Bean skin rims this view (gevr_bean_caps.inc.h), drawn as fans */
    struct { int matrix, nedges, piece, other; const s16 (*rim)[2][3]; } bean[RD_MAXCAPS];
    int nbean;
    Mtxf bind[RD_MATRICES], offset[RD_MATRICES];
    /* rdPiece of each matrix's owner, for these cuts (gevrRagdollPiece, Remap) */
    unsigned pieceSerial, pieceCuts;
    u8 piece[RD_MATRICES];
    Mtxf joints[RD_PARTS], initial[RD_PARTS];
    coord3d origin;
    u8 rooms[16];
    rgba_u8 shade;
} RdBody;

/* Row-vector affine convention used by GE and the CE C interface. */
static inline void rdMul(const Mtxf *a, const Mtxf *b, Mtxf *out)
{
    Mtxf t = {0};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 4; k++) t.m[i][j] += a->m[i][k] * b->m[k][j];
    *out = t;
}
static inline int rdInverse(const Mtxf *a, Mtxf *out)
{
    float d = a->m[0][0]*(a->m[1][1]*a->m[2][2]-a->m[1][2]*a->m[2][1])
            - a->m[0][1]*(a->m[1][0]*a->m[2][2]-a->m[1][2]*a->m[2][0])
            + a->m[0][2]*(a->m[1][0]*a->m[2][1]-a->m[1][1]*a->m[2][0]);
    if (!isfinite(d) || fabsf(d) < 1e-12f) return 0;
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
        int r1=(j+1)%3, r2=(j+2)%3, c1=(i+1)%3, c2=(i+2)%3;
        out->m[i][j]=(a->m[r1][c1]*a->m[r2][c2]-a->m[r1][c2]*a->m[r2][c1])/d;
    }
    for (int j = 0; j < 3; j++) for (int k = 0; k < 3; k++)
        out->m[3][j] -= a->m[3][k]*out->m[k][j];
    out->m[3][3]=1;
    return 1;
}
static inline void rdTransform(const Mtxf *m, float p[3], int position)
{
    float t[3];
    for (int k = 0; k < 3; k++)
        t[k]=p[0]*m->m[0][k]+p[1]*m->m[1][k]+p[2]*m->m[2][k]+(position?m->m[3][k]:0);
    memcpy(p,t,sizeof(t));
}
static inline int rdPiece(const RdBody *rd, int part)
{
    for (int n=0; part>=0 && n<rd->count; n++,part=rd->parent[part])
        if (rd->cuts & (1u<<part)) return part+1;
    return 0;
}
void *rdAlloc(RdBody *rd, size_t size);
ModelNode *rdNext(Model *model, ModelNode *node);
void rdWorldInit(void);
void rdWorldUpdate(void);
void rdWorldReset(void);
void rdWorldStats(int room, int *walled, int *flooronly, int *tris, int *state);
void rdMoversUpdate(float dt, RdBody *bodies, int count);
void rdMoversReset(void);
int rdCopyMeshes(RdBody *rd);
int rdCut(RdBody *rd, int part);
void rdBuildCaps(RdBody *rd);
Gfx *rdDrawCaps(RdBody *rd, Gfx *gdl);
#endif
