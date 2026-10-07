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
static const u16 silverPp7Textures[] = { 2876,2157,2877,2158,2878,2159,2879,2160,3275,776,0 };
static const u16 goldPp7Textures[] = { 2876,2157,2877,2158,2878,2159,2879,2160,0 };
static const u16 dd44Textures[] = { 2876,2157,2877,2158,2878,2159,2879,2160,3274,1725,3275,776,0 };
static const u16 klobbTextures[] = { 1,1514,3274,1725,3279,2145,3280,2146,3281,2147,3282,2148,3284,1867,0 };
static const u16 zmgTextures[] = { 993,2155,994,2153,995,2156,996,2154,997,2151,998,2149,999,2152,1000,2150,3274,1725,3285,28,3286,27,0 };
static const u16 d5kTextures[] = { 923,2137,924,2138,925,2139,926,2140,3274,1725,3285,28,3286,27,0 };
static const u16 phantomTextures[] = { 1,1514,518,664,939,2141,940,2142,941,2143,942,2144,2447,1608,3274,1725,0 };
static const u16 ar33Textures[] = { 1022,884,3087,2293,3274,1725,3285,28,3286,27,3299,886,0 };

static const u16 p90Textures[] = {1022,884,3285,28,3286,27,3300,2114,3301,2115,3302,2116,3303,2117,3304,1671,0};
static const u16 laserTextures[] = {515,849,518,664,694,508,1838,511,1839,850,1843,2130,1844,2131,1845,2132,1846,2133,2466,2057,0};
static const u16 shotgunTextures[] = {939,2141,940,2142,941,2143,942,2144,0};
static const u16 autoshotTextures[] = {243,82,1848,232,2385,74,2386,75,2387,76,2388,78,2389,79,2390,80,2392,84,2393,1217,0};
static const u16 cougarTextures[] = {923,2137,924,2138,925,2139,926,2140,2489,1141,2490,1142,2491,1143,3275,776,0};
static const u16 launcherTextures[] = {1,1514,257,880,1673,227,2983,767,3286,27,0};
static const u16 rocketTextures[] = {269,56,315,616,692,1379,694,508,695,1377,696,1378,1674,228,2496,418,2497,420,2637,532,0};
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

