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

/* Pixel-identical matches in each original GE model; others retain GE-X IDs. */
static const u16 dd44Textures[] = { 2876,2157,2877,2158,2878,2159,2879,2160,3274,1725,3275,776,0 };
static const u16 klobbTextures[] = { 1,1514,3274,1725,3279,2145,3280,2146,3281,2147,3282,2148,3284,1867,0 };
static const u16 zmgTextures[] = { 993,2155,994,2153,995,2156,996,2154,997,2151,998,2149,999,2152,1000,2150,3274,1725,3285,28,3286,27,0 };
static const u16 d5kTextures[] = { 923,2137,924,2138,925,2139,926,2140,3274,1725,3285,28,3286,27,0 };
static const u16 phantomTextures[] = { 1,1514,518,664,939,2141,940,2142,941,2143,942,2144,2447,1608,3274,1725,0 };
static const u16 ar33Textures[] = { 1022,884,3087,2293,3274,1725,3285,28,3286,27,3299,886,0 };

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
    .visible={1,0,silencerdraw,0,0,0}, .pistol=1, .compact=1 }

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
    PP7_DEF(ITEM_WPPKSIL, 1, -21.968322f, 52.960776f, 385.794870f),
    {
        .item=ITEM_TT33, .slot=5, .model="Gtt33Z", .texturePairs=dd44Textures,
        .fireAnim=236, .gunMatrix=33, .magMatrix=38, .heldMatrix=42,
        .reload={237,24,19,1,24}, .dualReload={1009,50,50,-1,-1},
        .holdFrame=20, .screenOffset={24.000000f,26.000000f,76.000000f},
        .magCentre={0.500000f,-61.500000f,-22.500000f}, .magTop={0.500000f,-4.000000f,-22.500000f}, .heldTop={96.422394f,72.278064f,-28.083353f},
        .magWell={0.500000f,-118.500000f,-44.000000f}, .heldToMag=PP7_ALIGNMENT,
        .muzzle={-21.779305f,60.827705f,354.222450f}, .screenMuzzle={0.000000f,63.899998f,298.435272f},
        .grabRoot={-19.175694f,-8.079137f,93.296245f}, .supportRoot={-39.498364f,-28.279365f,49.061958f},
        .numParts=2, .parts={42,43}, .visible={1,0}, .pistol=1, .compact=1
    },
    {
        .item=ITEM_SKORPION, .slot=6, .model="GskorpionZ", .texturePairs=klobbTextures,
        .fireAnim=1011, .gunMatrix=33, .magMatrix=37, .heldMatrix=38,
        .reload={1012,25,18,4,25}, .dualReload={1013,47,47,-1,-1},
        .holdFrame=20, .screenOffset={0.000000f,32.000000f,13.000000f},
        .magCentre={0.000000f,-7.000000f,-17.000000f}, .magTop={0.000000f,38.000000f,-17.000000f}, .heldTop={0.000000f,38.000000f,-17.000000f},
        .magWell={0.000000f,38.000000f,-17.000000f}, .heldToMag=IDENTITY,
        .muzzle={19.240172f,63.125797f,407.637995f}, .screenMuzzle={0.000000f,76.495583f,319.346497f},
        .grabRoot={20.030263f,-15.436797f,221.159018f}, .supportRoot={-12.282281f,-17.296873f,49.852005f},
        .numParts=2, .parts={42,43}, .visible={1,0}, .pistol=0, .compact=1
    },
    {
        .item=ITEM_UZI, .slot=8, .model="GuziZ", .texturePairs=zmgTextures,
        .fireAnim=278, .gunMatrix=33, .magMatrix=38, .heldMatrix=39,
        .reload={277,45,23,33,45}, .dualReload={1058,61,23,33,61},
        .holdFrame=40, .screenOffset={0.000000f,23.000000f,0.000000f},
        .magCentre={0.000000f,-152.000000f,-1.500000f}, .magTop={0.000000f,-75.000000f,-1.500000f}, .heldTop={-1.000000f,-75.000000f,-2.000000f},
        .magWell={0.000000f,-75.000000f,-1.500000f}, .heldToMag={{1,0,0,0},{0,1,0,0},{0,0,1,0},{1,0,0.5f,1}},
        .muzzle={20.369784f,76.576648f,341.022283f}, .screenMuzzle={0.000000f,57.087162f,255.171997f},
        .grabRoot={19.856725f,-86.416272f,76.018692f}, .supportRoot={-13.950680f,-13.872228f,49.998823f},
        .numParts=2, .parts={42,43}, .visible={1,0}, .pistol=0, .compact=1
    },
    {
        .item=ITEM_MP5K, .slot=9, .model="Gmp5kZ", .texturePairs=d5kTextures,
        .fireAnim=1022, .gunMatrix=33, .magMatrix=38, .heldMatrix=41,
        .reload={1019,48,25,25,48}, .dualReload={1019,48,25,25,48},
        .holdFrame=40, .screenOffset={0.000000f,8.000000f,6.000000f},
        .magCentre={0.000000f,-104.000000f,23.000000f}, .magTop={0.000000f,-8.000000f,23.000000f}, .heldTop={0.000000f,-8.000000f,23.000000f},
        .magWell={0.000000f,-8.000000f,23.000000f}, .heldToMag=IDENTITY,
        .muzzle={36.501513f,109.676946f,292.410569f}, .screenMuzzle={0.000000f,63.899998f,338.669983f},
        .grabRoot={33.109865f,-36.185327f,118.553810f}, .supportRoot={83.501094f,44.497476f,148.870497f},
        .numParts=2, .parts={42,40}, .visible={1,0}, .pistol=0, .compact=0
    },
    {
        .item=ITEM_MP5KSIL, .slot=10, .model="Gcmp150Z", .texturePairs=d5kTextures,
        .fireAnim=1022, .gunMatrix=33, .magMatrix=38, .heldMatrix=41,
        .reload={1019,48,25,25,48}, .dualReload={1019,48,25,25,48},
        .holdFrame=40, .screenOffset={0.000000f,8.000000f,6.000000f},
        .magCentre={0.000000f,-104.000000f,23.000000f}, .magTop={0.000000f,-8.000000f,23.000000f}, .heldTop={0.000000f,-8.000000f,23.000000f},
        .magWell={0.000000f,-8.000000f,23.000000f}, .heldToMag=IDENTITY,
        .muzzle={39.136895f,111.526412f,492.500989f}, .screenMuzzle={0.000000f,63.899998f,538.786316f},
        .grabRoot={33.109865f,-36.185327f,118.553810f}, .supportRoot={83.501094f,44.497476f,148.870497f},
        .numParts=2, .parts={42,40}, .visible={1,0}, .pistol=0, .compact=0
    },
    {
        .item=ITEM_SPECTRE, .slot=11, .model="GcycloneZ", .texturePairs=phantomTextures,
        .fireAnim=1068, .gunMatrix=33, .magMatrix=38, .heldMatrix=41,
        .reload={1020,48,16,16,48}, .dualReload={1020,48,16,16,48},
        .holdFrame=40, .screenOffset={0.000000f,0.000000f,0.000000f},
        .magCentre={0.000000f,-144.500000f,38.500000f}, .magTop={0.000000f,-83.000000f,38.500000f}, .heldTop={0.000000f,-83.000000f,38.500000f},
        .magWell={0.000000f,-83.000000f,38.500000f}, .heldToMag=IDENTITY,
        .muzzle={39.234583f,93.618309f,403.035495f}, .screenMuzzle={-0.359254f,40.148415f,444.701294f},
        .grabRoot={7.454986f,-77.427628f,133.014247f}, .supportRoot={75.305382f,8.527945f,214.503765f},
        .numParts=2, .parts={42,40}, .visible={1,0}, .pistol=0, .compact=0
    },
    {
        .item=ITEM_M16, .trackedMagWrist=1, .slot=12, .model="Gm16Z", .texturePairs=ar33Textures,
        .fireAnim=1003, .gunMatrix=33, .magMatrix=39, .heldMatrix=40,
        .reload={1004,41,17,17,41}, .dualReload={1004,41,17,17,41},
        .holdFrame=30, .screenOffset={8.000000f,22.000000f,4.000000f},
        .magCentre={-1.500000f,-98.000000f,25.500000f}, .magTop={-1.500000f,-18.000000f,25.500000f}, .heldTop={0.500000f,-18.000000f,25.500000f},
        .magWell={-1.500000f,-18.000000f,25.500000f}, .heldToMag={{1,0,0,0},{0,1,0,0},{0,0,1,0},{-2,0,0,1}},
        .muzzle={35.101284f,74.215844f,761.338969f}, .screenMuzzle={0.000000f,52.446072f,804.576599f},
        .grabRoot={33.426419f,-35.122415f,120.074552f}, .supportRoot={53.045119f,37.117804f,202.413309f},
        .numParts=2, .parts={42,40}, .visible={1,0}
    }
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

