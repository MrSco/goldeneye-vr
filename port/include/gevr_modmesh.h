#ifndef GEVR_MODMESH_H
#define GEVR_MODMESH_H
#include <ultra64.h>
#include <bondtypes.h>

typedef struct GevrModMesh GevrModMesh;
/* 0: legacy model or missing; -1: native format that could not be loaded;
 * 1: native model, owned by the mission cache. */
int gevrModMeshLoad(const char *name, const GevrModMesh **out);
void gevrModMeshReset(void);
Gfx *gevrModMeshDraw(Gfx *gdl, const Mtxf *pose, const GevrModMesh *mesh);
const GevrModMesh *gevrModGunMesh(s32 hand);
#endif
