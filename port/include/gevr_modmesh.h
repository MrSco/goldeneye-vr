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
/* Optional pack body fit for the mesh the hand is actually drawing. */
const Mtxf *gevrModGunFrame(s32 hand);
/* A model-local point in its fitted carrier coordinates; NULL fit is identity. */
void gevrModMeshPoint(const Mtxf *fit, const float point[3], float out[3]);
/* Fitted muzzle in carrier coordinates, absent for stock or a pending choice. */
s32 gevrModGunMuzzle(s32 hand, coord3d *out);
#endif
