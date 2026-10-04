#ifndef GEVR_PDANIM_H
#define GEVR_PDANIM_H

/*
 * Perfect Dark's animations, read from the player's GoldenEye X ROM, for the
 * GE-X guns' joints (port/src/gevr_pdanim.c, docs/gex-weapons.md).
 */
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Frames in animation animnum; 0 when it or the ROM is missing. */
s32 gevrPdAnimNumFrames(s32 animnum);

/* Part `part`'s values at a frame: rotation in radians, translation and
 * scale (1 when the animation has none). Returns 0 if the part has no data,
 * with rotation and translation zero. */
s32 gevrPdAnimPart(s32 animnum, s32 frame, s32 part, f32 rot[3], f32 trans[3], f32 scale[3]);

/* Perfect Dark's mtx4LoadRotationAndTranslation: rows 0..2 the rotation,
 * row 3 the translation, as Mtxf lays them out. */
void gevrPdMtxRotTrans(const f32 rot[3], const f32 pos[3], f32 m[4][4]);

#ifdef __cplusplus
}
#endif

#endif