extern float VrGexPp7SupportRot[3];
static s32 fitItem(s32 item) { return item == ITEM_MP5KSIL ? ITEM_MP5K : item >= 0 && item < 64 ? item : 0; }
static s32 pp7(s32 item) { return item == ITEM_WPPK || item == ITEM_WPPKSIL; }
float *gevrGexWellFit(s32 item) { return pp7(item) ? VrGexPp7WellOff : item == ITEM_AK47 ? VrGexKf7WellOff : VrGexWeaponFits[fitItem(item)][5]; }
float *gevrGexHeldMagFit(s32 item) { return pp7(item) ? VrGexPp7MagOff : item == ITEM_AK47 ? VrGexKf7MagOff : VrGexWeaponFits[fitItem(item)][4]; }
float *gevrGexGunFit(s32 item) { return pp7(item) ? VrGexPp7GunOff : item == ITEM_AK47 ? VrGexGunOff : VrGexWeaponFits[fitItem(item)][0]; }
float *gevrGexSupportFit(s32 item) { return pp7(item) ? VrGexPp7Support : item == ITEM_AK47 ? VrGexForeHold : VrGexWeaponFits[fitItem(item)][2]; }
float *gevrGexSupportRotFit(s32 item) { return pp7(item) ? VrGexPp7SupportRot : VrGexWeaponFits[fitItem(item)][3]; }
float *gevrGexGrabFit(s32 item) { return pp7(item) ? VrGexPp7Grab : item == ITEM_AK47 ? VrReloadGrab[1] : VrGexWeaponFits[fitItem(item)][1]; }

float *gevrGexInstalledMagFit(s32 item) { return VrGexWeaponFits[pp7(item) ? ITEM_WPPK : fitItem(item)][6]; }
