#ifndef GEVR_GEXWEAPON_H
#define GEVR_GEXWEAPON_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Animation joints are decoded by the skeleton walker. These indices address
 * its output matrices, which are NOT necessarily the animation joint IDs. */
typedef struct GexReloadDef {
    s32 anim;
    f32 ammoFrame, magOut, heldShow, heldHide;
} GexReloadDef;

typedef struct GexWeaponDef {
    s32 item, slot;
    const char *model;
    const u16 *texturePairs;
    s32 fireAnim, gunMatrix, magMatrix, heldMatrix;
    GexReloadDef reload, dualReload;
    f32 holdFrame, screenOffset[3];
    f32 magCentre[3], magTop[3], heldTop[3];
    f32 magWell[3]; /* insertion entrance in installed-magazine coordinates, not its seated top */
    /* Rigid transform from held mesh coordinates to installed mesh coordinates. */
    f32 heldToMag[4][4];
    f32 muzzle[3];
    f32 screenMuzzle[3];
    f32 grabRoot[3], supportRoot[3]; /* rest pose, in model-root coordinates */
    s32 numParts, parts[6], visible[6]; /* appended switches; first two are magazines */
    s32 pistol;
} GexWeaponDef;

const GexWeaponDef *gevrGexWeaponGet(s32 item);
const GexWeaponDef *gevrGexWeaponForHand(s32 hand);
void gevrGexMagazineReady(s32 hand);
void gevrGexReloadReset(s32 hand);
/* Shared model fits: legacy KF7 keys remain valid; both PP7s share new keys. */
float *gevrGexSupportFit(s32 item);
float *gevrGexGrabFit(s32 item);
float *gevrGexGunFit(s32 item);
float *gevrGexHeldMagFit(s32 item);
float *gevrGexWellFit(s32 item);

#ifdef __cplusplus
}
#endif
#endif