/* PP7_DEF's rig without the PP7's silencer or extra parts */
#define PP7_RIG(id, gexslot, file, textures) { \
    .item=id, .slot=gexslot, .model=file, \
    .texturePairs=textures, .fireAnim=236, .gunMatrix=33, .magMatrix=38, .heldMatrix=42, \
    .reload={1047,53,19,1,24}, .dualReload={1009,50,50,-1,-1}, \
    .holdFrame=20, .screenOffset={24,26,77}, .magCentre={0.5f,-61.5f,-22.5f}, \
    .magTop={0.5f,-4,-22.5f}, .heldTop={96.42239378f,72.27806361f,-28.08335267f}, \
    .magWell={0.5f,-118.5f,-44.0f}, \
    .heldToMag=PP7_ALIGNMENT, .muzzle={-21.278400f,55.235700f,237.304000f}, \
    .screenMuzzle={0,56,182}, \
    .grabRoot={-19.544478f,-64.599627f,71.853345f}, \
    .supportRoot={-39.498364f,-28.279365f,49.061958f}, \
    .numParts=2, .parts={42,43}, .visible={1,0}, .pistol=1, .compact=1 }

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
    /* The bonus PP7s: GE-X draws them as Perfect Dark's DY357 (silver) and
     * DY357-LX (gold) on the PP7's own rig: same skeleton, scripts (GE-X
     * includes the PP7's), clips and magazine meshes 42/43 on 38/42. They share
     * the PP7's screen anchor and family fits; the barrel ends where the
     * unsilenced PP7's does. Bodies keep GE-X's silver/gold textures. */
    PP7_RIG(ITEM_SILVERWPPK, 19, "Gdy357Z", silverPp7Textures),
    PP7_RIG(ITEM_GOLDWPPK, 20, "Gdy357trentZ", goldPp7Textures),
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
    },
    {
        .item=ITEM_FNP90,
        .slot=13,
        .model="Gfnp90Z",
        .gunMatrix=33,
        .magMatrix=38,
        .heldMatrix=39,
        .fireAnim=0,
        .restAnim=1038,
        .reload={1038,104,41,41,104},
        .dualReload={1038,104,41,41,104},
        .holdFrame=60,
        .numParts=2,
        .parts={41,42},
        .visible={1,0},
        .trackedMagWrist=1,
        .heldToMag=IDENTITY,
        .texturePairs=p90Textures,
        .gripMatrix=38,
        .pullUp=1,
        .screenOffset={0.000000f,0.000000f,0.000000f},
        .magCentre={0.000000f,88.000000f,-18.500000f},
        .magTop={0.000000f,74.000000f,-18.500000f},
        .magWell={0.000000f,74.000000f,-18.500000f},
        .heldTop={0.000000f,74.000000f,-18.500000f},
        .muzzle={20.335665f,86.540096f,262.667465f},
        .screenMuzzle={-0.066446f,64.356987f,281.000885f},
        .grabRoot={19.371676f,111.797445f,-36.919436f},
        .supportRoot={56.208606f,7.879716f,84.394411f}
    },
    {
        .item=ITEM_SNIPERRIFLE,
        .slot=16,
        .model="GsniperrifleZ",
        .gunMatrix=33,
        .magMatrix=40,
        .heldMatrix=41,
        .fireAnim=0,
        .restAnim=1036,
        .reload={1039,72,42,42,70},
        .dualReload={1039,72,42,42,70},
        .holdFrame=55,
        .numParts=2,
        .parts={40,41},
        .visible={1,0},
        .trackedMagWrist=1,
        .heldToMag=IDENTITY,
        .hasScope=1,
        .scopeRoot={84.062355f,160.668619f,-130.990765f},
        .screenOffset={0.000000f,0.000000f,-24.000000f},
        .magCentre={0.500000f,-66.500000f,-1.500000f},
        .magTop={0.500000f,0.000000f,-1.500000f},
        .magWell={0.500000f,0.000000f,-1.500000f},
        .heldTop={0.500000f,0.000000f,-1.500000f},
        .muzzle={68.266352f,84.391401f,800.749969f},
        .screenMuzzle={0.000000f,53.000000f,804.000000f},
        .grabRoot={67.750442f,44.920854f,-208.381778f},
        .supportRoot={82.311004f,9.009504f,178.547281f}
    },
    {
        .item=ITEM_LASER,
        .slot=21,
        .model="GdysuperdragonZ",
        .gunMatrix=33,
        .magMatrix=-1,
        .heldMatrix=-1,
        .fireAnim=236,
        .pistol=1,
        .compact=1,
        .texturePairs=laserTextures,
        .hasScope=1,
        .scopeRoot={-28.142334f,105.657446f,33.712573f},
        .screenOffset={32.000000f,17.000000f,18.000000f},
        .magCentre={0.000000f,0.000000f,0.000000f},
        .magTop={0.000000f,0.000000f,0.000000f},
        .magWell={0.000000f,0.000000f,0.000000f},
        .heldTop={0.000000f,0.000000f,0.000000f},
        .muzzle={-30.275230f,48.675247f,433.266698f},
        .screenMuzzle={-0.082999f,42.599998f,319.500000f},
        .grabRoot={0.000000f,0.000000f,0.000000f},
        .supportRoot={-39.498364f,-28.279365f,49.061958f}
    },
    {
        .item=ITEM_SHOTGUN,
        .slot=14,
        .model="GshotgunZ",
        .texturePairs=shotgunTextures,
        .singleRound=1,
        .gunMatrix=33,
        .magMatrix=33,
        .heldMatrix=38,
        .fireAnim=1006,
        .pistol=1,
        .trackedMagWrist=1,
        .restAnim=1006,
        .reload={1005,53,0,1,54},
        .dualReload={1005,53,0,1,54},
        .holdFrame=51,
        .numParts=2,
        .parts={-1,43},
        .visible={0,0},
        .heldToMag={{-0.956721f,-0.283435f,0.065942f,0.000000f},{0.290947f,-0.927144f,0.236124f,0.000000f},{-0.005788f,0.245090f,0.969483f,0.000000f},{9.530871f,0.044579f,139.660652f,1.000000f}},
        .screenOffset={0.000000f,0.000000f,0.000000f},
        .magCentre={9.293550f,10.093276f,179.409449f},
        .magTop={9.293550f,10.093276f,179.409449f},
        .magWell={9.293550f,10.093276f,179.409449f},
        .heldTop={0.000000f,0.000000f,41.000000f},
        .muzzle={12.792196f,78.478575f,584.089849f},
        .screenMuzzle={0.000000f,56.375000f,638.000000f},
        .grabRoot={22.009215f,32.187631f,125.498696f},
        .supportRoot={36.395319f,-2.015339f,246.939236f}
    },
    {
        .item=ITEM_AUTOSHOT,
        .slot=15,
        .model="Grcp120Z",
        .texturePairs=autoshotTextures,
        .singleRound=1,
        .gunMatrix=33,
        .magMatrix=33,
        .heldMatrix=38,
        .fireAnim=0,
        .pistol=0,
        .trackedMagWrist=1,
        .restAnim=1006,
        .reload={1005,53,0,1,54},
        .dualReload={1005,53,0,1,54},
        .holdFrame=51,
        .numParts=2,
        .parts={-1,43},
        .visible={0,0},
        .heldToMag={{-0.956721f,-0.283435f,0.065942f,0.000000f},{0.290947f,-0.927144f,0.236124f,0.000000f},{-0.005788f,0.245090f,0.969483f,0.000000f},{1.352107f,-3.304647f,138.766152f,1.000000f}},
        .screenOffset={0.000000f,0.000000f,0.000000f},
        .magCentre={1.114787f,6.744050f,178.514949f},
        .magTop={1.114787f,6.744050f,178.514949f},
        .magWell={1.114787f,6.744050f,178.514949f},
        .heldTop={0.000000f,0.000000f,41.000000f},
        .muzzle={13.191346f,73.810903f,575.765720f},
        .screenMuzzle={0.384615f,77.923077f,632.000000f},
        .grabRoot={13.820502f,2.631209f,122.280635f},
        .supportRoot={42.395319f,-32.264195f,246.939236f}
    },
    {
        .item=ITEM_ROCKETLAUNCH,
        .slot=24,
        .model="GdyrocketZ",
        .texturePairs=rocketTextures,
        .singleRound=1,
        .gunMatrix=33,
        .magMatrix=33,
        .heldMatrix=37,
        .fireAnim=1008,
        .pistol=1,
        .trackedMagWrist=1,
        .restAnim=1008,
        .reload={1007,93,0,24,93},
        .dualReload={1007,93,0,24,93},
        .holdFrame=55,
        .numParts=2,
        .parts={-1,40},
        .visible={0,0},
        .heldToMag={{0.999686f,-0.024887f,0.002771f,0.000000f},{0.024885f,0.999690f,0.000817f,0.000000f},{-0.002790f,-0.000747f,0.999996f,0.000000f},{-0.351584f,125.155830f,561.999469f,1.000000f}},
        .screenOffset={0.000000f,0.000000f,0.000000f},
        .magCentre={0.000000f,125.250000f,436.000000f},
        .magTop={0.000000f,125.250000f,436.000000f},
        .magWell={0.000000f,125.250000f,436.000000f},
        .heldTop={0.000000f,0.000000f,-126.000000f},
        .muzzle={10.517989f,109.895504f,424.241002f},
        .screenMuzzle={3.800000f,125.250000f,436.000000f},
        .grabRoot={0.890595f,112.147940f,762.914865f},
        .supportRoot={-25.404290f,17.755143f,-59.739831f},
        .compact=1
    },
    {
        .item=ITEM_GOLDENGUN,
        .slot=18,
        .model="Gleegun1Z",
        .singleRound=1,
        .fireAnim=236,
        .gunMatrix=33,
        .magMatrix=33,
        .heldMatrix=42,
        .reload={1045,82,0,38,115,19,115},
        .dualReload={1059,68,0,-1,-1,19,59},
        .holdFrame=60,
        .numParts=4,
        .parts={-1,43,45,42},
        .visible={0,0,0,0},
        .pistol=1,
        .trackedMagWrist=1,
        .compact=1,
        .heldToMag=IDENTITY,
        .screenOffset={24.000000f,30.000000f,74.000000f},
        .muzzle={-22.367944f,57.400586f,376.980107f},
        .screenMuzzle={0.030529f,64.543457f,319.046722f},
        .supportRoot={-39.498364f,-28.279365f,49.061958f},
        .grabRoot={-42.860337f,87.695306f,51.817671f},
        .magCentre={-24.000000f,34.000000f,-64.000000f},
        .magTop={-24,34,-51},
        .heldTop={-24,34,-51},
        .magWell={-24,34,-51}
    },
    /* Cougar: GmaianpistolZ, body = GE GrugerZ + (0,50,42) (131/143 vertices).
     * Reload 1032 swings the cylinder out by 80 and holds it still to 135;
     * the six-round speedloader (parts 40..45, one matrix 46) shows at 92,
     * seats on the cylinder's rear face at 121, ammo moves at 123, closed at 147.
     * The loader's rounds are not drawn once in the cylinder, as in the source. */
    {
        .item=ITEM_RUGER,
        .slot=17,
        .model="GmaianpistolZ",
        .texturePairs=cougarTextures,
        .singleRound=1,
        .loaderRounds=6,
        .fireAnim=1030,
        .restAnim=1030,
        .gunMatrix=33,
        .magMatrix=33,
        .heldMatrix=46,
        .reload={1032,123,0,92,121},
        .dualReload={1056,123,0,-1,-1},
        .holdFrame=100,
        .numParts=7,
        .parts={-1,40,41,42,43,44,45},
        .visible={0,0,0,0,0,0,0},
        .pistol=1,
        .trackedMagWrist=1,
        .compact=1,
        /* the loader's frame in the gun's at frame 121, seated */
        .heldToMag={{0.312677f,-0.949678f,-0.018587f,0},{0.948930f,0.313177f,-0.038119f,0},{0.042021f,-0.005719f,0.999100f,0},{42.539028f,14.827619f,41.727231f,1}},
        .screenOffset={0.000000f,50.000000f,42.000000f},
        .muzzle={-3.264762f,73.745319f,461.450909f},
        .screenMuzzle={0.000000f,85.199997f,397.716400f},
        .supportRoot={-39.498364f,-28.279365f,49.061958f},
        .grabRoot={0,0,0},
        /* the ring's centre on the cylinder's rear face; the rounds' tips lead */
        .magCentre={42.220902f,14.196192f,41.736997f},
        .magTop={42.220902f,14.196192f,41.736997f},
        .magWell={42.220902f,14.196192f,41.736997f},
        .heldTop={0.5f,-0.5f,34.0f}
    },
    /* Grenade launcher: GdydevastatorZ (slot 23) = GE GgrenadelaunchZ + (0,-24,118)
     * (108/108 vertices). Built on the Cougar's skeleton, with a drum on matrix
     * 34, it has no reload clip and no round mesh. The hand holds GoldenEye's
     * own round (PchrgrenaderoundZ, nose +z, 122 wide) scaled to 40 mm on the
     * Cougar loader's joint 46, posed by the Cougar's reload 1032 at 100. One
     * round goes into the drum's bottom chamber from behind per insertion: the
     * frame covers the drum's upper rear; chambers sit 47 units from its axis. */
    {
        .item=ITEM_GRENADELAUNCH,
        .slot=23,
        .model="GdydevastatorZ",
        .texturePairs=launcherTextures,
        .singleRound=1,
        .fireAnim=1030,
        .restAnim=1030,
        .gunMatrix=33,
        .magMatrix=33,
        .heldMatrix=46,
        .reload={0,0,0,-1,-1},
        .dualReload={0,0,0,-1,-1},
        .holdAnim=1032,
        .holdFrame=100,
        .payloadProp=PROP_CHRGRENADEROUND,
        .payloadScale=0.385f,
        .numParts=2,
        .parts={-1,-1},
        .visible={0,0},
        .pistol=1,
        .trackedMagWrist=1,
        .compact=1,
        /* the Cougar loader joint's seated orientation: the round points forward */
        .heldToMag={{0.312677f,-0.949678f,-0.018587f,0},{0.948930f,0.313177f,-0.038119f,0},{0.042021f,-0.005719f,0.999100f,0},{0,0,0,1}},
        .screenOffset={0.000000f,-24.000000f,118.000000f},
        .muzzle={-3.288169f,110.359302f,626.104645f},
        .screenMuzzle={0.000000f,53.250000f,639.000000f},
        .supportRoot={-39.498364f,-28.279365f,49.061958f},
        .grabRoot={0,0,0},
        /* the bottom chamber's rear mouth: drum axis (0,22,42.3) less 47 down */
        .magCentre={0.0f,-25.0f,42.3f},
        .magTop={0.0f,-25.0f,42.3f},
        .magWell={0.0f,-25.0f,42.3f},
        .heldTop={0.0f,0.0f,36.575f}
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
/* the PP7 rig's family: both PP7s and the bonus DY357s share its fits */
static s32 pp7(s32 item) { return item == ITEM_WPPK || item == ITEM_WPPKSIL || item == ITEM_SILVERWPPK || item == ITEM_GOLDWPPK; }
float *gevrGexWellFit(s32 item) { return pp7(item) ? VrGexPp7WellOff : item == ITEM_AK47 ? VrGexKf7WellOff : VrGexWeaponFits[fitItem(item)][5]; }
float *gevrGexHeldMagFit(s32 item) { return pp7(item) ? VrGexPp7MagOff : item == ITEM_AK47 ? VrGexKf7MagOff : VrGexWeaponFits[fitItem(item)][4]; }
float *gevrGexGunFit(s32 item) { return pp7(item) ? VrGexPp7GunOff : item == ITEM_AK47 ? VrGexGunOff : VrGexWeaponFits[fitItem(item)][0]; }
float *gevrGexSupportFit(s32 item) { return pp7(item) ? VrGexPp7Support : item == ITEM_AK47 ? VrGexForeHold : VrGexWeaponFits[fitItem(item)][2]; }
float *gevrGexSupportRotFit(s32 item) { return pp7(item) ? VrGexPp7SupportRot : VrGexWeaponFits[fitItem(item)][3]; }
float *gevrGexGrabFit(s32 item) { return pp7(item) ? VrGexPp7Grab : item == ITEM_AK47 ? VrReloadGrab[1] : VrGexWeaponFits[fitItem(item)][1]; }

float *gevrGexInstalledMagFit(s32 item) { return VrGexWeaponFits[pp7(item) ? ITEM_WPPK : fitItem(item)][6]; }
float *gevrGexHandFit(s32 item) { return VrGexWeaponFits[pp7(item) ? ITEM_WPPK : fitItem(item)][7]; }
float *gevrGexHandRotFit(s32 item) { return VrGexWeaponFits[pp7(item) ? ITEM_WPPK : fitItem(item)][8]; }
