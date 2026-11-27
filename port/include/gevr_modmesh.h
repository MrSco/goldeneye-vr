#ifndef GEVR_MODMESH_H
#define GEVR_MODMESH_H
#include <ultra64.h>
#include <bondtypes.h>

typedef struct GevrModMesh GevrModMesh;

#include "gevr_modapi.h"   /* GevrModGunParts */
/* 0: legacy model or missing; -1: native format that could not be loaded;
 * 1: native model, owned by the mission cache. */
int gevrModMeshLoad(const char *name, const GevrModMesh **out);
void gevrModMeshReset(void);
Gfx *gevrModMeshDraw(Gfx *gdl, const Mtxf *pose, const GevrModMesh *mesh);
/* Build (once) the body's lists split by moving pictures: 1 ready; 0 when a
 * picture isn't the file's, the groups share one, or memory ran out. */
int gevrModMeshSplit(const GevrModMesh *mesh, const unsigned pictures[GEVR_MODPART_GROUPS]);
/* The split body: the rest at pose, each group moved by its offset in the
 * body's own units; the whole body at pose when it was never split. */
Gfx *gevrModMeshDrawParts(Gfx *gdl, const Mtxf *pose, const GevrModMesh *mesh,
                          const unsigned pictures[GEVR_MODPART_GROUPS], const f32 offset[GEVR_MODPART_GROUPS][3]);
const GevrModMesh *gevrModGunMesh(s32 hand);
/* Optional pack body fit for the mesh the hand is actually drawing. */
const Mtxf *gevrModGunFrame(s32 hand);
/* A model-local point in its fitted carrier coordinates; NULL fit is identity. */
void gevrModMeshPoint(const Mtxf *fit, const float point[3], float out[3]);
/* Fitted muzzle in carrier coordinates, absent for stock or a pending choice. */
s32 gevrModGunMuzzle(s32 hand, coord3d *out);
/* Fitted off-hand hold point for the body the hand is actually drawing. */
s32 gevrModGunSupport(s32 hand, coord3d *out);
/* The moving parts of the body the hand is drawing, NULL for none; and each
 * group's offset this frame (gevr_modaction.h's share of its travel). */
const GevrModGunParts *gevrModGunParts(s32 hand, f32 offset[GEVR_MODPART_GROUPS][3]);
/* How far a gun attachment's travel goes this frame: 0..1 (handle: rack only). */
f32 gevrModGunActionShare(s32 hand, s32 handle);
/* gun.c, each tick after the body's action: gun parts with their own timing step theirs */
void gevrModAttachActionTick(s32 hand, s32 item, s32 shot, f32 ticks, s32 loaded, s32 holdOpen, s32 interval);
/* gun.c, each of another player's copy's ticks (#335): its hand's action and
 * its gun parts' step on the shot its copy fired here (shot), by its timing */
void gevrModAttachRemoteActionTick(s32 slot, s32 hand, s32 item, s32 shot, f32 ticks, s32 interval);
#endif
