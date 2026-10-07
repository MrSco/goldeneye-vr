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
    f32 openShow, openHide; /* optional chamber/cover visibility interval */
} GexReloadDef;

typedef struct GexWeaponDef {
    s32 item, slot;
    const char *model;
    const u16 *texturePairs;
    s32 singleRound; /* load one shell/rocket without removing loaded ammunition */
    /* A speedloader (Cougar): a pickup carries up to this many rounds, as many
     * as fit and the reserve has; parts[1..] are its rounds, shown one per round
     * held; and the mechanism stands open at holdFrame while it is held. */
    s32 loaderRounds;
    s32 restAnim; /* explicit equip/idle pose when the ROM has no firing animation */
    s32 gripMatrix; /* optional reload joint actually held by the hand */
    s32 pullUp; /* top-loading magazine extracts up instead of down */
    s32 hasScope; f32 scopeRoot[3];
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
    s32 numParts, parts[8], visible[8]; /* appended switches; first two are magazines */
    s32 pistol;
    s32 trackedMagWrist; /* retain tracked wrist orientation without rotating the magazine */
    s32 compact; /* wrap the shooting grip; rest left arm is parked on these rigs */
} GexWeaponDef;

static inline s32 gevrGexHasMagazine(const GexWeaponDef *def) { return def && !def->singleRound && def->magMatrix >= 0 && def->heldMatrix >= 0; }
static inline s32 gevrGexHasAmmo(const GexWeaponDef *def) { return def && def->magMatrix >= 0 && def->heldMatrix >= 0; }
static inline s32 gevrGexRestAnim(const GexWeaponDef *def) { return def->restAnim ? def->restAnim : def->fireAnim; }
/* the mechanism (chamber, cylinder) stands open while the off hand holds the payload */
static inline s32 gevrGexOpensWhileHeld(const GexWeaponDef *def)
{
    return def->reload.openHide > 0 || def->loaderRounds > 1;
}
/* the rounds one pickup can carry: one, or a loader's as many as fit and the reserve has */
static inline s32 gevrGexPickupRounds(const GexWeaponDef *def, s32 loaded, s32 capacity, s32 reserve)
{
    s32 n = def->loaderRounds > 1 ? def->loaderRounds : 1;
    if (n > capacity - loaded) n = capacity - loaded;
    if (n > reserve) n = reserve;
    return n > 0 ? n : 0;
}
static inline s32 gevrGexChamberOpen(const GexReloadDef *reload, f32 frame, s32 held)
{
    return reload->openHide > reload->openShow && (held || (frame >= reload->openShow && frame < reload->openHide));
}
const GexWeaponDef *gevrGexWeaponGet(s32 item);
const GexWeaponDef *gevrGexWeaponForHand(s32 hand);
void gevrGexMagazineReady(s32 hand);
void gevrGexReloadReset(s32 hand);
/* Fits are per model family. Legacy KF7/PP7 keys remain valid;
 * D5K and its silenced variant share their fit storage. */
extern float VrGexWeaponFits[64][7][3]; /* gun, grab, support, rotation, held mag, well, installed mesh */
float *gevrGexSupportRotFit(s32 item);
float *gevrGexSupportFit(s32 item);
float *gevrGexGrabFit(s32 item);
float *gevrGexGunFit(s32 item);
float *gevrGexHeldMagFit(s32 item);
float *gevrGexWellFit(s32 item);
float *gevrGexInstalledMagFit(s32 item);

#ifdef __cplusplus
}
#endif
#endif
