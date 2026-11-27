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
/* The pack glove the hand's gun draws in a pose (GEVR_MODHAND_*), NULL for the
 * stock hand: only while the hand draws the pack body it was given with;
 * pose is its fit in carrier coordinates (its own frame, else the body's). */
const GevrModMesh *gevrModGunHand(s32 hand, s32 pose, Mtxf *fit, s32 *fitted);
/* the hand's gun is held two-handed and has a support glove, so the off
 * hand's own arm isn't drawn (gunfire.c) */
s32 gevrModGunSupportGloveShown(s32 hand);
/* the gun hand's trigger is held (the VR trigger; flat shows ready) */
s32 gevrModGunTriggerHeld(s32 hand);
/* gun.c, each tick after the body's action: gun parts with their own timing step theirs */
void gevrModAttachActionTick(s32 hand, s32 item, s32 shot, f32 ticks, s32 loaded, s32 holdOpen, s32 interval);
#endif
