#include <stddef.h>
#include "bondconstants.h"
#include "gevr_gexweapon.h"

static const u16 kf7Textures[] = {
    3271,26, 3285,28, 3286,27, 3287,2673, 1014,2672, 1,1514,
    3288,2674, 2444,870, 3289,2675, 2414,887, 3290,2120, 3291,2121,
    3292,2122, 3293,2123, 3294,2124, 3295,2125, 3296,2126, 3297,2127, 0
};
static const u16 pp7Textures[] = {
    2447,1608, 2876,2157, 2877,2158, 2878,2159, 2879,2160, 0
};

#define IDENTITY {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}
/* GE-X 6a GwppkZ: the same 16 magazine vertices in both display lists.
 * Least-squares rigid fit, residual < 0.5 model units (N64 quantisation).
 * The installed mesh uses matrix 38 (animation joint 41), held uses 42 (40). */
#define PP7_ALIGNMENT { \
    {-0.22183382f,0.88302514f,-0.41358960f,0}, \
    {0.61174536f,0.45633213f,0.64616453f,0}, \
    {0.75931375f,-0.10967037f,-0.64141643f,0}, \
    {-1.00194544f,-125.20611212f,-47.33734574f,1} }
#define PP7_DEF(id, silencerdraw, muzzleX, muzzleY, muzzleZ) { \
    .item=id, .slot=(silencerdraw ? 4 : 3), .model="GwppkZ", \
    .texturePairs=pp7Textures, .fireAnim=236, .gunMatrix=33, .magMatrix=38, .heldMatrix=42, \
    .reload={1047,53,19,1,24}, .dualReload={1009,50,50,-1,-1}, \
    .holdFrame=20, .screenOffset={24,26,77}, .magCentre={0.5f,-61.5f,-22.5f}, \
    .magTop={0.5f,-4,-22.5f}, .heldTop={96.42239378f,72.27806361f,-28.08335267f}, \
    .magWell={0.5f,-118.5f,-44.0f}, \
    .heldToMag=PP7_ALIGNMENT, .muzzle={muzzleX,muzzleY,muzzleZ}, \
    .screenMuzzle={0,56,(silencerdraw ? 331 : 182)}, \
    .grabRoot={-19.544478f,-64.599627f,71.853345f}, \
    .supportRoot={-39.498364f,-28.279365f,49.061958f}, \
    .numParts=6, .parts={42,43,45,44,46,47}, \
    .visible={1,0,silencerdraw,0,0,0}, .pistol=1 }

static const GexWeaponDef weapons[] = {
    {
        .item=ITEM_AK47, .slot=7, .model="Gak47Z", .texturePairs=kf7Textures,
        .fireAnim=1017, .gunMatrix=33, .magMatrix=39, .heldMatrix=40,
        .reload={1018,50,18,18,50}, .dualReload={1018,50,18,18,50},
        .holdFrame=40, .screenOffset={0,22,0}, .magCentre={0,-121,73}, .magTop={0,-10,73},
        .heldTop={0,-10,73}, .heldToMag=IDENTITY,
        .magWell={0,-10,73}, /* retain the already calibrated KF7 seat */
        /* Preserve the origin of saved GexMuzzleKF7 calibration. */
        .muzzle={0,23.27f,705.74f}, .screenMuzzle={0,23.27f,705.74f},
        .numParts=2, .parts={42,40}, .visible={1,0}
    },
    PP7_DEF(ITEM_WPPK, 0, -21.375904f, 52.805902f, 236.796127f),
    PP7_DEF(ITEM_WPPKSIL, 1, -21.968322f, 52.960776f, 385.794870f)
};

const GexWeaponDef *gevrGexWeaponGet(s32 item)
{
    unsigned i;
    for (i=0; i<sizeof(weapons)/sizeof(weapons[0]); ++i) {
        if (weapons[i].item == item) return &weapons[i];
    }
    return NULL;
}

extern float VrReloadGrab[2][3], VrGexForeHold[3];
extern float VrGexPp7Grab[3], VrGexPp7Support[3];
extern float VrGexGunOff[3], VrGexPp7GunOff[3];
extern float VrGexKf7MagOff[3], VrGexPp7MagOff[3];
extern float VrGexKf7WellOff[3], VrGexPp7WellOff[3];

float *gevrGexWellFit(s32 item)
{
    return item == ITEM_WPPK || item == ITEM_WPPKSIL ? VrGexPp7WellOff : VrGexKf7WellOff;
}

float *gevrGexHeldMagFit(s32 item)
{
    return item == ITEM_WPPK || item == ITEM_WPPKSIL ? VrGexPp7MagOff : VrGexKf7MagOff;
}

float *gevrGexGunFit(s32 item)
{
    return item == ITEM_WPPK || item == ITEM_WPPKSIL ? VrGexPp7GunOff : VrGexGunOff;
}

float *gevrGexSupportFit(s32 item)
{
    return item == ITEM_WPPK || item == ITEM_WPPKSIL ? VrGexPp7Support : VrGexForeHold;
}

float *gevrGexGrabFit(s32 item)
{
    return item == ITEM_WPPK || item == ITEM_WPPKSIL ? VrGexPp7Grab : VrReloadGrab[1];
}
