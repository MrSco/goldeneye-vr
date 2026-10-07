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
    /* the clip whose holdFrame poses the hand holding the payload when the
     * rig has no reload clip of its own (0: reload.anim) */
    s32 holdAnim;
    /* a GoldenEye prop drawn as the held payload on heldMatrix when the rig has
     * none (grenade launcher), in model units times payloadScale */
    s32 payloadProp; f32 payloadScale;
    /* screen mode: place the rig's root (Perfect Dark's own first-person
     * pose) at screenOffset instead of squaring the gun joint to the screen;
     * for items whose mesh does not run along their joint's z (the knife) */
    s32 screenFromRoot;
    /* matrices whose geometry is collapsed: always (a stray mesh the rig
     * carries), or once a single-use item has left the hand (the item itself,
     * when it is not a toggled part); 0 none */
    s32 hideMatrix, spentMatrix;
    /* cooking (the trigger held): this clip plays on the mechanism to cookEnd
     * (the grenade's lever popping off), then cookMatrix is collapsed */
    s32 cookAnim; f32 cookEnd; s32 cookMatrix;
    s32 restAnim; /* explicit equip/idle pose when the ROM has no firing animation */
    s32 gripMatrix; /* optional reload joint actually held by the hand */
    s32 pullUp; /* top-loading magazine extracts up instead of down */
    s32 hasScope; f32 scopeRoot[3];
    s32 fireAnim, gunMatrix, magMatrix, heldMatrix;
    /* the fist: GoldenEye's two punches (PUNCH1, PUNCH2) play fireAnim and this,
     * on GoldenEye's own punch clock, in place of its keyframed swing */
    s32 fireAnimAlt;
    /* a watch item: the device is the watch on the left arm (in the headset the
     * tracked arm's, and the right hand presses it) */
    s32 watch;
    /* screen mode: held where the PP7's hand is, moved by the difference of the
     * two Gun fits, as a hand on a controller would hold them (gun.c
     * gevrGexScreenAnchor): the hand-held items, the fist, the watch */
    s32 screenHand;
    /* screen mode, a screenHand rig at GE-X's own place beside the PP7 (the fist,
     * user: its chops flew up in the air): the PP7's virtual controller moved by
     * this, model units in the root's frame - ten times the difference of their
     * Perfect Dark weapon positions (x the gun's left, y up, z ahead) */
    s32 screenFromPp7;
    f32 screenPp7Offset[3];
    /* a joint the off hand holds (the remote mine's detonator, user: the watch
     * in the palm): in the headset put in the tracked off hand's palm, on the
     * screen held by the rig's own left hand, off on the watch pages */
    s32 offHandMatrix;
    /* its watch face on that joint (user: the health, armor and radar readout on
     * the watch wherever it is, and the look at it pauses): the dial's centre
     * just above the glass and its radius, model units; out of the face is the
     * joint's -z and twelve o'clock its +y. Radius 0: no face */
    f32 offHandFace[4];
    /* the weapon panel and the watch's pages (gun.c gevrGexPoseStill): GoldenEye's
     * layout is made for its own model, so GE-X's, about its gun joint at rest, is
     * sized (x) and moved (y, z, w) onto GoldenEye's mesh's bounds (measured from
     * both ROMs: its centre and its diagonal). Size 0: as it is */
    f32 panelFit[4];
    /* ... and turned as GoldenEye's own lies (user: the knives, taser and watch lay
     * on their sides): p' = (size p) R + move, R's rows matched from both ROMs'
     * meshes. All zero: unturned */
    f32 panelRot[3][3];
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
static inline s32 gevrGexHoldAnim(const GexWeaponDef *def) { return def->holdAnim ? def->holdAnim : def->reload.anim; }
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
/* gun, grab, support, rotation, held mag, well, installed mesh, gun hand, gun hand rotation,
 * item size. For a hand-held item (knife, grenade, mine) 7 and 8 move and turn the item in
 * the hand instead of the hand, and 9's x scales it (1 + x). */
#define GEVR_GEX_FIT_COMPONENTS 10
extern float VrGexWeaponFits[64][GEVR_GEX_FIT_COMPONENTS][3];
float *gevrGexSupportRotFit(s32 item);
float *gevrGexSupportFit(s32 item);
float *gevrGexGrabFit(s32 item);
float *gevrGexGunFit(s32 item);
float *gevrGexHeldMagFit(s32 item);
float *gevrGexWellFit(s32 item);
float *gevrGexInstalledMagFit(s32 item);
/* Gun fit's Gun hand: GE-X's own right hand on the gun (cm right, up, back; degrees) */
float *gevrGexHandFit(s32 item);
float *gevrGexHandRotFit(s32 item);
float *gevrGexItemSizeFit(s32 item);

#ifdef __cplusplus
}
#endif
#endif
