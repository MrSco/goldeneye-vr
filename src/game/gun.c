#include <ultra64.h>
#include <limits.h>
#include <bondconstants.h>
#include <bondtypes.h>
#include <bondgame.h>
#include <music.h>
#include <snd.h>
#include "bondview.h"
#include "bondinv.h"
#include "gun.h"
#include "chrobjdata.h"
#include "game/propobj.h"
#include "game/objective_status.h"
#include "quaternion.h"
#include "image_bank.h"
#include "bondwalk2.h"
#include "othermodemicrocode.h"
#include "player.h"
#include "lv.h"
#include "random.h"
#include "math_asinfacosf.h"
#include "loadobjectmodel.h"
#include "objecthandler.h"
#include "image.h"
#include "tex.h"
#include "debugmenu_handler.h"
#include "fr.h"
#include "assets/obseg/text/LgunE.h"
#include "textrelated.h"
#include "chrai.h"
#include "model.h"
#include "options.h"
#include "mpmenu.h"
#include "joy.h"
#include "matrixmath.h"
#include "bondinv.h"
#include "stan.h"
#include "gbi_extension.h"


// bss
ALSoundState *g_CasingSfxState;
ALSoundState* g_UnusedSfxState; // Unused, type assumed from surrounding variables.
ALSoundState* g_ImpactSfxStates[NUM_IMPACT_SFX_STATES];

CasingRecord g_Casings[20];
s32 dword_CODE_bss_80076A48; // Unused

#ifdef GEVR
s32 g_gevrMotionThrowActive[2] = { 0, 0 };
struct coord3d g_gevrMotionThrowVel[2];

/*
 * Online projectiles, after Perfect Dark port-net's SVC_PROP_SPAWN with
 * GEVR's owner -> host -> peers relay. The owner's spawner sends its spawn
 * point, velocity and orientation; the other headsets run the same spawner
 * for that player's copy and it takes them instead of its own (a copy has
 * no first-person hand to throw from, and its view matrices are not built
 * online). The projectile then flies and bounces on every headset; the
 * explosion itself comes from the owner (explosion.c).
 */
static struct {
    s32 active;
    coord3d pos, vel, extra;
    f32 rot[9];
} s_gevrNetSpawn;

s32 gevrNetProjectile(s32 kind, s32 hand, coord3d *pos, coord3d *vel, Mtxf *rot, coord3d *extra)
{
    extern bool netIsActive(void);
    extern int netGetLocalSlot(void);
    extern void netSendProjectile(s32 kind, s32 hand, s32 item, const coord3d *pos, const coord3d *vel,
                                  const f32 *rot9, const coord3d *extra, s32 cooktimer);
    s32 i;

    if (s_gevrNetSpawn.active)
    {
        *pos = s_gevrNetSpawn.pos;
        *vel = s_gevrNetSpawn.vel;
        for (i = 0; i < 9; i++)
        {
            rot->m[i / 3][i % 3] = s_gevrNetSpawn.rot[i];
        }
        if (extra != NULL)
        {
            *extra = s_gevrNetSpawn.extra;
        }
        return TRUE;
    }

    if (netIsActive() && get_cur_playernum() == netGetLocalSlot())
    {
        f32 rot9[9];

        for (i = 0; i < 9; i++)
        {
            rot9[i] = rot->m[i / 3][i % 3];
        }
        netSendProjectile(kind, hand, getCurrentPlayerWeaponId(hand), pos, vel, rot9, extra,
                          g_CurrentPlayer->last_z_trigger_timer);
    }
    return FALSE;
}

void gevrNetSpawnProjectile(s32 slot, s32 kind, s32 hand, s32 item, const coord3d *pos, const coord3d *vel,
                            const f32 *rot9, const coord3d *extra, s32 cooktimer)
{
    extern int netGetLocalSlot(void);
    extern void gevrSetCopyTrace(s32 on);
    s32 prev = get_cur_playernum();
    s32 local = netGetLocalSlot();
    struct player *pl;
    struct player *localpl;
    Mtxf *savedv2w;
    Mtxf *savedw2v;
    ITEM_IDS savedweapon;
    s32 savedcook;
    s32 i;

    if (slot < 0 || slot >= MAX_PLAYER_COUNT || slot == local || local < 0 || local >= MAX_PLAYER_COUNT || hand < 0 || hand > 1)
    {
        return;
    }
    pl = g_playerPointers[slot];
    localpl = g_playerPointers[local];
    if (pl == NULL || pl->prop == NULL || pl->prop->stan == NULL || localpl == NULL || localpl->viewtoworldmtxf == NULL)
    {
        return;
    }

    /* the spawners read the view matrices before taking the sent values */
    savedv2w = pl->viewtoworldmtxf;
    savedw2v = pl->field_10CC;
    savedweapon = pl->hands[hand].weaponnum;
    savedcook = pl->last_z_trigger_timer;
    pl->viewtoworldmtxf = localpl->viewtoworldmtxf;
    pl->field_10CC = localpl->field_10CC;
    pl->hands[hand].weaponnum = (ITEM_IDS)item;
    pl->last_z_trigger_timer = cooktimer;

    s_gevrNetSpawn.pos = *pos;
    s_gevrNetSpawn.vel = *vel;
    s_gevrNetSpawn.extra = *extra;
    for (i = 0; i < 9; i++)
    {
        s_gevrNetSpawn.rot[i] = rot9[i];
    }
    s_gevrNetSpawn.active = TRUE;

    set_cur_player(slot);
    gevrSetCopyTrace(TRUE);

    switch (kind)
    {
        case GEVR_NETPROJ_GRENADE:
            generate_player_thrown_grenade(hand);
            break;
        case GEVR_NETPROJ_KNIFE:
            generate_player_thrown_knife(hand);
            break;
        case GEVR_NETPROJ_OBJECT:
            generate_player_thrown_object(hand);
            break;
        case GEVR_NETPROJ_GLGRENADE:
            gunSpawnGLGrenade(hand);
            break;
        case GEVR_NETPROJ_ROCKET:
            gunFireTankShell(hand);
            {
                /* the rocket leaves the copy's launcher until its reload (gevr_heldgun.c) */
                extern void gevrHeldGunRocketFired(s32 slot, s32 hand);

                gevrHeldGunRocketFired(slot, hand);
            }
            break;
    }

    gevrSetCopyTrace(FALSE);
    set_cur_player(prev);
    s_gevrNetSpawn.active = FALSE;
    pl->viewtoworldmtxf = savedv2w;
    pl->field_10CC = savedw2v;
    pl->hands[hand].weaponnum = savedweapon;
    pl->last_z_trigger_timer = savedcook;
}
#endif

#ifdef REFRESH_PAL
    /* PAL */
    #define THROWN_ITEM_REFRESH_RATE                   50
    #define THROWN_ITEM_TIMER_SOLO                     250
    #define THROWN_ITEM_TIMER_MULTI                    150
    #define THROWN_ITEM_TIMER_DEFAULT                  200
    #define GLGRENADE_TIMER                            1000
    #define DUAL_WIELD_TRIGGER_SWAP_TICKS              24
    #define DUAL_WIELD_SINGLE_TRIGGER_SWAP_TICKS       36
    #define WATCH_SOUND_DURATION_TICKS                 250
    #define GUN_SPRING_DAMP                            0.9402999877929688f
    #define GUN_SPRING_SCALE                           0.05970001220703125f
#else
    /* NTSC */
    #define THROWN_ITEM_REFRESH_RATE                   60
    #define THROWN_ITEM_TIMER_SOLO                     300
    #define THROWN_ITEM_TIMER_MULTI                    180
    #define THROWN_ITEM_TIMER_DEFAULT                  240
    #define GLGRENADE_TIMER                            1200
    #define DUAL_WIELD_TRIGGER_SWAP_TICKS              20
    #define DUAL_WIELD_SINGLE_TRIGGER_SWAP_TICKS       30
    #define WATCH_SOUND_DURATION_TICKS                 300
    #define GUN_SPRING_DAMP                            0.95f
    #define GUN_SPRING_SCALE                           0.050000012f
#endif

extern f32 g_GLGrenadeLaunchUnk8C;
extern f32 g_GLGrenadeLaunchUnk94;
extern f32 g_TankShellSpeed;

// data
////D:80032440
//rgba_u8 D_80032440[] = {
//	{0x96, 0x96, 0x96, 0},
//	{0x96, 0x96, 0x96, 0}
//};
//
////D:80032448
//rgba_u8 D_80032448[] = {
//	{0xFF, 0xFF, 0xFF, 0},
//	{0xFF, 0xFF, 0xFF, 0},
//	{0xB2, 0x4D, 0x2E, 0}
//};
/**
 * Controls the lighting on environment mapped weapons such as the Cougar Magnum and Golden Gun.
 */
Lights1 g_WeaponEnvmapLight = gdSPDefLights1(
    0x96, 0x96, 0x96,   // ambient RGB
    0xff, 0xff, 0xff,   // diffuse RGB
    0xb2, 0x4d, 0x2e);  // direction
//D:80032454
//u32 D_80032454 = 0;

//D:80032458
u32 D_80032458 = 0;

//D:8003245C
#ifdef GEVR
/* D45 (gepc-ref): Gfx slots are 16 bytes on a 64-bit host, so a model file's
 * display-list regions double, and texLoadFromGdl's texture-marker expansion
 * adds more RDP commands on top. The per-hand buffer holds the model region
 * plus the texture pool after it, and bondview's body+head+held-prop chain
 * (bondview2.c, no-chr path) uses the same buffers; its worst case is
 * 0x1DB9A, and the suit path needs pool 0xA0B0 + region 0x18000 = 0x220B0. */
u32 size_item_buffer[] = {0x23000, 0x23000};
#else
u32 size_item_buffer[] = {0x14820, 0x14820};
#endif

//D:80032464
#ifdef GEVR
/* D45 (gepc-ref): the model region inside that buffer. The largest weapon
 * model file is GautoshotZ at 0xE788 once converted. At the N64's 0x7530,
 * sub_GAME_7F0762E0 moves the file's tail to the end of the region and writes
 * the expanded display lists back from the front; with host-sized files the
 * writer overtook the unread tail, and switching to a freshly loaded gun
 * (Gtt33Z, the DD44) walked into a display list of zeros:
 * "FATAL: Unknown GBI opcode 0x00". The silenced PP7 already logged
 * "no room to rewrite its display lists (file 25264, allocation 30000)".
 * Must grow together with size_item_buffer: the texture pool is the
 * difference, and growing this alone would shrink it from 0xD2F0 to 0x5820. */
u32 D_80032464[] ={0xF000, 0xF000};
#else
u32 D_80032464[] ={0x7530, 0x7530};
#endif



//D:8003246C
CartridgeModelFileRecord ejected_cartridge[] = {
	{&cartridge_header, "GcartridgeZ"},
	{&cartrifle_header, "GcartrifleZ"},
	{&cartblue_header, "GcartblueZ"},
	{&cartshell_header, "GcartshellZ"},
	{0, ""}
};

#include <assets/obseg/gun/gunWeaponStats.inc.c>

//D:80033924
#include <assets/obseg/gun/gunModelFileRecord.inc.c>

//D:80034C9C
u32 cartridges_eject = 0;
//D:80034CA0
u32 g_gunDebKeyframeIndex = 0;

//D:80034CA4
u32 D_80034CA4[] = {
	       0x0,           0x0,           0x0,           0x0,
	       0x0,           0x0,           0x0,    0x3F000000,
	0x41000000,           0x0,           0x0,           0x0,
	       0x0,           0x0,           0x0,           0x0,
	0x3F000000,    0x41000000,           0x0,    0x40C00000,
	0xBFC00000,           0x0,    0x40B487B1,    0x3E70C0AD,
	0x3E0AE536,    0x3F000000,    0x41000000,           0x0,
	0x41480000,    0xC0600000,           0x0,    0x40C159EC,
	0x3D374BC7,    0x3F0E4378,    0x3F000000,    0x41000000,
	       0x0,    0xC1200000,    0xC1300000,           0x0,
	0x3F9ED962,    0x3EA24C40,    0x3F8B0DF1,    0x3F000000,
	0x41000000,           0x0,    0xC1600000,    0xC1700000,
	       0x0,    0x3FEA4780,    0x40C498E3,    0x3FA316D3,
	0x3F000000,    0x41200000,           0x0,    0xBF800000,
	0xC1100000,           0x0,    0x3EC4BBA1,    0x3EB87C42,
	0x3DD75968,    0x3F000000,    0x41200000,           0x0,
	       0x0,           0x0,           0x0,           0x0,
	       0x0,           0x0,    0x3F000000,    0x41A00000,
	       0x0,           0x0,           0x0,           0x0,
	       0x0,           0x0,           0x0,    0x3F000000,
	0x41A00000,           0x1,           0x0,           0x0,
	       0x0,           0x0,           0x0,           0x0,
	       0,           0
};

u32 D_80034E0C[] = {
	       0x0,           0x0,           0x0,           0x0,
	       0x0,           0x0,           0x0,    0x3F000000,
	0x41000000,           0x0,           0x0,           0x0,
	       0x0,           0x0,           0x0,           0x0,
	0x3F000000,    0x41000000,           0x0,    0xC1080000,
	0xC0C00000,           0x0,    0x40AF7506,    0x40BAB4B9,
	0x40C2A5C2,    0x3F000000,    0x41000000,           0x0,
	0xC0400000,    0xC0600000,           0x0,    0x3ECE08F2,
	0x40B75721,    0x40B62409,    0x3F000000,    0x41000000,
	       0x0,    0xBF000000,    0xC1080000,           0x0,
	0x3F9DFD7A,    0x40B768CD,    0x40B37BDF,    0x3F000000,
	0x41000000,           0x0,    0x40E00000,    0xC1E40000,
	0xBFC00000,    0x3FA74949,    0x40B63EBC,    0x40B6443D,
	0x3F000000,    0x41200000,           0x0,    0xBFC00000,
	0xC1100000,           0x0,    0x3D8ADEEC,    0x40C84E72,
	0x3E506749,    0x3F000000,    0x41200000,           0x0,
	       0x0,           0x0,           0x0,           0x0,
	       0x0,           0x0,    0x3F000000,    0x41A00000,
	       0x0,           0x0,           0x0,           0x0,
	       0x0,           0x0,           0x0,    0x3F000000,
	0x41A00000,           0x1,           0x0,           0x0,
	       0x0,           0x0,           0x0,           0x0,
           0x0,           0x0
};

/**
 * Throwing Knife animation for when Z is pressed/held down.
 */
struct Weapon1PTransformKeyframe throwKnifeDrawBackKeyframes[6] = {
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 4.5f}, { 5.576369f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 20.5f}, { 5.26209f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 3.0f, 5.5f}, { 0.031375f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Throwing Knife animation for when Z is released.
 */
struct Weapon1PTransformKeyframe throwKnifeReleaseKeyframes[6] = {
    { 0, { 0.0f, 0.0f, 4.5f}, { 5.576369f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 20.5f}, { 5.26209f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 3.0f, 5.5f}, { 0.031375f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, -20.0f, 18.0f}, { 0.785458f, 0.0f, 0.0f}, 0.5f, 20.0f},
    { 0, { 0.0f, -20.0f, 18.0f}, { 0.785458f, 0.0f, 0.0f}, 0.5f, 20.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Keyframes for the Grenade which is strange since you cannot see it on screen. Perhaps the developers once intended to have a proper first person Grenade throwing animation?
 */
struct Weapon1PTransformKeyframe grenadeThrowKeyframes[6] = {
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 4.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 4.0f},
    { 0, { 10.0f, 12.5f, 17.5f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 4.0f},
    { 0, { 10.0f, 34.5f, 25.5f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 0, { 10.0f, 34.5f, 25.5f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Keyframes for the Timed Mine, but changing the durations has no effect.
 */
struct Weapon1PTransformKeyframe timedMineThrowKeyframes[6] = {
    { 0, { 10.0f, 34.5f, 25.5f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 0, { 10.0f, 34.5f, 25.5f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 0, { 10.0f, 12.5f, 17.5f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Keyframes for the Proximity Mine. Changing the durations does effect the time it takes to throw the mine, although nothing is seen on screen.
 */
struct Weapon1PTransformKeyframe proxMineThrowKeyframes[6] = {
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 4.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 4.0f},
    { 0, { 0.0f, 0.0f, 4.5f}, { 5.576369f, 0.0f, 0.0f}, 0.5f, 4.0f},
    { 0, { 0.0f, 0.0f, 20.5f}, { 5.26209f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 3.0f, 5.5f}, { 0.031375f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Keyframes for the Remote Mine. Changing the durations does not effect the time it takes to throw them,
 * but it does change the time it takes before you can throw another.
 */
struct Weapon1PTransformKeyframe remoteMineThrowKeyframes[7] = {
    { 0, { 0.0f, 0.0f, 4.5f}, { 5.576369f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 20.5f}, { 5.26209f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 3.0f, 5.5f}, { 0.031375f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, -20.0f, 18.0f}, { 0.785458f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 20.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 20.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Slapper attack when the hand starts at the right of the screen then chops downward and to the left.
 */
Weapon1PTransformKeyframe fistMeleeKeyframes1[10] = {
    { 0, {   0.0f,  0.0f, 0.0f }, {      0.0f,      0.0f,      0.0f }, 0.5f, 10.0f },
    { 0, {   0.0f,  0.0f, 0.0f }, {      0.0f,      0.0f,      0.0f }, 0.5f, 10.0f },
    { 0, {   6.0f, 23.0f, 0.0f }, {  5.91572f, 0.085832f, 0.219482f }, 0.5f, 10.0f },
    { 0, {  18.0f, 35.0f, 9.5f }, { 4.998193f, 0.084203f, 0.268954f }, 0.5f, 10.0f },
    { 0, { -20.0f, 25.5f, 4.0f }, { 0.126148f, 0.304284f, 0.548047f }, 0.5f, 10.0 },
    { 0, { -28.0f, -4.0f, 2.0f }, { 0.506821f,  0.51473f, 0.484098f }, 0.5f,  1.0f },
    { 0, { -28.0f, -4.0f, 2.0f }, { 0.506821f,  0.51473f, 0.484098f }, 0.5f,  1.0f },
    { 0, {   0.0f,  0.0f, 0.0f }, {      0.0f,      0.0f,      0.0f }, 0.5f, 20.0f },
    { 0, {   0.0f,  0.0f, 0.0f }, {      0.0f,      0.0f,      0.0f }, 0.5f, 20.0f },
    { 1, {   0.0f,  0.0f, 0.0f }, {      0.0f,      0.0f,      0.0f }, 0.0f,  0.0f }
};

/**
 * Slapper attack when the hand moves to the left of the screen then chops downward and to the right.
 */
Weapon1PTransformKeyframe fistMeleeKeyframes2[10] = {
       { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
       { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
       { 0, { -6.0f, 23.0f, 0.0f}, { 5.08683f, 6.131295f, 5.534376f}, 0.5f, 10.0f},
       { 0, { -18.0f, 35.0f, 9.5f}, { 4.880698f, 0.070396f, 5.53615f}, 0.5f, 10.0f},
       { 0, { 8.0f, 25.5f, 4.0f}, { 0.107213f, 6.062361f, 5.404225f}, 0.5f, 10.0f},
       { 0, { 28.0f, -4.0f, 2.0f}, { 0.107213f, 6.062361f, 5.404225f}, 0.5f, 1.0f},
       { 0, { 28.0f, -4.0f, 2.0f}, { 0.107213f, 6.062361f, 5.404225f}, 0.5f, 1.0f},
       { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 20.0f},
       { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 20.0f},
       { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Sniper swing right to left.
 */
Weapon1PTransformKeyframe sniperMeleeKeyframes1[11] = {
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 9.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, {9.5f, -0.5f, 3.5f}, { 0.291053f, 5.584375f, 6.212358f}, 0.5f, 8.0f},
    { 0, {18.0f, 7.5f, 3.5f}, { 0.439372f, 5.945201f, 5.993666f}, 0.5f, 8.0f},
    { 0, {-9.0f, 8.5f, 5.5f}, { 0.704803f, 0.194459f, 6.168447f}, 0.5f, 7.0f},
    { 0, {-29.0f, -5.5f, 5.5f}, { 2.281831f, 1.106353f, 1.489998f}, 0.5f, 7.0f},
    { 0, {-57.5f, -27.5f, 5.5f}, { 2.281831f, 1.106353f, 1.489998f}, 0.5f, 7.0f},
    { 0, {-19.5f, -20.0f, 5.5f}, { 1.22519f, 0.726087f, 1.210713f}, 0.5f, 15.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 20.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 20.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Sniper swing left to right.
 */
Weapon1PTransformKeyframe sniperMeleeKeyframes2[11] = {
    { 0, {   0.0f,  0.0f,  0.0f }, {                 0.0f,                 0.0f,                 0.0f }, 0.5f,  9.0f },
    { 0, {   0.0f,  0.0f,  0.0f }, {                 0.0f,                 0.0f,                 0.0f }, 0.5f,  8.0f },
    { 0, { -15.5f,  0.5f, 15.0f }, {  0.9344959855079651f,  0.6256099939346313f,  0.2237969934940338f }, 0.5f,  8.0f },
    { 0, { -23.0f,  2.0f, 12.0f }, {  1.8016400337219238f,  0.9494050145149231f,  0.6307389736175537f }, 0.5f,  8.0f },
    { 0, { -18.0f, -0.5f,  4.0f }, {  0.8478249907493591f,  0.9247649908065796f, 0.07744300365447998f }, 0.5f,  7.0f },
    { 0, {  10.5f,  5.0f,  2.5f }, { 0.22940599918365479f, 0.24570399522781372f, 0.09906300157308578f }, 0.5f,  7.0f },
    { 0, {  18.0f,  5.0f,  2.5f }, { 0.03281300142407417f,    6.20933723449707f,  0.1350640058517456f }, 0.5f,  7.0f },
    { 0, {   9.5f,  3.5f, -1.5f }, {   6.273238182067871f,   6.005795001983643f, 0.08971499651670456f }, 0.5f,  7.0f },
    { 0, {   0.0f,  0.0f,  0.0f }, {                 0.0f,                 0.0f,                 0.0f }, 0.5f, 20.0f },
    { 0, {   0.0f,  0.0f,  0.0f }, {                 0.0f,                 0.0f,                 0.0f }, 0.5f, 20.0f },
    { 1, {   0.0f,  0.0f,  0.0f }, {                 0.0f,                 0.0f,                 0.0f }, 0.0f,  0.0f },
};

/**
 * Animation when the Taser is lowering to fire position.
 */
Weapon1PTransformKeyframe taserFireKeyFrames[6] = {
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.5f, -6.0f, -8.0f}, { 0.439468f, 0.278829f, 0.195178f}, 0.5f, 8.0f},
    { 0, { -2.0f, -8.0f, -10.0f}, { 1.101655f, 0.460753f, 0.570961f}, 0.5f, 8.0f},
    { 0, { -2.0f, -8.0f, -10.0f}, { 1.101655f, 0.460753f, 0.570961f}, 0.5f, 8.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Animation once the Taser has fired and goes back to idle position.
 */
Weapon1PTransformKeyframe taserRaiseKeyframes[6] = {
    {0, { -2.0f, -8.0f, -10.0f}, {1.101655f, 0.460753f, 0.570961f}, 0.5f, 8.0f},
    {0, { -2.0f, -8.0f, -10.0f}, {1.101655f, 0.460753f, 0.570961f}, 0.5f, 8.0f},
    {0, { 0.5f, -6.0f, -8.0f}, {0.439468f, 0.278829f, 0.195178f}, 0.5f, 8.0f},
    {0, { 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    {0, { 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    {1, { 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

coord3d D_80035C40 = {0.0f, 0.0f, 0.0f};
coord3d D_80035C4C = {0.0f, 0.0f, 0.0f};
coord3d D_80035C58 = {0.0f, 0.0f, -1.0f};
coord3d D_80035C64 = {0.0f, 1.0f, 0.0f};
coord3d D_80035C70 = {6.2536321f, 6.2592888f, 0.204238f};
coord3d D_80035C7C = {0.25044999f, 0.90482301f, 0.28716999f};
coord3d D_80035C88 = {1.715736f, 0.37460899f, 0.92193699f};

//D:80035C94
f32 D_80035C94 = 0;


//D:80035C98
Vtx D_80035C98 = {0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff };
//D:80035CA8
coord3d D_80035CA8 = { 0.0f, 0.0f, 0.0f };
//D:80035CB4
coord3d D_80035CB4 = { 0.0f, 0.0f, 0.0f };
//D:80035CC0
u32 D_80035CC0 = 0;



//D:80035CC4
u32 D_80035CC4[] =                      { 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,           0,  0};
/* ModelRenderData D_8002CCBC = {NULL,
                                TRUE,
                                0x00000003,
                                NULL,
                                NULL,
                                0,
                                0,
                                0,
                                0,
                                0,
                                0,
                                0,
                                0,
                                {0, 0, 0, 0},
                                {0, 0, 0, 0},
                                CULLMODE_BOTH};
*/
//D:80035D00
u32 D_80035D00 = 0;
//D:80035D04
u32 D_80035D04[] = {1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
//D:80035D44
u32 watchControllerButtonBases[] = {
	1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

//D:0x80035E04
struct RicochetSoundsSmall ricochet_sounds_small = {
    RICO_8_AFDM_A_SFX, RICO_8_AFDM_B_SFX, RICO_8_AFDM_C_SFX, RICO_8_AFDM_D_SFX,
    RICO_8_AFDM_A_SFX, RICO_8_AFDM_B_SFX, RICO_8_AFDM_C_SFX, RICO_8_AFDM_D_SFX,
    RICO_8_AFDM_A_SFX, RICO_8_AFDM_B_SFX, RICO_8_AFDM_C_SFX, RICO_8_AFDM_D_SFX,
    RICO_5_A_SFX,      RICO_5_B_SFX,      RICO_5_C_SFX,      RICO_5_D_SFX,
    RICO_6_HBBA_A_SFX, RICO_6_HBBA_B_SFX, RICO_6_HBBA_C_SFX, RICO_6_HBBA_D_SFX
};

//D:80035E2C
struct PunchSounds punch_sounds = {
    PUNCH1_SFX,
    PUNCH2_SFX,
    PUNCH3_SFX
};

//D:80035E34
struct BulletFleshSounds bullet_flesh_sounds = {
    HIT_BULLET_FLESH_SFX,
    HIT_BULLET_FLESH_SFX
};

struct LaserRichochetSounds laser_ricochet_sounds = {
    RICO_LASER2_SFX,
    RICO_LASER3_SFX
};

struct RicochetSoundsLarge ricochet_sounds_large = {
	RICO_12_GBU_A_SFX, RICO_12_GBU_B_SFX, RICO_12_GBU_C_SFX, RICO_12_GBU_D_SFX,
    RICO_6_TAJ_A_SFX,  RICO_6_TAJ_B_SFX,  RICO_6_TAJ_C_SFX,  RICO_6_TAJ_D_SFX,
    RICO_6_TAJ_A_SFX,  RICO_6_TAJ_B_SFX,  RICO_6_TAJ_C_SFX,  RICO_6_TAJ_D_SFX,
    RICO_6_TAJ_A_SFX,  RICO_6_TAJ_B_SFX,  RICO_6_TAJ_C_SFX,  RICO_6_TAJ_D_SFX,
    RICO_4_A_SFX,      RICO_4_B_SFX,      RICO_4_B_SFX,      RICO_4_C_SFX,
    RICO_4_A_SFX,      RICO_4_B_SFX,      RICO_4_B_SFX,      RICO_4_C_SFX,
    RICO_4_A_SFX,      RICO_4_B_SFX,      RICO_4_B_SFX,      RICO_4_C_SFX,
    RICO_5_A_SFX,      RICO_5_B_SFX,      RICO_5_C_SFX,      RICO_5_D_SFX,
    RICO_6_HBBA_A_SFX, RICO_6_HBBA_B_SFX, RICO_6_HBBA_C_SFX, RICO_6_HBBA_D_SFX
};

//D:80035E84
struct EarWhistleSounds ear_whistle_sounds = {
    RICO_EAR_WHISTLE1_SFX,
    RICO_EAR_WHISTLE2_SFX,
    RICO_EAR_WHISTLE3_SFX,
    RICO_EAR_WHISTLE4_SFX,
    RICO_EAR_WHISTLE5_SFX
};

//D:80035E90
struct sfx2 watchlaser_fire_sounds = { RICO_LASER2_SFX, RICO_LASER3_SFX };
//D:80035E94
struct sfx3 knife_throw_sounds = { KNIFE_THROW1_SFX, KNIFE_THROW2_SFX, KNIFE_THROW3_SFX };
//D:80035E9C
struct gun_trigger_state g_ZeroTriggerState = { 0, 0 };
//D:80035EA0
//u32 D_80035EA0 = 0;
//D:80035EA4
u32 D_80035EA4 = 0;
//D:80035EA8
u32 D_80035EA8 = 0;
//D:80035EAC
u32 D_80035EAC = 0;
//D:80035EB0
u32 g_DefaultCasingModelRenderData[] = {0, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
//D:80035EEC
u32 dword_D_80035EEC = 0; // Unused

//D:80035EF0
#define AMMO_RELATED_MAX 30
AmmoStats ammo_related[AMMO_RELATED_MAX] = {
    { 0x0    , 0x00000000,   0.0f, },
    { 0x320  , 0x02000C84,   0.0f, },
    { 0xC8   , 0x00000000,   0.0f, },
    { 0x190  , 0x02000C90,  -2.0f, },
    { 0x64   , 0x02000C9C,   0.0f, },
    { 0xC    , 0x02000CD8,   0.0f, },
    { 0x3    , 0x02000CC0,  -2.0f, },
    { 0xA    , 0x02000CFC,   1.0f, },
    { 0xA    , 0x02000D14,   1.0f, },
    { 0xA    , 0x02000D08,   1.0f, },
    { 0xA    , 0x02000CA8,   0.0f, },
    { 0xC    , 0x02000CB4,   0.0f, },
    { 0xC8   , 0x02000CE4,   0.0f, },
    { 0x64   , 0x02000CF0,   0.0f, },
    { 0x32   , 0x00000000,   0.0f, },
    { 0xA    , 0x00000000,   0.0f, },
    { 0x2    , 0x00000000,   0.0f, },
    { 0x8    , 0x00000000,   0.0f, },
    { 0x6    , 0x00000000,   0.0f, },
    { 0xA    , 0x00000000,   0.0f, },
    { 0xA    , 0x00000000,   0.0f, },
    { 0xA    , 0x00000000,   0.0f, },
    { 0x1    , 0x00000000,   0.0f, },
    { 0xA    , 0x00000000,   0.0f, },
    { 0x3E8  , 0x00000000,   0.0f, },
    { 0xA    , 0x00000000,   0.0f, },
    { 0xA    , 0x00000000,   0.0f, },
    { 0xA    , 0x00000000,   0.0f, },
    { 0x32   , 0x02000D20,  -1.0f, },
    { 0x1    , 0x00000000,   0.0f, },
};

//was previously attached to ammo_related[] (array at D:80035EF0)
//D:80036058
u16 D_80036058[] = { 0, 0, 0, 0, };

extern struct ModelSkeleton skeleton_gun_kf7;

//i may belong to objecthandler.c
// D:80036060 canonically freedist
struct ModelHitEntry *g_ModelHitFreeList = NULL;

typedef struct ModelHeader {
            s16                unk00;
            s16                Type;
            struct ChrRecord  *chr;
            ModelFileHeader   *obj;
            RenderPosView     *render_pos;
            union ModelRwData **datas;
            f32               scale;
            struct Model     *attachedto;
            ModelNode        *attachedto_objinst;
} ModelHeader;


// forward declarations

void bullet_path_from_screen_center(coord3d* arg0, coord3d* arg1, enum GUNHAND arg2);
void gunInitProjectileFromPlayer(ObjectRecord *obj, coord3d *targetpos, Mtxf *arg2, coord3d *velocity, Mtxf *arg4);
s32 gunSample1PTransform(Weapon1PTransformKeyframe *keyframes, f32 time, Mtxf *matrix, GUNHAND hand);
void analyzeGEKey(void);
void give_weapon_case_items(void);
struct ModelFileHeader * get_ptr_weapon_model_header_line(ITEM_IDS weapon);
s32 get_ammo_in_hands_weapon(enum GUNHAND hand);
s32 get_ammo_type_for_weapon(ITEM_IDS weapon);
f32 gunSetHorizontalOffset(GUNHAND hand);
void give_weapon_case_items(void);
void sub_GAME_7F05DA8C(GUNHAND hand, ITEM_IDS weaponnum_watchmenu);
void sub_GAME_7F05E808(GUNHAND hand);
void sub_GAME_7F0649D8(enum GUNHAND hand);
void gunCreateBeamForHand(enum GUNHAND hand);
CasingRecord* casingCreate(ModelFileHeader* header, Mtxf* mtx);
void sub_GAME_7F068508(GUNHAND handnum, f32 floor_y_pos);
Vtx *dynAllocateVertices(s32 count);
Mtx *dynAllocateMatrix(void);
void divide3DCoordinates(coord3d *in, f32 divisor, coord3d *out);

// end forward declarations

// current debug keyframes
#define DEB_KEYFRAMES sniperMeleeKeyframes2


void set_cartridges_eject(u32 uParm1)
{
    cartridges_eject = uParm1;
}


u32 get_cartridges_eject(void)
{
    return cartridges_eject;
}


// Unreferenced debPrintKeyframe
// Address: ~ 7F05C538
void nullsub_73(void)
{
#ifdef DEBUG
    osSyncPrintf("\t{");
    osSyncPrintf("0");
    osSyncPrintf(",{%ff,%ff,%ff}", DEB_KEYFRAMES[g_gunDebKeyframeIndex].pos.x, DEB_KEYFRAMES[g_gunDebKeyframeIndex].pos.y, DEB_KEYFRAMES[g_gunDebKeyframeIndex].pos.z);
    osSyncPrintf(",{%ff,%ff,%ff}", DEB_KEYFRAMES[g_gunDebKeyframeIndex].rot.x, DEB_KEYFRAMES[g_gunDebKeyframeIndex].rot.y, DEB_KEYFRAMES[g_gunDebKeyframeIndex].rot.z);
    osSyncPrintf(",0.5f,20.0f");
    osSyncPrintf("},\n");
#endif
    return;
}


// Unreferenced - force keyframe to position
// Address: 7F05C540
void sub_GAME_7F05C540(coord3d* pos)
{
    Weapon1PTransformKeyframe* temp_v0;

    temp_v0 = &DEB_KEYFRAMES[g_gunDebKeyframeIndex];
    temp_v0->pos.x += pos->x;
    temp_v0->pos.y += pos->y;
    temp_v0->pos.z += pos->z;
}


// Unreferenced
void sub_GAME_7F05C594(Mtxf* mtxf)
{
    Mtxf sp18;
    matrix_4x4_set_rotation_around_xyz(&DEB_KEYFRAMES[g_gunDebKeyframeIndex].rot, &sp18);
    matrix_4x4_multiply_in_place(mtxf, &sp18);
    matrix_4x4_get_rotation_around_xyz(&sp18, &DEB_KEYFRAMES[g_gunDebKeyframeIndex].rot);
}


void sub_GAME_7F05C614(void)
{
    if (!cartridges_eject) { return; }

    g_CurrentPlayer->hands[0].field_92C = 1;
    matrix_4x4_set_rotation_around_xyz(&DEB_KEYFRAMES[g_gunDebKeyframeIndex].rot, (Mtxf *)&g_CurrentPlayer->hands[0].field_8EC);
    matrix_4x4_set_position(&DEB_KEYFRAMES[g_gunDebKeyframeIndex].pos, (Mtxf *)&g_CurrentPlayer->hands[0].field_8EC);
    cartridges_eject = 0;
}


// Unreferenced increment keyframe index, loop back to start if final keyframe is reached
// Address: 7F05C6B8
void gunDebAdvanceKeyframe(void)
{
    g_gunDebKeyframeIndex++;
    if (DEB_KEYFRAMES[g_gunDebKeyframeIndex].isFinalKey & 1)
    {
        g_gunDebKeyframeIndex = 0;
    }
}


/**
 * Address: 7F05C6FC
 * 
 * Sample a first person weapon transform keyframe animation at `time` and
 * write the interpolated transform into `matrix`
 * 
 * @returns 1 when an in-progress frame was interpolated, 0 when `time` reached
 * the final keyframe and the static end pose was written.
 */
s32 gunSample1PTransform(Weapon1PTransformKeyframe *keyframes, f32 time, Mtxf *matrix, GUNHAND hand)
{
    Weapon1PTransformKeyframe *current;
    f32 frac;
    f32 tangent;
    coord3d posResult;
    quatf qResult;
    quatf q0;
    quatf q1;
    quatf q2;
    quatf q3;
    s32 i;

    i = 1;
    frac = keyframes[1].duration;
    current = keyframes + i;

    while (time >= current->duration)
    {
        time -= current->duration;
        i++;
        current++;

        if (current[2].isFinalKey & 1)
        {
            break;
        }
    }

     // Comma operator is intentional: nudges IDO to emit addu s0,a0,t8.
    current = (0, keyframes) + i;

    i = 2;

    if (current[i].isFinalKey & 1)
    {
        matrix_4x4_set_rotation_around_xyz(&current->rot, matrix);
        matrix_4x4_set_position(&current->pos, matrix);
        return 0;
    }

    frac = time / current->duration;
    tangent = current->interpParam;

    quaternion_set_rotation_around_xyzf(&current[-1].rot, &q0);
    quaternion_set_rotation_around_xyzf(&current->rot, &q1);
    quaternion_set_rotation_around_xyzf(&current[1].rot, &q2);
    quaternion_set_rotation_around_xyzf(&current[2].rot, &q3);

    quaternion_ensure_shortest_path(&q1, &q2);
    quaternion_ensure_shortest_path(&q2, &q3);
    quaternion_ensure_shortest_path(&q1, &q0);

    quaternion_7F05C2F0(&q0, &q1, &q2, &q3, frac, &qResult);

    coord3dCubicSplineInterp(&current[-1].pos, &current->pos, &current[1].pos, &current[2].pos, frac, tangent, &posResult);

    if (hand == GUNLEFT)
    {
        posResult.x = -posResult.x;
        qResult[0] = -qResult[0];
        qResult[1] = -qResult[1];
    }

    quaternion_to_matrix(&qResult, matrix);
    matrix_4x4_set_position(&posResult, matrix);

    return 1;
}


WeaponStats *get_ptr_item_statistics(ITEM_IDS item)
{
    if (gitem_structs[item].has_no_model == 0)
    { /* weapon has model, return stats struct */
        return gitem_structs[item].item_weapon_stats;
    }
    return &default_weaponstats; /* no model, return defaults */
}




void copy_item_in_hand(coord3d *pos)
{
    ITEM_IDS item;
    WeaponStats *stats;

    item = getCurrentPlayerWeaponId(0);
    stats = get_ptr_item_statistics(item);

    pos->x = stats->PosX;
    pos->y = stats->PosY;
    pos->z = stats->PosZ;
}


void copy_item_in_hand_to_main_list(coord3d *pos) {

    WeaponStats *stats;
    ITEM_IDS item;

    item = getCurrentPlayerWeaponId(0);
    stats = get_ptr_item_statistics(item);

    stats->PosX = pos->x;
    stats->PosY = pos->y;
    stats->PosZ = pos->z;
}


void bgunCalculateBlend(enum GUNHAND handnum)
{
    s32 sp60[2];
    s32 sp58[2];
    f32 mult = get_ptr_item_statistics(getCurrentPlayerWeaponId(handnum))->Sway;

    sp60[handnum] = (g_CurrentPlayer->hands[handnum].curblendpos + 2) % 4;
    sp58[handnum] = (g_CurrentPlayer->hands[handnum].curblendpos + 1) % 4;
    g_CurrentPlayer->hands[handnum].curblendpos = sp58[handnum];

    g_CurrentPlayer->hands[handnum].blendlook[sp60[handnum]].x = (RANDOMFRAC() - 0.5f) * 0.08f * mult;
    g_CurrentPlayer->hands[handnum].blendlook[sp60[handnum]].y = (RANDOMFRAC() - 0.5f) * 0.1f * mult;
    g_CurrentPlayer->hands[handnum].blendlook[sp60[handnum]].z = -1;

    g_CurrentPlayer->hands[handnum].blendup[sp60[handnum]].x = (RANDOMFRAC() - 0.5f) * 0.1f * mult;
    g_CurrentPlayer->hands[handnum].blendup[sp60[handnum]].y = 1;
    g_CurrentPlayer->hands[handnum].blendup[sp60[handnum]].z = (RANDOMFRAC() - 0.5f) * 0.1f * mult;

    g_CurrentPlayer->hands[handnum].blendpos[sp60[handnum]].x = (RANDOMFRAC() * 0.75f) + 1.5f;
    g_CurrentPlayer->hands[handnum].blendpos[sp60[handnum]].y = (2 + RANDOMFRAC()) * g_CurrentPlayer->hands[handnum].blendscale1;
    g_CurrentPlayer->hands[handnum].blendpos[sp60[handnum]].z = (RANDOMFRAC() - 0.5f) * 2.5f;

    if (g_CurrentPlayer->hands[handnum].sideflag < 0)
    {
        g_CurrentPlayer->hands[handnum].blendpos[sp60[handnum]].x *= -1;

        if (g_CurrentPlayer->hands[handnum].sideflag == -2)
        {
            g_CurrentPlayer->hands[handnum].sideflag = 1;
        }
        else
        {
            g_CurrentPlayer->hands[handnum].sideflag = -2;
        }
    }
    else
    {
        if (g_CurrentPlayer->hands[handnum].sideflag == 2)
        {
            g_CurrentPlayer->hands[handnum].sideflag = -1;
        }
        else
        {
            g_CurrentPlayer->hands[handnum].sideflag = 2;
        }
    }

    g_CurrentPlayer->hands[handnum].blendscale1 = -g_CurrentPlayer->hands[handnum].blendscale1;
}


s32 Gun_hand_without_item(enum GUNHAND arg0)
{
    return g_CurrentPlayer->hand_invisible[arg0] > 0
        || (g_CurrentPlayer->hand_item[arg0] == 0 && g_CurrentPlayer->field_2A44[arg0] < 0);
}


s32 get_itemtype_in_hand(GUNHAND hand)
{
    return g_CurrentPlayer->hand_item[hand];
}


ModelFileHeader *get_ptr_itemheader_in_hand(GUNHAND hand)
{
    return &g_CurrentPlayer->copy_of_body_obj_header[hand];
}


u8 * getPlayerWeaponBufferForHand(GUNHAND hand)
{
    return g_CurrentPlayer->ptr_hand_weapon_buffer[hand];
}


u32 getSizeBufferWeaponInHand(s32 hand)
{
    return size_item_buffer[hand];
}


void remove_item_in_hand(GUNHAND hand)
{
  g_CurrentPlayer->hand_invisible[hand] = 0;
  g_CurrentPlayer->hand_item[hand] = ITEM_UNARMED;
  g_CurrentPlayer->field_2A44[hand] = -1;
  g_CurrentPlayer->lock_hand_model[hand] = 1;
  return;
}


void place_item_in_hand_swap_and_make_visible(GUNHAND hand, ITEM_IDS item)
{
    if (g_CurrentPlayer->lock_hand_model[hand]) { return; }

    if (g_CurrentPlayer->hand_invisible[hand] >= 0)
    {
        if (item != g_CurrentPlayer->hand_item[hand])
        {
            g_CurrentPlayer->hand_invisible[hand] = -1;
            g_CurrentPlayer->field_2A44[hand] = item;
        }
        return;
    }

    if (item != g_CurrentPlayer->hand_item[hand])
    {
        g_CurrentPlayer->field_2A44[hand] = item;
        return;
    }

    g_CurrentPlayer->hand_invisible[hand] = 1;
}


char *get_ptr_item_text_call_line(ITEM_IDS item)
{
    if (item == ITEM_FIST)
    {
        item = g_CurrentPlayer->cur_item_weapon_getname;
    }
    return gitem_structs[item].item_file_name;
}


 ModelFileHeader *get_ptr_weapon_model_header_line(ITEM_IDS weapon)
{
    if (weapon == ITEM_FIST)
    {
        weapon = g_CurrentPlayer->cur_item_weapon_getname;
    }
    return gitem_structs[weapon].item_header;
}


int getCurrentWeaponOrItem(void)
{
    return g_CurrentPlayer->cur_item_weapon_getname;
}


#ifdef GEVR
#include <stdlib.h>
#include "gevr_gexmodel.h"
#include "gevr_gexweapon.h"
#include "net_game.h"
extern bool netIsActive(void);   /* net_core.c */
extern int netGetLocalSlot(void);
#include "gevr_pdanim.h"
#include "system.h"
extern int VrGexGuns;   /* goldeneye-vr.ini GexGuns: GoldenEye X's guns (docs/gex-weapons.md) */
extern s32 g_gevrHandPatchSkip;   /* gevr_handpatch.c: the hand shells are GoldenEye's model's */

/*
 * GoldenEye X's remote mines (user): GE-X holds the watch in the left hand and
 * the mines in the right, and detonates one-handed from the watch, so with its
 * models on the mines are one weapon. The gun hand's trigger throws, the off
 * hand's detonates (port/src/input.c), the Detonator leaves the weapon lists,
 * and the empty hand stays on the mines while any of this player's are out.
 */
static s32 s_gevrGexDetonate;   /* the off hand's trigger asked; the gun hand's tick detonates */

/* any of the current player's remote mines thrown or stuck */
s32 gevrGexRemoteMinesOut(void)
{
    PropRecord *prop;
    s32 me = get_cur_playernum();

    for (prop = chrpropGetActiveTail(); prop != NULL; prop = prop->prev)
    {
        if (prop->type == PROP_TYPE_WEAPON && prop->weapon != NULL && prop->weapon->weaponnum == ITEM_REMOTEMINE
            && RUNTIME_OWNER(prop->weapon->runtime_bitflags) == me)
        {
            return TRUE;
        }
    }
    return FALSE;
}

/* the remote mines are out in the gun hand and detonate from the watch */
s32 gevrGexMineDetonates(void)
{
    return VrGexGuns && g_CurrentPlayer != NULL && !g_CurrentPlayer->bonddead
        && getCurrentPlayerWeaponId(GUNRIGHT) == ITEM_REMOTEMINE
        && getCurrentPlayerWeaponId(GUNLEFT) == ITEM_UNARMED
        && bondinvItemAvailable(ITEM_TRIGGER);
}

/* input.c: the off hand's trigger, pressed */
void gevrGexDetonateRequest(void)
{
    s_gevrGexDetonate = TRUE;
}

/* gunfire.c, the gun hand's tick: the request, as the Detonator's own trigger
 * would (chrprop.c chraiCheckUseHeldItem) */
void gevrGexDetonateTick(void)
{
    if (!s_gevrGexDetonate || (netIsActive() && get_cur_playernum() != netGetLocalSlot()))
    {
        return;   /* this headset's player only: a copy's mines go off on its owner's word */
    }
    s_gevrGexDetonate = FALSE;
    if (gevrGexMineDetonates())
    {
        trigger_remote_mine_detonation();
        sysLogPrintf(LOG_NOTE, "gex: remote mines detonated from the watch");
    }
}

/* the hand empty of mines stays on them while any are out (no Detonator to go to) */
s32 gevrGexMinesHold(GUNHAND hand)
{
    return VrGexGuns && hand == GUNRIGHT && getCurrentPlayerWeaponId(GUNRIGHT) == ITEM_REMOTEMINE
        && bondinvItemAvailable(ITEM_TRIGGER) && gevrGexRemoteMinesOut();
}

/* GoldenEye X's watch rig (WATCH_DEF): both arms are its own */
s32 gevrGexWatchItem(s32 item)
{
    const GexWeaponDef *def = VrGexGuns ? gevrGexWeaponGet(item) : NULL;

    return def != NULL && def->watch;
}

/*
 * gunfire.c, the gun hand's tick: a watch item in the gun hand with GE-X's
 * rig, whose raised left arm wears the watch, takes the left hand's gun
 * away (user: a gun in the left hand as well drew three arms, in 2D after
 * the headset).
 */
void gevrGexWatchHandsTick(void)
{
    extern void gunRequestHandWeaponChange(enum GUNHAND hand, s32 nextWeapon, s32 cycleDirection);   /* below */
    s32 st;

    if (g_CurrentPlayer == NULL || g_CurrentPlayer->bonddead
        || (netIsActive() && get_cur_playernum() != netGetLocalSlot())
        || !gevrGexWatchItem(getCurrentPlayerWeaponId(GUNRIGHT))
        || getCurrentPlayerWeaponId(GUNLEFT) == ITEM_UNARMED)
    {
        return;
    }
    st = g_CurrentPlayer->hands[GUNLEFT].weapon_action_state;
    if (st != GUN_ANIM_STATE_SWITCH_LOWER && st != GUN_ANIM_STATE_SWITCH_SWAP)
    {
        gunRequestHandWeaponChange(GUNLEFT, ITEM_UNARMED, 1);
        sysLogPrintf(LOG_NOTE, "gex: watch item in the gun hand, the left hand emptied");
    }
}

/*
 * gunfire.c's watch-page offsets (the weapon panel, the watch's pages): a GE-X
 * model centred on its own mesh (GexWeaponDef panelFit) spins about the
 * origin, not GoldenEye's mesh's place (user: they swung down and to the
 * side); the tank's prop model the same.
 */
s32 gevrGexPanelCentred(s32 item)
{
    const GexWeaponDef *def = VrGexGuns ? gevrGexWeaponGet(item) : NULL;

    return item == ITEM_TANKSHELLS || (def != NULL && def->panelFit[0] > 0.0f);
}

/* each player's hands whose model is GoldenEye X's (gevrGexGunPrepare) */
static const GexWeaponDef *s_gevrGexHand[MAX_PLAYER_COUNT][2];
static void gevrGexResetModel(s32 hand, s32 rebuildCache);

static void gevrGexHandSet(GUNHAND hand, const GexWeaponDef *def)
{
    s32 p = get_cur_playernum();

    if (p >= 0 && p < MAX_PLAYER_COUNT && (hand == GUNRIGHT || hand == GUNLEFT))
    {
        gevrGexResetModel(hand, def != NULL);
        gevrGexReloadReset(hand);
        s_gevrGexHand[p][hand] = def;
    }
}

/* the current player's hand holds a GoldenEye X model: gunfire.c poses it,
 * bondview2.c and input.c give it its own gun fit */
s32 gevrGexHeld(s32 hand)
{
    s32 p = get_cur_playernum();

    return p >= 0 && p < MAX_PLAYER_COUNT && (hand == GUNRIGHT || hand == GUNLEFT) && s_gevrGexHand[p][hand] != NULL;
}

const GexWeaponDef *gevrGexWeaponForHand(s32 hand)
{
    s32 p = get_cur_playernum();
    return p >= 0 && p < MAX_PLAYER_COUNT && hand >= 0 && hand < 2 ? s_gevrGexHand[p][hand] : NULL;
}

/* gunfire.c: this hand's model is GE-X's, for this item (it has its own hand) */
s32 gevrGexShowsItem(s32 hand, s32 item)
{
    const GexWeaponDef *def = gevrGexWeaponForHand(hand);
    return def != NULL && def->item == item;
}

/* bondview2.c gevrStereoItemShown: either hand draws this item as GE-X's, not as a gadget */
s32 gevrGexDrawsItem(s32 item)
{
    return gevrGexShowsItem(GUNRIGHT, item) || gevrGexShowsItem(GUNLEFT, item);
}

/* a GE-X item the hand simply holds - knife, grenade, mine: no ammo payload, no
 * fire clip. In the headset the player's own hand swings and throws it. */
static s32 gevrGexIsHandHeld(const GexWeaponDef *def)
{
    return def != NULL && def->magMatrix < 0 && def->fireAnim == 0;
}

s32 gevrGexHandHeld(s32 hand)
{
    return gevrGexIsHandHeld(gevrGexWeaponForHand(hand));
}

/* a single-use item (throwable) that has left the hand: GoldenEye hid the
 * whole model; GE-X's hides the item alone and keeps the hand */
static s32 gevrGexItemSpent(const GexWeaponDef *def, GUNHAND hand)
{
    return g_CurrentPlayer != NULL && (hand == GUNRIGHT || hand == GUNLEFT)
        && g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine <= 0
        && bondwalkItemCheckBitflags(def->item, WEAPONSTATBITFLAG_SINGLE_USE_RELOAD);
}

static void gevrGexCollapse(Mtxf *m)
{
    s32 r, c;

    for (r = 0; r < 3; r++)
    {
        for (c = 0; c < 3; c++)
        {
            m->m[r][c] = 0.0f;
        }
    }
}

static ModelFileHeader *s_gevrGexPanelHdr;            /* the weapon panel's (below) */
static const GexWeaponDef *s_gevrGexPanelDef;

static const GexWeaponDef *gevrGexForHeader(ModelFileHeader *hdr)
{
    s32 hand;
    if (hdr != NULL && hdr == s_gevrGexPanelHdr && s_gevrGexPanelDef != NULL) return s_gevrGexPanelDef;
    for (hand = 0; hand < 2; hand++)
        if (hdr == &g_CurrentPlayer->copy_of_body_obj_header[hand]) return gevrGexWeaponForHand(hand);
    return gevrGexWeaponForHand(GUNRIGHT);
}

/* Weapon definitions own the appended part switches and texture mapping.
 * Original GoldenEye switch indices stay clear for cuffs and gun logic. */
static void gevrGexGunPrepare(GUNHAND hand, ITEM_IDS item, ModelFileHeader *hdr)
{
    s32 parts[64];
    u32 len = 0;
    u16 mtx = 0, tex = 0;
    s32 i;
    const s32 n = hdr->numSwitches;
    const GexWeaponDef *def = gevrGexWeaponGet(item);

    if (!VrGexGuns || def == NULL || n + def->numParts > 64)
    {
        return;
    }
    for (i = 0; i < 64; i++)
    {
        parts[i] = -1;
    }
    if (n > 1)
    {
        parts[1] = 90;
    }
    for (i = 0; i < def->numParts; i++) parts[n + i] = def->parts[i];
    gevrGexPendingFile = gevrGexBuildModel(def->model, n + def->numParts, parts, def->texturePairs, &len, &mtx, &tex);
    if (gevrGexPendingFile != NULL)
    {
        for (i = 0; i < def->numParts; i++)
        {
            const u8 *sw = gevrGexPendingFile + 4*(n+i);
            if (def->parts[i] >= 0 && !(sw[0] | sw[1] | sw[2] | sw[3]))
            {
                sysLogPrintf(LOG_ERROR, "gex: %s missing configured part %d", def->model, def->parts[i]);
                free(gevrGexPendingFile);
                gevrGexPendingFile = NULL;
                break;
            }
        }
    }
    if (gevrGexPendingFile && (mtx > 64 || mtx <= def->magMatrix || mtx <= def->heldMatrix
        || len > D_80032464[hand] || gevrPdAnimNumFrames(gevrGexRestAnim(def)) <= 0
        || (def->reload.anim > 0 && gevrPdAnimNumFrames(def->reload.anim) <= 0)
        || (def->holdAnim > 0 && gevrPdAnimNumFrames(def->holdAnim) <= 0)
        || (def->fireAnimAlt > 0 && gevrPdAnimNumFrames(def->fireAnimAlt) <= 0)))
    {
        sysLogPrintf(LOG_ERROR, "gex: %s exceeds gun limits or animation unavailable", def->model);
        free(gevrGexPendingFile);
        gevrGexPendingFile = NULL;
    }
    gevrGexHandSet(hand, gevrGexPendingFile != NULL ? def : NULL);
    if (gevrGexPendingFile != NULL)
    {
        gevrGexPendingLen = len;
        hdr->numSwitches = n + def->numParts;
        hdr->numMatrices = mtx;
        hdr->numtextures = tex;
        g_gevrHandPatchSkip = TRUE;
    }
}

/*
 * A GoldenEye X gun plays Perfect Dark's animations (docs/gex-weapons.md;
 * tools/gex/gexguns.py lists each gun's scripts), driven by this hand's
 * state:
 *  - the reload plays its reload animation (the KF7's 1018, 90 frames)
 *    across GoldenEye's reload, with its ammo frame (50) where GoldenEye
 *    moves the ammo (the raise's start), so the game's timing stands; the
 *    magazines swap on the script's frames (18 out, 50 in);
 *  - a burst plays the fire animation (1017) once from the trigger, a
 *    frame a 60th, as Perfect Dark starts it per attack
 *    (bgunTickIncAttackingShoot);
 *  - at rest the gun holds the fire animation's first frame.
 * GoldenEye's own reload tilt stands down for it (gunfire.c).
 */

extern f32 gevrReloadPhase(GUNHAND hand);   /* gunfire.c */

/* each player's hands' fire animation frame, or -1 when it is not playing */
static f32 s_gevrGexFire[MAX_PLAYER_COUNT][2] = {
    { -1.0f, -1.0f }, { -1.0f, -1.0f }, { -1.0f, -1.0f }, { -1.0f, -1.0f },
#if MAX_PLAYER_COUNT > 4
    { -1.0f, -1.0f }, { -1.0f, -1.0f }, { -1.0f, -1.0f }, { -1.0f, -1.0f },
#endif
};
static s32 s_gevrGexFiring[MAX_PLAYER_COUNT][2];
static s32 s_gevrGexReloading[MAX_PLAYER_COUNT][2];
static f32 s_gevrGexReadyFrame[MAX_PLAYER_COUNT][2];

/* Physical insertion readies ammo immediately. PP7 retains GE-X's receiver
 * motion after its ammo frame, without animating either tracked hand. */
void gevrGexMagazineReady(s32 hand)
{
    s32 p = get_cur_playernum();
    const GexWeaponDef *def = gevrGexWeaponForHand(hand);
    if (p >= 0 && p < MAX_PLAYER_COUNT && def != NULL && (def->pistol || def->singleRound) && def->reload.anim > 0)
        s_gevrGexReadyFrame[p][hand] = def->reload.ammoFrame;
}

static void gevrGexFallTick(s32 hand);   /* a dropped magazine's clock, below */

/* gunfire.c gunTickHandState, each tick: the fire animation's clock */
void gevrGexTick(GUNHAND hand, s32 firing, s32 shot)
{
    s32 p = get_cur_playernum();
    f32 *fire;

    if (p < 0 || p >= MAX_PLAYER_COUNT || (hand != GUNRIGHT && hand != GUNLEFT))
    {
        return;
    }
    fire = &s_gevrGexFire[p][hand];
    if (!gevrGexHeld(hand) || gevrGexWeaponForHand(hand)->fireAnim <= 0)
    {
        *fire = -1.0f;
    }
    else if (gevrGexWeaponForHand(hand)->pistol ? shot : firing && !s_gevrGexFiring[p][hand])
    {
        *fire = 0.0f;
    }
    else if (*fire >= 0.0f)
    {
        *fire += g_ClockTimer;
        if (*fire >= gevrPdAnimNumFrames(gevrGexWeaponForHand(hand)->fireAnim) - 1)
        {
            *fire = -1.0f;
        }
    }
    s_gevrGexFiring[p][hand] = firing;
    if (gevrGexHeld(hand) && s_gevrGexReadyFrame[p][hand] >= 0)
    {
        const GexWeaponDef *def = gevrGexWeaponForHand(hand);
        s_gevrGexReadyFrame[p][hand] += g_ClockTimer;
        if (shot || s_gevrGexReadyFrame[p][hand] >= gevrPdAnimNumFrames(def->reload.anim)-1)
            s_gevrGexReadyFrame[p][hand] = -1.0f;
    }
    gevrGexFallTick(hand);
}

/* a joint at a fractional frame, blended between the two either side as
 * Perfect Dark's models are (rotations the short way round) */
static void gevrGexAnimPart(s32 anim, f32 frame, s32 part, f32 rot[3], f32 trans[3])
{
    s32 f0 = (s32) frame;
    f32 t = frame - f0;
    f32 rot1[3], trans1[3], scale[3];
    s32 i;

    gevrPdAnimPart(anim, f0, part, rot, trans, scale);
    if (t <= 0.0f || f0 + 1 >= gevrPdAnimNumFrames(anim))
    {
        return;
    }
    gevrPdAnimPart(anim, f0 + 1, part, rot1, trans1, scale);
    for (i = 0; i < 3; i++)
    {
        f32 d = rot1[i] - rot[i];

        if (d > M_PI_F)
        {
            d -= 2.0f * M_PI_F;
        }
        else if (d < -M_PI_F)
        {
            d += 2.0f * M_PI_F;
        }
        rot[i] += d * t;
        trans[i] += (trans1[i] - trans[i]) * t;
    }
}

/* each joint from its parent (the description below), the root from base */
static void gevrGexPoseWalk(ModelFileHeader *hdr, const Mtxf *base, s32 anim, f32 frame, Mtxf *rwmtx)
{
    ModelNode *node = hdr->RootNode;
    Mtxf offset;

    while (node != NULL)
    {
        if ((node->Opcode & 0xff) == MODELNODE_OPCODE_GROUP)
        {
            ModelRoData_GroupRecord *group = &node->Data->Group;
            ModelNode *up = node->Parent;
            const Mtxf *parent = base;

            while (up != NULL && (up->Opcode & 0xff) != MODELNODE_OPCODE_GROUP)
            {
                up = up->Parent;
            }
            if (up != NULL && up->Data->Group.MatrixID0 < hdr->numMatrices)
            {
                parent = &rwmtx[up->Data->Group.MatrixID0];
            }
            if (group->MatrixID0 >= 0 && group->MatrixID0 < hdr->numMatrices)
            {
                f32 rot[3], trans[3], pos[3];

                gevrGexAnimPart(anim, frame, group->JointID, rot, trans);
                if (up == NULL)
                {
                    pos[0] = trans[0];
                    pos[1] = trans[1];
                    pos[2] = trans[2];
                }
                else
                {
                    pos[0] = trans[0] + group->Origin.x;
                    pos[1] = trans[1] + group->Origin.y;
                    pos[2] = trans[2] + group->Origin.z;
                }
                gevrPdMtxRotTrans(rot, pos, offset.m);
                matrix_4x4_multiply((Mtxf *) parent, &offset, &rwmtx[group->MatrixID0]);
            }
        }

        if (node->Child != NULL)
        {
            node = node->Child;
        }
        else
        {
            while (node != NULL && node->Next == NULL)
            {
                node = node->Parent;
            }
            node = node != NULL ? node->Next : NULL;
        }
    }
}

/*
 * On the screen (user: it did not look right) the gun body sits where
 * GoldenEye's KF7 did. GE-X's KF7 is GoldenEye's mesh on Perfect Dark's
 * skeleton: its vertices are GoldenEye's in the gun joint's frame (33),
 * 22 units lower (measured from both ROMs), where GoldenEye drew them in
 * the gun matrix's own. So the root goes where that joint, at rest, lands
 * on the gun matrix 22 units up: the gun matrix, the 22 units, then the
 * rest pose's joint undone. The animation then moves the gun from there,
 * hands and all. The headset keeps the root on the controller, as
 * Perfect Dark VR does, where Gun fit places it (the user fitted it so).
 */
static void gevrGexRigidInverse(const Mtxf *g, Mtxf *inv);
static void gevrGexMtxPoint(const Mtxf *m, const f32 local[3], f32 out[3]);
/*
 * Screen mode for the hand-held rigs (user: their arms came at the camera,
 * anchored where GoldenEye drew its own item). On the screen a GE-X gun sits as
 * the PP7's does, so the PP7's pose is a virtual controller: its root (the gun
 * joint squared at its screen offset, at the PP7's own screen place, gunfire.c)
 * moved back by its Gun fit. The item's root goes on that controller by its own
 * Gun fit, as in the headset: the difference of the two fits, cm right, up and
 * back, in the root's model units (x the gun's left, z ahead; 0.085 cm a unit).
 * So the headset's fitting places it on the screen too.
 */
static const Mtxf s_gevrGexPp7RestGun = { {
    { 0.99996f, -0.008036f, 0.003984f, 0.0f },
    { 0.00804f, 0.999967f, -0.001007f, 0.0f },
    { -0.003976f, 0.001039f, 0.999992f, 0.0f },
    { 2.799401f, 22.504885f, 131.922856f, 1.0f },
} };   /* GwppkZ's gun joint (33) at its rest, 236's frame 0 */
static const f32 s_gevrGexPp7ScreenOffset[3] = { 24.0f, 26.0f, 77.0f };

static void gevrGexScreenFitAnchor(const f32 fit[3], Mtxf *anchor)
{
    const f32 *pp7 = gevrGexGunFit(ITEM_WPPK);
    Mtxf up, inv, shift, tmp;
    s32 i;

    gevrGexRigidInverse(&s_gevrGexPp7RestGun, &inv);
    matrix_4x4_set_identity(&up);
    matrix_4x4_set_identity(&shift);
    for (i = 0; i < 3; i++)
    {
        up.m[3][i] = s_gevrGexPp7ScreenOffset[i];
    }
    shift.m[3][0] = -(fit[0] - pp7[0]) / 0.085f;
    shift.m[3][1] = (fit[1] - pp7[1]) / 0.085f;
    shift.m[3][2] = -(fit[2] - pp7[2]) / 0.085f;
    matrix_4x4_multiply(&inv, &shift, &tmp);
    matrix_4x4_multiply(&up, &tmp, anchor);
}

static void gevrGexScreenHandAnchor(const GexWeaponDef *def, Mtxf *anchor)
{
    if (def->screenFromPp7)
    {
        /* GE-X's own place beside its PP7, not the headset's fit */
        Mtxf pp7, shift;
        s32 i;

        gevrGexScreenFitAnchor(gevrGexGunFit(ITEM_WPPK), &pp7);
        matrix_4x4_set_identity(&shift);
        for (i = 0; i < 3; i++)
        {
            shift.m[3][i] = def->screenPp7Offset[i];
        }
        matrix_4x4_multiply(&pp7, &shift, anchor);
        return;
    }
    gevrGexScreenFitAnchor(gevrGexGunFit(def->item), anchor);
}

/*
 * gunfire.c, the screen, GoldenEye X's models on: a listed gadget, which the flat
 * game never drew (bondview2.c gevrScreenGadget), held as in the headset on the
 * PP7's virtual controller - the gadget by GoldenEye's own Gun fit and its gadget
 * pose (cm along the model's left, up and forward, 0.085 cm a model unit; a turn;
 * a size), GE-X's hand by the fist's (gevrGexDrawItemHand). m: the gun matrix after
 * the flat game's 0.1, at the PP7's place; it becomes the gadget's.
 */
static Mtxf s_gevrGexScreenRoot;
static s32 s_gevrGexScreenRootTimer = -1;

void gevrGexScreenGadget(s32 item, Mtxf *m)
{
    extern float VrGunOffX, VrGunOffY, VrGunOffZ;   /* vr_settings_defaults.c */
    extern s32 gevrGadgetFitPose(s32 item, f32 ofs[3], f32 rot[3], f32 *scale);   /* bondview2.c */
    const f32 gadgetFit[3] = { VrGunOffX, VrGunOffY, VrGunOffZ };
    f32 ofs[3], rot[3], scale;
    Mtxf anchor, base, turn;
    struct coord3d r;
    s32 i, j;

    s_gevrGexScreenRoot = *m;
    s_gevrGexScreenRootTimer = g_GlobalTimer;
    gevrGexScreenFitAnchor(gadgetFit, &anchor);
    matrix_4x4_multiply(m, &anchor, &base);
    *m = base;
    if (!gevrGadgetFitPose(item, ofs, rot, &scale))
    {
        return;
    }
    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 3; j++)
        {
            m->m[3][j] += ofs[i] / 0.085f * base.m[i][j];
        }
    }
    r.f[0] = rot[0] * (M_PI_F / 180.0f);
    r.f[1] = rot[1] * (M_PI_F / 180.0f);
    r.f[2] = rot[2] * (M_PI_F / 180.0f);
    matrix_4x4_set_rotation_around_xyz(&r, &turn);
    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 3; j++)
        {
            m->m[i][j] = (turn.m[i][0] * base.m[0][j] + turn.m[i][1] * base.m[1][j] + turn.m[i][2] * base.m[2][j]) * scale;
        }
    }
}

/* gunfire.c: this hand's rig takes the PP7's place on the screen */
s32 gevrGexScreenHand(GUNHAND hand)
{
    const GexWeaponDef *def = gevrGexWeaponForHand(hand);

    return def != NULL && def->screenHand;
}

/* gunfire.c, the screen's muzzle (where beams and thrown things start), in the gun matrix's frame */
void gevrGexScreenMuzzle(GUNHAND hand, f32 out[3])
{
    const GexWeaponDef *def = gevrGexWeaponForHand(hand);
    Mtxf anchor;

    if (def == NULL)
    {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    if (!def->screenHand)
    {
        out[0] = def->screenMuzzle[0];
        out[1] = def->screenMuzzle[1];
        out[2] = def->screenMuzzle[2];
        return;
    }
    gevrGexScreenHandAnchor(def, &anchor);
    gevrGexMtxPoint(&anchor, def->muzzle, out);
}

static void gevrGexScreenAnchor(ModelFileHeader *hdr, Mtxf *anchor)
{
    const GexWeaponDef *def = gevrGexForHeader(hdr);
    static Mtxf rest[64];
    Mtxf ident, up, inv;
    const Mtxf *g;
    s32 i, j;

    if (def->screenHand)
    {
        gevrGexScreenHandAnchor(def, anchor);
        return;
    }
    if (def->screenFromRoot)
    {
        matrix_4x4_set_identity(anchor);
        for (i = 0; i < 3; i++) anchor->m[3][i] = def->screenOffset[i];
        return;
    }
    matrix_4x4_set_identity(&ident);
    gevrGexPoseWalk(hdr, &ident, gevrGexRestAnim(def), 0.0f, rest);
    g = &rest[def->gunMatrix];

    /* the joint undone: rotation transposed, translation turned back */
    matrix_4x4_set_identity(&inv);
    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 3; j++)
        {
            inv.m[i][j] = g->m[j][i];
        }
    }
    for (j = 0; j < 3; j++)
    {
        inv.m[3][j] = -(g->m[3][0] * g->m[j][0] + g->m[3][1] * g->m[j][1] + g->m[3][2] * g->m[j][2]);
    }
    matrix_4x4_set_identity(&up);
    for (i=0;i<3;i++) up.m[3][i] = def->screenOffset[i];
    matrix_4x4_multiply(&up, &inv, anchor);
}

/*
 * A GE-X gun's joints. GoldenEye's gun code sets each of its own models'
 * few matrices by hand (gunfire.c); Perfect Dark derives every joint from
 * its parent, its position record and the animation playing (pdvr
 * model.c modelPositionJointUsingVecRot): once the gun code has set the
 * gun's own matrix (rwmtx[0]), each group's matrix is its parent group's
 * (or that gun matrix, for the root) times the animation's rotation at
 * the joint's offset plus the animation's translation; the root takes the
 * animation's translation alone.
 */
/* the magazines: the one in the gun and the one in the left hand; for a
 * speedloader inHand counts its rounds, one switch each (parts[1..]) */
static void gevrGexShowMagazines(ModelFileHeader *hdr, Model *model, s32 inGun, s32 inHand)
{
    const GexWeaponDef *def = gevrGexForHeader(hdr);
    s32 i;

    for (i = 0; i < def->numParts; i++)
    {
        ModelNode *sw = hdr->Switches[hdr->numSwitches - def->numParts + i];
        s32 *visible = sw != NULL ? (s32 *) modelGetNodeRwData(model, sw) : NULL;

        if (visible != NULL)
        {
            *visible = i == 0 ? inGun : def->loaderRounds > 1 ? i <= inHand : i == 1 ? inHand : def->visible[i];
        }
    }
}

/* The Golden Gun's open cover and chamber are separate toggled meshes, not
 * a magazine. Source scripts provide their single/dual visibility intervals. */
static void gevrGexShowChamber(ModelFileHeader *hdr, Model *model, s32 open)
{
    const GexWeaponDef *def = gevrGexForHeader(hdr);
    if (def->reload.openHide <= 0) return;
    for (s32 i=2; i<def->numParts; i++) {
        ModelNode *sw = hdr->Switches[hdr->numSwitches-def->numParts+i];
        if (sw) *(s32 *)modelGetNodeRwData(model,sw) = open;
    }
}

/* every joint from the gun matrix (rwmtx[0]), anchored as on the screen or not */
static void gevrGexPoseFrom(ModelFileHeader *hdr, Mtxf *rwmtx, s32 anchored, s32 anim, f32 frame)
{
    const GexWeaponDef *def = gevrGexForHeader(hdr);
    Mtxf base;

    matrix_4x4_copy(&rwmtx[0], &base);
    if (anchored && hdr->numMatrices > def->gunMatrix && hdr->numMatrices <= 64)
    {
        Mtxf anchor;

        gevrGexScreenAnchor(hdr, &anchor);
        matrix_4x4_multiply(&rwmtx[0], &anchor, &base);
    }
    gevrGexPoseWalk(hdr, &base, anim, frame, rwmtx);
}

/* gunfire.c, the watch's weapon pages: at rest, where GoldenEye's KF7 shows */
static void gevrGexHandFitTo(const GexWeaponDef *def, Mtxf *rwmtx, s32 numMatrices);   /* below */
static void gevrGexInstalledMagFitTo(const GexWeaponDef *def, Mtxf *mag, const Mtxf *gun);   /* below */

void gevrGexPoseStill(ModelFileHeader *hdr, Model *model, Mtxf *rwmtx)
{
    const GexWeaponDef *def = gevrGexForHeader(hdr);

    gevrGexShowMagazines(hdr, model, TRUE, FALSE);
    if (def->screenHand && hdr->numMatrices > def->gunMatrix && hdr->numMatrices <= 64)
    {
        /* a hand-held item at rest on a page: the item itself (its joint) where
         * GoldenEye's own model has its origin, square to the page (user: the
         * panel's grenade was a speck and its unarmed off to one side) */
        Mtxf ident, rest[64], inverse, base;

        matrix_4x4_set_identity(&ident);
        gevrGexPoseWalk(hdr, &ident, gevrGexRestAnim(def), 0.0f, rest);
        gevrGexRigidInverse(&rest[def->gunMatrix], &inverse);
        if (def->panelFit[0] > 0.0f)
        {
            /* onto GoldenEye's own mesh's place and size (user: the grenade was
             * small, the unarmed huge and off to one side) */
            const s32 turned = def->panelRot[0][0] != 0.0f || def->panelRot[0][1] != 0.0f || def->panelRot[0][2] != 0.0f;
            Mtxf fit, moved;
            s32 i, j;

            matrix_4x4_set_identity(&fit);
            for (i = 0; i < 3; i++)
            {
                for (j = 0; j < 3; j++)
                {
                    fit.m[i][j] = def->panelFit[0] * (turned ? def->panelRot[i][j] : (f32) (i == j));
                }
                fit.m[3][i] = def->panelFit[1 + i];
            }
            matrix_4x4_multiply(&fit, &inverse, &moved);
            inverse = moved;
        }
        matrix_4x4_multiply(&rwmtx[0], &inverse, &base);
        gevrGexPoseWalk(hdr, &base, gevrGexRestAnim(def), 0.0f, rwmtx);
        if (def->hideMatrix > 0 && def->hideMatrix < hdr->numMatrices)
        {
            gevrGexCollapse(&rwmtx[def->hideMatrix]);
        }
        if (def->offHandMatrix > 0 && def->offHandMatrix < hdr->numMatrices)
        {
            gevrGexCollapse(&rwmtx[def->offHandMatrix]);   /* the remote mine's detonator */
        }
        return;
    }
    gevrGexPoseFrom(hdr, rwmtx, TRUE, gevrGexRestAnim(def), 0.0f);
    if (hdr->numMatrices > def->gunMatrix && hdr->numMatrices <= 64)
    {
        gevrGexHandFitTo(def, rwmtx, hdr->numMatrices);
    }
    if (gevrGexHasMagazine(def) && def->magMatrix < hdr->numMatrices)
    {
        /* the visible magazine where Gun fit put it (user: not on the watch's pages) */
        gevrGexInstalledMagFitTo(def, &rwmtx[def->magMatrix], &rwmtx[def->gunMatrix]);
    }
}

/*
 * The headset's reload by hand (bondview2.c gevrGexMagState; user): the
 * player does the magazine's moves, so nothing animates. The gun stays at
 * rest, its magazine shown where it is - in the gun, in the left hand, or
 * gone - and while the off hand holds it, GE-X's left hand is the
 * player's, in its pose holding the magazine (the reload animation's
 * weapon definition's hold frame and held-magazine matrix). The
 * magazine turns with the off hand as the gun's own is turned in the gun
 * (user: it followed the gun and would not line up), so with the hands
 * held alike it lines up with the well; the hand is placed round it, and
 * all moved so its palm, GEVR_GEX_PALM_Z along the hand from the wrist,
 * is on the off hand's grip, as Perfect Dark VR moves its left hand to the
 * real one (vrApplyReloadOffset).
 */
#define GEVR_GEX_RHAND_ARM   1    /* the right forearm: GE-X's sleeve (Ghand_jowetsuitZ) */
#define GEVR_GEX_RHAND_WRIST 2
#define GEVR_GEX_LHAND_ARM   17
#define GEVR_GEX_LHAND_FIRST 17
#define GEVR_GEX_LHAND_LAST  32
#define GEVR_GEX_LHAND_WRIST 18
#define GEVR_GEX_PALM_Z      50.0f

/* the right gun's, from its last draw, for bondview2.c */
static f32 s_gevrGexMagAt[3], s_gevrGexWellAt[3], s_gevrGexHeldAt[3];
static s32 s_gevrGexMagPointsValid;   /* 1 the gun's magazine and well, 2 the held one */

/*
 * A magazine that leaves the gun by button, or the hand when let go,
 * falls (user: it just vanished): from where it was last drawn, taken
 * into the world as the casings' throw matrix is (gunfire.c: view units
 * over D_800364CC, then view to world), it drops under gravity (980
 * world units, cm, a second squared) until it passes the floor or a
 * second is up, drawn by its joint through the world-to-view matrix each
 * frame, so it falls in the room, not with the head.
 */
extern f32 D_800364CC;

static Mtxf s_gevrGexLastMag[2], s_gevrGexLastHeld;   /* installed matrix per gun hand, held matrix */
static Mtxf s_gevrGexLastGun[2];
static s32 s_gevrGexLastValid[2], s_gevrGexLastHeldValid;

static struct
{
    s32 on;
    s32 matrix;
    s32 rounds;   /* a speedloader's, as it was held */
    f32 t;        /* seconds */
    Mtxf world;
} s_gevrGexFall[2];
/* gunfire.c gevrGexDrawPayload: a prop payload (payloadProp) is held or falling */
static s32 s_gevrGexPayloadOn[2];

static void gevrGexRigidInverse(const Mtxf *g, Mtxf *inv);

extern s32 gevrGexHeldRoundCount(void);   /* bondview2.c: rounds in the off hand's payload */

void gevrGexMagazineFalls(s32 hand, s32 fromHand)
{
    const GexWeaponDef *def = gevrGexWeaponForHand(hand);
    Mtxf *v2w = currentPlayerGetViewToWorldMtxf();
    const Mtxf *last;
    Mtxf unit;
    f32 inv = D_800364CC > 1e-6f ? 1.0f / D_800364CC : 1.0f;
    s32 r, c;

    if (hand < 0 || hand > 1 || def == NULL || v2w == NULL || !(fromHand ? s_gevrGexLastHeldValid : s_gevrGexLastValid[hand]))
    {
        return;
    }
    last = fromHand ? &s_gevrGexLastHeld : &s_gevrGexLastMag[hand];
    for (r = 0; r < 4; r++)
    {
        for (c = 0; c < 3; c++)
        {
            unit.m[r][c] = last->m[r][c] * inv;
        }
        unit.m[r][3] = r == 3 ? 1.0f : 0.0f;
    }
    matrix_4x4_multiply(v2w, &unit, &s_gevrGexFall[hand].world);
    s_gevrGexFall[hand].on = TRUE;
    s_gevrGexFall[hand].matrix = fromHand ? def->heldMatrix : def->magMatrix;
    s_gevrGexFall[hand].rounds = fromHand ? gevrGexHeldRoundCount() : 0;
    s_gevrGexFall[hand].t = 0.0f;
}

static void gevrGexFallTick(s32 hand)
{
    if (hand >= 0 && hand < 2 && s_gevrGexFall[hand].on)
    {
        s_gevrGexFall[hand].t += g_ClockTimer / 60.0f;
    }
}

/* the falling magazine's joint this frame, in view units; FALSE once it is gone */
static s32 gevrGexFallAt(s32 hand, Mtxf *out)
{
    Mtxf *v2w = currentPlayerGetViewToWorldMtxf();
    Mtxf world, w2v;
    s32 r, c;

    if (!s_gevrGexFall[hand].on || v2w == NULL)
    {
        return FALSE;
    }
    world = s_gevrGexFall[hand].world;
    world.m[3][1] -= 490.0f * s_gevrGexFall[hand].t * s_gevrGexFall[hand].t;
    if (s_gevrGexFall[hand].t > 1.0f || world.m[3][1] < bondviewGetPlayerStanHeight(g_CurrentPlayer))
    {
        s_gevrGexFall[hand].on = FALSE;
        return FALSE;
    }
    gevrGexRigidInverse(v2w, &w2v);
    matrix_4x4_multiply(&w2v, &world, out);
    for (r = 0; r < 4; r++)
    {
        for (c = 0; c < 3; c++)
        {
            out->m[r][c] *= D_800364CC;
        }
    }
    return TRUE;
}

static void gevrGexMtxPoint(const Mtxf *m, const f32 local[3], f32 out[3])
{
    s32 i;

    for (i = 0; i < 3; i++)
    {
        out[i] = local[0] * m->m[0][i] + local[1] * m->m[1][i] + local[2] * m->m[2][i] + m->m[3][i];
    }
}

/*
 * Gun fit's Gun hand: GE-X's own right hand (joints 1..16) moved on the gun
 * (cm right, up, back) and turned about its palm (degrees pitch, yaw, roll, in
 * the gun's frame). A rig borrowed from another gun poses its hand at that
 * gun's grip (user: the grenade launcher's, built on the Cougar's, missed its
 * own). The gun matrix carries the gun's size, so its axes are normalised to
 * turn the joints' rows rather than inverted.
 */
static void gevrGexHandFitTo(const GexWeaponDef *def, Mtxf *rwmtx, s32 numMatrices)
{
    const f32 *pos = gevrGexHandFit(def->item);
    const f32 *rot = gevrGexHandRotFit(def->item);
    const f32 palmLocal[3] = { 0.0f, 0.0f, GEVR_GEX_PALM_Z };
    /* a hand-held item (knife, grenade, mine): the item moves, turns and is
     * sized about its own origin in the hand, the hand staying on the
     * controller (user: fit the grenade's size and place in the hand) */
    const s32 item = gevrGexIsHandHeld(def);
    const f32 size = item ? 1.0f + gevrGexItemSizeFit(def->item)[0] : 1.0f;
    const s32 first = item ? def->gunMatrix : GEVR_GEX_RHAND_ARM;
    const s32 last = item ? numMatrices : GEVR_GEX_LHAND_FIRST;
    Mtxf gunAt = rwmtx[def->gunMatrix];
    const Mtxf *gun = &gunAt;
    struct coord3d angles;
    Mtxf turn;
    f32 ax[3][3], palm[3], d[3], v[3], g[3], t[3];
    s32 i, j, k, r;

    if (pos[0] == 0.0f && pos[1] == 0.0f && pos[2] == 0.0f && rot[0] == 0.0f && rot[1] == 0.0f && rot[2] == 0.0f
        && size == 1.0f)
    {
        return;
    }
    if (size < 0.1f)
    {
        return;   /* never collapse it from a fit */
    }
    for (k = 0; k < 3; k++)
    {
        f32 len = sqrtf(gun->m[k][0] * gun->m[k][0] + gun->m[k][1] * gun->m[k][1] + gun->m[k][2] * gun->m[k][2]);

        for (i = 0; i < 3; i++)
        {
            ax[k][i] = len > 1e-9f ? gun->m[k][i] / len : 0.0f;
        }
    }
    for (i = 0; i < 3; i++)
    {
        d[i] = (-pos[0] * gun->m[0][i] + pos[1] * gun->m[1][i] - pos[2] * gun->m[2][i]) / 0.085f;
        angles.f[i] = rot[i] * (M_PI / 180.0f);
    }
    matrix_4x4_set_rotation_around_xyz(&angles, &turn);
    if (item)
    {
        for (i = 0; i < 3; i++) palm[i] = gunAt.m[3][i];   /* the item's own origin */
    }
    else
    {
        gevrGexMtxPoint(&rwmtx[GEVR_GEX_RHAND_WRIST], palmLocal, palm);
    }
    for (j = first; j < last && j < 64; j++)
    {
        /* rows 0..2 its axes, row 3 its place about the pivot: into the gun's
         * frame, turned (and sized) there, and back */
        for (r = 0; r < 4; r++)
        {
            for (i = 0; i < 3; i++)
            {
                v[i] = (r == 3 ? rwmtx[j].m[3][i] - palm[i] : rwmtx[j].m[r][i]) * size;
            }
            for (k = 0; k < 3; k++)
            {
                g[k] = v[0] * ax[k][0] + v[1] * ax[k][1] + v[2] * ax[k][2];
            }
            for (i = 0; i < 3; i++)
            {
                t[i] = g[0] * turn.m[0][i] + g[1] * turn.m[1][i] + g[2] * turn.m[2][i];
            }
            for (i = 0; i < 3; i++)
            {
                v[i] = t[0] * ax[0][i] + t[1] * ax[1][i] + t[2] * ax[2][i];
                rwmtx[j].m[r][i] = r == 3 ? palm[i] + v[i] + d[i] : v[i];
            }
        }
    }
}

s32 gevrGexMagPoints(f32 centre[3], f32 well[3], f32 held[3])
{
    s32 i;

    for (i = 0; i < 3; i++)
    {
        centre[i] = s_gevrGexMagAt[i];
        well[i] = s_gevrGexWellAt[i];
        held[i] = s_gevrGexHeldAt[i];
    }
    return s_gevrGexMagPointsValid;
}

/* a joint's matrix without scale, undone: rotation transposed, translation turned back */
static void gevrGexRigidInverse(const Mtxf *g, Mtxf *inv)
{
    s32 i, j;

    matrix_4x4_set_identity(inv);
    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 3; j++)
        {
            inv->m[i][j] = g->m[j][i];
        }
    }
    for (j = 0; j < 3; j++)
    {
        inv->m[3][j] = -(g->m[3][0] * g->m[j][0] + g->m[3][1] * g->m[j][1] + g->m[3][2] * g->m[j][2]);
    }
}

/* a joint with the gun's size in its rows (uniform): rows transposed and divided
 * by the size squared, the translation turned back */
static void gevrGexScaledInverse(const Mtxf *g, Mtxf *inv)
{
    const f32 s2 = g->m[0][0] * g->m[0][0] + g->m[0][1] * g->m[0][1] + g->m[0][2] * g->m[0][2];
    s32 i, j;

    gevrGexRigidInverse(g, inv);
    if (s2 < 1e-12f)
    {
        return;
    }
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 3; j++)
        {
            inv->m[i][j] /= s2;
        }
    }
}

/* bondview2.c gevrGexMagState */
enum { GEVR_GEXMAG_IN, GEVR_GEXMAG_GRIPPED, GEVR_GEXMAG_INHAND, GEVR_GEXMAG_OUT };
extern s32 gevrGexMagState(s32 hand, f32 off[3]);

/* the mechanism only (matrices after the gun's, bar the magazines) at a clip's
 * frame, on the gun as tracked: the hands and the body stay where they are */
static void gevrGexPoseMechanism(ModelFileHeader *hdr, Mtxf *rwmtx, s32 anim, f32 frame)
{
    const GexWeaponDef *def = gevrGexForHeader(hdr);
    Mtxf animated[64], ident, inverse, relative;
    s32 i;
    /* the converter appends one matrix for the muzzle flash (switch 1, part
     * 90) only when the rig has one; a grenade's last joint is its lever */
    const s32 end = hdr->Switches != NULL && hdr->numSwitches > 1 && hdr->Switches[1] != NULL
        ? hdr->numMatrices - 1 : hdr->numMatrices;
    if (anim <= 0 || frame < 0.0f || hdr->numMatrices > 64) return;
    matrix_4x4_set_identity(&ident);
    gevrGexPoseWalk(hdr, &ident, anim, frame, animated);
    gevrGexRigidInverse(&animated[def->gunMatrix], &inverse);
    for (i=def->gunMatrix+1; i<end; i++)
    {
        if (i == def->magMatrix || i == def->heldMatrix) continue;
        matrix_4x4_multiply(&inverse, &animated[i], &relative);
        matrix_4x4_multiply(&rwmtx[def->gunMatrix], &relative, &rwmtx[i]);
    }
}

static void gevrGexPoseReady(ModelFileHeader *hdr, Mtxf *rwmtx, f32 frame)
{
    const GexWeaponDef *def = gevrGexForHeader(hdr);
    if (!def->pistol && !def->singleRound) return;
    gevrGexPoseMechanism(hdr, rwmtx, def->reload.anim, frame);
}

static void gevrGexWellPoint(const GexWeaponDef *def, const Mtxf *mag, const Mtxf *gun, f32 out[3])
{
    const f32 *fit = gevrGexWellFit(def->item);
    const f32 noVisual[3] = {0,0,0};
    const f32 *visual = gevrGexHasMagazine(def) ? gevrGexInstalledMagFit(def->item) : noVisual;
    s32 i;
    gevrGexMtxPoint(mag, def->magWell, out);
    for (i = 0; i < 3; i++)
        out[i] += (-(fit[0]-visual[0]) * gun->m[0][i] + (fit[1]-visual[1]) * gun->m[1][i] - (fit[2]-visual[2]) * gun->m[2][i]) / 0.085f;
}

/* Off-hand trigger in well-fit mode: the held magazine's tip defines the
 * entrance. Change the gun-local target only, never the ammunition state. */
void gevrReloadFitSetWell(void)
{
    const GexWeaponDef *def = gevrGexWeaponForHand(GUNRIGHT);
    const Mtxf *gun = &s_gevrGexLastGun[GUNRIGHT];
    f32 length2[3], projection;
    f32 *fit;
    s32 r, i;
    if (def == NULL || (s_gevrGexMagPointsValid & 3) != 3) return;
    for (r = 0; r < 3; r++)
    {
        length2[r] = 0;
        for (i = 0; i < 3; i++) length2[r] += gun->m[r][i] * gun->m[r][i];
        if (length2[r] < 1e-12f) return;
    }
    fit = gevrGexWellFit(def->item);
    for (r = 0; r < 3; r++)
    {
        projection = 0;
        for (i = 0; i < 3; i++) projection += (s_gevrGexHeldAt[i] - s_gevrGexWellAt[i]) * gun->m[r][i];
        fit[r] += (r == 1 ? 1.0f : -1.0f) * projection / length2[r] * 0.085f;
    }
}

static void gevrGexLeftHandTo(ModelFileHeader *hdr, Mtxf *rwmtx, const f32 off[3])
{
    const GexWeaponDef *def = gevrGexForHeader(hdr);
    extern s32 gevrStereoOffHandMatrix(Mtxf *out);   /* bondview2.c */
    extern s32 gevrGexHeldPalm(f32 out[3]);           /* bondview2.c */
    static Mtxf rest[64], held[64];
    Mtxf ident, goff, mag, inv, rel, installed;
    f32 palm[3], d[3], at[3];
    const f32 palmLocal[3] = { 0.0f, 0.0f, GEVR_GEX_PALM_Z };
    s32 i, j;

    if (hdr->numMatrices <= def->heldMatrix || hdr->numMatrices > 64 || !gevrStereoOffHandMatrix(&goff))
    {
        return;
    }
    /* each joint from the gun's own frame: at rest, and holding the magazine */
    matrix_4x4_set_identity(&ident);
    gevrGexPoseWalk(hdr, &ident, gevrGexRestAnim(def), 0.0f, rest);
    gevrGexPoseWalk(hdr, &ident, gevrGexHoldAnim(def), def->holdFrame, held);

    /* the magazine as the gun's own sits in a gun held by the off hand */
    matrix_4x4_multiply(&goff, &rest[def->magMatrix], &installed);
    matrix_4x4_multiply(&installed, (Mtxf *) def->heldToMag, &mag);
    rwmtx[def->heldMatrix] = mag;
    /* the hand round it, as it holds it */
    gevrGexRigidInverse(&held[def->gripMatrix ? def->gripMatrix : def->heldMatrix], &inv);
    for (j = GEVR_GEX_LHAND_FIRST; j <= GEVR_GEX_LHAND_LAST; j++)
    {
        matrix_4x4_multiply(&inv, &held[j], &rel);
        matrix_4x4_multiply(&mag, &rel, &rwmtx[j]);
    }
    /* all of it moved so the palm is where the player set it (Gun fit's
     * reload mode: VrGexHeldMag from the off hand's grip), else on the grip */
    if (!gevrGexHeldPalm(at))
    {
        for (i = 0; i < 3; i++)
        {
            at[i] = off[i];
        }
    }
    gevrGexMtxPoint(&rwmtx[GEVR_GEX_LHAND_WRIST], palmLocal, palm);
    for (i = 0; i < 3; i++)
    {
        d[i] = at[i] - palm[i];
    }
    for (j = GEVR_GEX_LHAND_FIRST; j <= def->heldMatrix; j++)
    {
        if (j <= GEVR_GEX_LHAND_LAST || j == def->heldMatrix)
        {
            for (i = 0; i < 3; i++)
            {
                rwmtx[j].m[3][i] += d[i];
            }
        }
    }
}

/*
 * The headset's arms (user: one arm look and size for every hand, the gun
 * hands keep their animated fingers, and the watch must not shrink - its
 * health, armor and radar would be too small to read): with VrGexGuns on,
 * GoldenEye X's own arms are every arm, as GE-X made them, and GoldenEye's
 * watch is drawn at its own size on the left one's wrist, at the end of its
 * sleeve (bondview2.c gevrRenderGexWatch): the two-handed grip hand, the
 * hand holding a magazine, the empty off hand, and a left gun's mirrored
 * hand. Holding the gun with both hands, GE-X's left hand grips it where
 * its animation has it, in place of the grip hand.
 */
#define GEVR_GEX_CUFF_Z 330.0f   /* where the hand model's sleeves end, on their arm joints' z */
static void gevrGexOffCache(ModelFileHeader *gunhdr);
static s32 gevrGexOffHandPose(Mtxf *m, s32 n);
extern s32 gevrStereoTwoHandGrip(void);   /* bondview2.c */
extern float VrGexWatch[4];   /* vr_settings_defaults.c: the watch's place and size (Gun fit) */

static struct
{
    s32 on;
    f32 pos[3], x[3], y[3];   /* the watch arm's wrist: its place, toward the hand, the back of the wrist */
} s_gevrGexWatch[2];          /* per gun hand */

static s32 gevrGexArmsOn(void)
{
    extern s32 g_gevrStereo;

    return g_gevrStereo && VrGexGuns;
}

/* gunfire.c, bondview2.c: GE-X's left hand holds the gun with both hands */
s32 gevrGexLeftHandShown(void)
{
    return gevrGexArmsOn() && gevrGexHeld(GUNRIGHT) && gevrStereoTwoHandGrip();
}

/*
 * The watch on a left forearm joint (+Z from the elbow toward the hand, +Y
 * its back): from its axis where the sleeve ends, the fitted cm ahead, up
 * and out. The joint's rows carry the gun's size, 0.085 cm a unit at size 1
 * (bondview2.c GEVR_VIEWMODEL_CM x 0.1), which turns the cm into its units.
 */
static s32 gevrGexWatchFrame(const Mtxf *forearm, f32 pos[3], f32 x[3], f32 y[3])
{
    const f32 *t = VrGexWatch;
    const f32 cuff[3] = { 0.0f, 0.0f, GEVR_GEX_CUFF_Z };
    f32 lx = 0.0f, ly = 0.0f, z[3], at[3], u;
    s32 i;

    for (i = 0; i < 3; i++)
    {
        lx += forearm->m[2][i] * forearm->m[2][i];
        ly += forearm->m[1][i] * forearm->m[1][i];
    }
    lx = sqrtf(lx);
    ly = sqrtf(ly);
    if (lx < 1e-6f || ly < 1e-6f)
    {
        return FALSE;
    }
    for (i = 0; i < 3; i++)
    {
        x[i] = forearm->m[2][i] / lx;
        y[i] = forearm->m[1][i] / ly;
    }
    z[0] = x[1] * y[2] - x[2] * y[1];
    z[1] = x[2] * y[0] - x[0] * y[2];
    z[2] = x[0] * y[1] - x[1] * y[0];
    gevrGexMtxPoint(forearm, cuff, at);
    u = lx / 0.085f;   /* units a cm */
    for (i = 0; i < 3; i++)
    {
        pos[i] = at[i] + (t[0] * x[i] + t[1] * y[i] + t[2] * z[i]) * u;
    }
    return TRUE;
}

/* A watch face on a held joint (GexWeaponDef offHandFace): its centre, twelve
 * o'clock and out of the face as unit axes, and its radius, the joint's space */
static s32 gevrGexFaceFrame(const Mtxf *joint, const f32 face[4], f32 pos[3], f32 up[3], f32 normal[3],
                            f32 *radius)
{
    const f32 at[3] = { face[0], face[1], face[2] };
    f32 lx = 0.0f, ly = 0.0f, lz = 0.0f;
    s32 i;

    for (i = 0; i < 3; i++)
    {
        lx += joint->m[0][i] * joint->m[0][i];
        ly += joint->m[1][i] * joint->m[1][i];
        lz += joint->m[2][i] * joint->m[2][i];
    }
    lx = sqrtf(lx);
    ly = sqrtf(ly);
    lz = sqrtf(lz);
    if (face[3] <= 0.0f || lx < 1e-6f || ly < 1e-6f || lz < 1e-6f)
    {
        return FALSE;
    }
    gevrGexMtxPoint(joint, at, pos);
    for (i = 0; i < 3; i++)
    {
        up[i] = joint->m[1][i] / ly;
        normal[i] = -joint->m[2][i] / lz;
    }
    *radius = face[3] * lx;
    return TRUE;
}

/*
 * The rig's own item in the off hand (the remote mine's detonator watch; user:
 * it sat wrong in the hand, and the fit moved the arm): Gun fit's off hand mode
 * moves it in the hand (cm along the wrist's own x, y and back along its z) and
 * turns it about its own origin (degrees), the hand staying on the controller -
 * the support fits, which a one-handed item has no other use for. In the rig's
 * left wrist's frame, model units; `about` is the item's origin there.
 */
static void gevrGexOffHoldFit(const GexWeaponDef *def, const f32 about[3], Mtxf *out)
{
    const f32 *pos = gevrGexSupportFit(def->item);
    const f32 *rot = gevrGexSupportRotFit(def->item);
    const f32 *palm = about;
    struct coord3d angles;
    s32 j;

    angles.f[0] = rot[0] * (M_PI_F / 180.0f);
    angles.f[1] = rot[1] * (M_PI_F / 180.0f);
    angles.f[2] = rot[2] * (M_PI_F / 180.0f);
    matrix_4x4_set_rotation_around_xyz(&angles, out);
    for (j = 0; j < 3; j++)
    {
        out->m[3][j] = palm[j] - (palm[0] * out->m[0][j] + palm[1] * out->m[1][j] + palm[2] * out->m[2][j]);
        out->m[j][3] = 0.0f;
    }
    out->m[3][0] += pos[0] / 0.085f;
    out->m[3][1] += pos[1] / 0.085f;
    out->m[3][2] -= pos[2] / 0.085f;
    out->m[3][3] = 1.0f;
}

static void gevrGexWatchAt(s32 hand, const Mtxf *forearm)
{
    s_gevrGexWatch[hand].on = gevrGexWatchFrame(forearm, s_gevrGexWatch[hand].pos, s_gevrGexWatch[hand].x,
                                                s_gevrGexWatch[hand].y);
}

/*
 * Where GE-X's left hand holds the gun with both hands (user: the hold was
 * taken too near the magazine): its palm as the gun's animation has it,
 * moved VrGexForeHold cm forward, up and out along the gun (Gun fit's grip
 * mode), which moves the drawn hand too. bondview2.c gevrTwoHandBarrel
 * takes hold there.
 */
extern float VrGexForeHold[3];   /* vr_settings_defaults.c */
static f32 s_gevrGexForeAt[3], s_gevrGexForeOff[3];
static s32 s_gevrGexForeValid;

static void gevrGexForeFrom(const Mtxf *rwmtx)
{
    const GexWeaponDef *def = gevrGexWeaponForHand(GUNRIGHT);
    const f32 *fit = gevrGexSupportFit(def->item);
    const f32 palmLocal[3] = { 0.0f, 0.0f, GEVR_GEX_PALM_Z };
    const Mtxf *g = &rwmtx[def->gunMatrix];
    f32 l[3], u, original[3];
    s32 i, r;

    for (r = 0; r < 3; r++)
    {
        l[r] = sqrtf(g->m[r][0] * g->m[r][0] + g->m[r][1] * g->m[r][1] + g->m[r][2] * g->m[r][2]);
        if (l[r] < 1e-6f)
        {
            s_gevrGexForeValid = FALSE;
            return;
        }
    }
    u = l[2] / 0.085f;   /* units a cm */
    gevrGexMtxPoint(&rwmtx[GEVR_GEX_LHAND_WRIST], palmLocal, s_gevrGexForeAt);
    memcpy(original, s_gevrGexForeAt, sizeof(original));
    if (def->compact)
        gevrGexMtxPoint(&rwmtx[GEVR_GEX_RHAND_WRIST], palmLocal, s_gevrGexForeAt);
    for (i = 0; i < 3; i++)
    {
        s_gevrGexForeOff[i] = (fit[0] * g->m[2][i] / l[2] + fit[1] * g->m[1][i] / l[1]
                              + fit[2] * g->m[0][i] / l[0]) * u;
        s_gevrGexForeAt[i] += s_gevrGexForeOff[i];
        if (def->compact) s_gevrGexForeOff[i] = s_gevrGexForeAt[i] - original[i];
    }
    s_gevrGexForeValid = TRUE;
}

/* bondview2.c: where GE-X's left hand holds the gun, camera space */
s32 gevrGexForePoint(f32 out[3])
{
    s32 i;

    if (!s_gevrGexForeValid || !gevrGexHeld(GUNRIGHT))
    {
        return FALSE;
    }
    for (i = 0; i < 3; i++)
    {
        out[i] = s_gevrGexForeAt[i];
    }
    return TRUE;
}

/*
 * The off hand, empty (user: GE-X's hand for the off hand all the time,
 * so changing grips does not jar): GE-X's left hand and arm on the off
 * controller as its right hand sits on the gun, mirrored, its palm where
 * the player set the held magazine's (VrGexHeldMag, so taking one does
 * not move it; Gun fit's off hand mode), with the watch. The hands are
 * drawn with the gun's skeleton (Perfect Dark's way; the hand model's own
 * puts the forearm elsewhere), so the KF7's resting pose is kept from its
 * last draw: the right wrist in the gun's frame, and the left hand's
 * joints from its wrist. The watch arm stays for the pause's watch and
 * until a GE-X gun has been held.
 */
/* the off hand holding the rig's own item (the remote mine's watch): the rig's
 * left hand, posed on the tracked one this frame (gevrGexPoseGun) */
static Mtxf s_gevrGexOffHeld[GEVR_GEX_LHAND_LAST + 1];
static Mtxf s_gevrGexOffHeldItem;   /* the held joint itself, for its watch face */
static s32 s_gevrGexOffHeldTimer = -1;
static Mtxf s_gevrGexOffR2;
static Mtxf s_gevrGexOffChain[GEVR_GEX_LHAND_LAST + 1];
static ModelNode *s_gevrGexOffFrom;
/* A new gun was loaded, possibly into the same buffer: rebuild from it, but
 * keep drawing the last empty hand meanwhile. Clearing the cache made the off
 * hand fall back to GoldenEye's watch arm for the switch's hidden frames (user:
 * the original arm blinked between guns). The cache holds matrices only. */
static s32 s_gevrGexOffStale;

static void gevrGexOffCache(ModelFileHeader *gunhdr)
{
    const GexWeaponDef *def = gevrGexForHeader(gunhdr);
    static Mtxf rest[64];
    Mtxf ident, inv;
    s32 j;

    if ((gunhdr->RootNode == s_gevrGexOffFrom && !s_gevrGexOffStale)
        || gunhdr->numMatrices <= GEVR_GEX_LHAND_LAST || gunhdr->numMatrices > 64)
    {
        return;
    }
    matrix_4x4_set_identity(&ident);
    gevrGexPoseWalk(gunhdr, &ident, gevrGexRestAnim(def), 0.0f, rest);
    s_gevrGexOffR2 = rest[GEVR_GEX_RHAND_WRIST];
    gevrGexRigidInverse(&rest[GEVR_GEX_LHAND_WRIST], &inv);
    for (j = GEVR_GEX_LHAND_FIRST; j <= GEVR_GEX_LHAND_LAST; j++)
    {
        matrix_4x4_multiply(&inv, &rest[j], &s_gevrGexOffChain[j]);
    }
    s_gevrGexOffFrom = gunhdr->RootNode;
    s_gevrGexOffStale = FALSE;
}

/* the empty off hand's n joints (the right hand's on its wrist), its palm where
 * the held magazine's goes; FALSE until a GE-X gun has been posed */
static s32 gevrGexOffHandPose(Mtxf *m, s32 n)
{
    extern s32 gevrStereoOffHandMatrix(Mtxf *out);   /* bondview2.c */
    extern s32 gevrGexHeldPalm(f32 out[3]);           /* bondview2.c */
    Mtxf goff, mx, a, b, wrist;
    f32 palm[3], at[3], d[3];
    const f32 palmLocal[3] = { 0.0f, 0.0f, GEVR_GEX_PALM_Z };
    s32 i, j;

    if (s_gevrGexOffFrom == NULL || n <= GEVR_GEX_LHAND_LAST || !gevrStereoOffHandMatrix(&goff))
    {
        return FALSE;
    }
    /* the left wrist: the right one on the gun, mirrored into the off hand */
    matrix_4x4_set_identity(&mx);
    mx.m[0][0] = -1.0f;
    matrix_4x4_multiply(&mx, &s_gevrGexOffR2, &a);
    matrix_4x4_multiply(&a, &mx, &b);
    matrix_4x4_multiply(&goff, &b, &wrist);
    for (j = 0; j < n; j++)
    {
        if (j >= GEVR_GEX_LHAND_FIRST && j <= GEVR_GEX_LHAND_LAST)
        {
            matrix_4x4_multiply(&wrist, &s_gevrGexOffChain[j], &m[j]);
        }
        else
        {
            m[j] = wrist;   /* the right hand's joints: its mesh is switched off */
        }
    }
    /* its palm where the held magazine's is */
    if (gevrGexHeldPalm(at))
    {
        gevrGexMtxPoint(&m[GEVR_GEX_LHAND_WRIST], palmLocal, palm);
        for (i = 0; i < 3; i++)
        {
            d[i] = at[i] - palm[i];
        }
        for (j = GEVR_GEX_LHAND_FIRST; j <= GEVR_GEX_LHAND_LAST; j++)
        {
            for (i = 0; i < 3; i++)
            {
                m[j].m[3][i] += d[i];
            }
        }
    }
    return TRUE;
}

/*
 * Taking a magazine, the off hand's arm stays where the empty hand's was
 * (user: the hand may snap round it, the arm should not jump): the hand
 * holding it moves so its wrist is the empty hand's, and its forearm is
 * the empty hand's. Pistols also keep that wrist's tracked orientation.
 */
static Mtxf s_gevrGexOffEmpty[GEVR_GEX_LHAND_LAST + 1];

/* Some reloads (PP7, AR33) hold the magazine with the wrist turned backwards.
 * Keep its finger curl on the tracked wrist, and put the magazine's top
 * at that grip without changing the magazine's insertion orientation. */
static void gevrGexPistolMagGrip(ModelFileHeader *hdr, Mtxf *rwmtx, const Mtxf *empty)
{
    const GexWeaponDef *def = gevrGexForHeader(hdr);
    Mtxf held[64], ident, inverse, relative;
    f32 top[3], local[3], wanted[3], current[3];
    s32 i, j;

    matrix_4x4_set_identity(&ident);
    gevrGexPoseWalk(hdr, &ident, gevrGexHoldAnim(def), def->holdFrame, held);
    gevrGexRigidInverse(&held[GEVR_GEX_LHAND_WRIST], &inverse);
    gevrGexMtxPoint(&held[def->gripMatrix ? def->gripMatrix : def->heldMatrix],
        def->gripMatrix ? def->magTop : def->heldTop, top);
    gevrGexMtxPoint(&inverse, top, local);
    gevrGexMtxPoint(&empty[GEVR_GEX_LHAND_WRIST], local, wanted);
    gevrGexMtxPoint(&rwmtx[def->heldMatrix], def->heldTop, current);

    rwmtx[GEVR_GEX_LHAND_ARM] = empty[GEVR_GEX_LHAND_ARM];
    for (j = GEVR_GEX_LHAND_WRIST; j <= GEVR_GEX_LHAND_LAST; j++)
    {
        matrix_4x4_multiply(&inverse, &held[j], &relative);
        matrix_4x4_multiply((Mtxf *) &empty[GEVR_GEX_LHAND_WRIST], &relative, &rwmtx[j]);
    }
    for (i = 0; i < 3; i++)
    {
        rwmtx[def->heldMatrix].m[3][i] += wanted[i] - current[i];
    }
}

static s32 gevrGexOffSteady(ModelFileHeader *hdr, Mtxf *rwmtx)
{
    const GexWeaponDef *def = gevrGexForHeader(hdr);
    f32 d[3];
    s32 i, j;

    gevrGexOffCache(hdr);
    if (!gevrGexOffHandPose(s_gevrGexOffEmpty, GEVR_GEX_LHAND_LAST + 1))
    {
        return FALSE;
    }
    if (def->compact || def->trackedMagWrist)
    {
        gevrGexPistolMagGrip(hdr, rwmtx, s_gevrGexOffEmpty);
        return TRUE;
    }
    for (i = 0; i < 3; i++)
    {
        d[i] = s_gevrGexOffEmpty[GEVR_GEX_LHAND_WRIST].m[3][i] - rwmtx[GEVR_GEX_LHAND_WRIST].m[3][i];
    }
    for (j = GEVR_GEX_LHAND_FIRST; j <= def->heldMatrix; j++)
    {
        if (j <= GEVR_GEX_LHAND_LAST || j == def->heldMatrix)
        {
            for (i = 0; i < 3; i++)
            {
                rwmtx[j].m[3][i] += d[i];
            }
        }
    }
    rwmtx[GEVR_GEX_LHAND_ARM] = s_gevrGexOffEmpty[GEVR_GEX_LHAND_ARM];
    return TRUE;
}

/* The support wrist stays fixed to the pistol, as GE's pistol grip does.
 * Off-controller rotation only determines whether support is held. */
static void gevrGexPistolSupportPose(ModelFileHeader *hdr, Mtxf *rwmtx)
{
    /* Rotation is per model family, in the gun frame. */
    const GexWeaponDef *def = gevrGexForHeader(hdr);
    const f32 palmLocal[3] = {0,0,GEVR_GEX_PALM_Z};
    Mtxf rest[64], ident, mirror, a, b, inverse, relative, wrist, rotation, rotated;
    struct coord3d angles;
    f32 palm[3];
    s32 i,j;
    gevrGexOffCache(hdr);
    matrix_4x4_set_identity(&ident);
    gevrGexPoseWalk(hdr, &ident, gevrGexRestAnim(def), 0.0f, rest);
    matrix_4x4_set_identity(&mirror);
    mirror.m[0][0] = -1.0f;
    matrix_4x4_multiply(&mirror, &rest[GEVR_GEX_RHAND_WRIST], &a);
    matrix_4x4_multiply(&a, &mirror, &b);
    if (!def->compact) b = rest[GEVR_GEX_LHAND_WRIST];
    gevrGexRigidInverse(&rest[def->gunMatrix], &inverse);
    matrix_4x4_multiply(&inverse, &b, &relative);
    for (i = 0; i < 3; i++) angles.f[i] = gevrGexSupportRotFit(def->item)[i] * (M_PI / 180.0f);
    matrix_4x4_set_rotation_around_xyz(&angles, &rotation);
    matrix_4x4_multiply(&rotation, &relative, &rotated);
    matrix_4x4_multiply(&rwmtx[def->gunMatrix], &rotated, &wrist);
    gevrGexMtxPoint(&wrist, palmLocal, palm);
    for (j=GEVR_GEX_LHAND_FIRST; j<=GEVR_GEX_LHAND_LAST; j++)
    {
        matrix_4x4_multiply(&wrist, &s_gevrGexOffChain[j], &rwmtx[j]);
        for (i=0;i<3;i++) rwmtx[j].m[3][i] += s_gevrGexForeAt[i]-palm[i];
    }
}

/* Mesh-only fit, in off-controller cm. Its adjusted top is also the physical
 * seating point, and the same matrix is cached for a dropped magazine. */
static void gevrGexHeldMagFitTo(const GexWeaponDef *def, Mtxf *mag)
{
    extern s32 gevrStereoOffHandMatrix(Mtxf *out);
    const f32 *fit = gevrGexHeldMagFit(def->item);
    Mtxf off;
    s32 i;
    if (!gevrStereoOffHandMatrix(&off)) return;
    for (i = 0; i < 3; i++)
    {
        mag->m[3][i] += (-fit[0] * off.m[0][i] + fit[1] * off.m[1][i] - fit[2] * off.m[2][i]) / 0.085f;
    }
}

/* Installed-magazine fitting changes only the visible mesh. The well helper
 * removes this visual delta so existing insertion-target calibration stays put. */
static void gevrGexInstalledMagFitTo(const GexWeaponDef *def, Mtxf *mag, const Mtxf *gun)
{
    const f32 *fit = gevrGexInstalledMagFit(def->item);
    s32 i;
    for (i=0; i<3; i++)
        mag->m[3][i] += (-fit[0]*gun->m[0][i]+fit[1]*gun->m[1][i]-fit[2]*gun->m[2][i])/0.085f;
}

static s32 gevrGexInstalledMagazineFitting(void)
{
    extern int gevrInstalledMagFitting, gevrGunFitActive;
    extern s32 g_gevrStereo;
    return g_gevrStereo && gevrGunFitActive == 1 && gevrInstalledMagFitting && gevrGexHeld(GUNRIGHT) && gevrGexHasMagazine(gevrGexWeaponForHand(GUNRIGHT));
}

/* Fitting gets a visual magazine even when physical reload is disabled.
 * This changes rendering only; inventory and the reload state stay live. */
static s32 gevrGexMagazineFitting(void)
{
    extern int gevrHeldMagFitting, gevrWellFitting, gevrGunFitActive;
    extern s32 g_gevrStereo;
    return g_gevrStereo && gevrGunFitActive == 1 && (gevrHeldMagFitting || gevrWellFitting) && gevrGexHeld(GUNRIGHT) && gevrGexHasAmmo(gevrGexWeaponForHand(GUNRIGHT));
}

static s32 gevrGexOffHandConsumed(s32 mag, s32 *drawn)
{
    if (mag == GEVR_GEXMAG_GRIPPED || mag == GEVR_GEXMAG_INHAND || gevrGexMagazineFitting())
    {
        *drawn = TRUE; /* its mesh was already rendered with the gun's hands */
        return TRUE;
    }
    return FALSE;
}

Gfx *gevrGexDrawWatch(Gfx *gdl, ModelRenderData *templ, GUNHAND hand)
{
    extern Gfx *gevrRenderGexWatch(Gfx *gdl, ModelRenderData *templ, const f32 pos[3], const f32 x[3],
                                   const f32 y[3]);   /* bondview2.c */

    if ((hand == GUNRIGHT || hand == GUNLEFT) && s_gevrGexWatch[hand].on)
    {
        gdl = gevrRenderGexWatch(gdl, templ, s_gevrGexWatch[hand].pos, s_gevrGexWatch[hand].x, s_gevrGexWatch[hand].y);
    }
    return gdl;
}

/*
 * gunfire.c, with the gun's hands: a GoldenEye prop as the held payload of a
 * rig that has none (the grenade launcher's round), on the held matrix the
 * hand holds, scaled into the gun's units. Its matrices are this draw's own,
 * turned to the view's fixed point after it, as the launcher's rocket is.
 */
extern struct ItemModelFileRecord PitemZ_entries[];
static Model s_gevrGexPayloadModel;
static u32 s_gevrGexPayloadRw[64];

Gfx *gevrGexDrawPayload(Gfx *gdl, ModelRenderData *templ, GUNHAND hand)
{
    const GexWeaponDef *def = gevrGexWeaponForHand(hand);
    ModelFileHeader *ph;
    ModelRenderData renderdata;
    Mtxf *held, *m, scale;
    s32 i, n;

    if ((hand != GUNRIGHT && hand != GUNLEFT) || def == NULL || def->payloadProp <= 0 || !s_gevrGexPayloadOn[hand])
    {
        return gdl;
    }
    ph = PitemZ_entries[def->payloadProp].header;
    held = (Mtxf *) g_CurrentPlayer->hands[hand].weaponModel.render_pos;
    if (ph == NULL || ph->RootNode == NULL || ph->numMatrices <= 0 || held == NULL
        || ph->numRecords > (s32) (sizeof(s_gevrGexPayloadRw) / sizeof(s_gevrGexPayloadRw[0]))
        || g_CurrentPlayer->copy_of_body_obj_header[hand].numMatrices <= def->heldMatrix)
    {
        return gdl;
    }
    n = ph->numMatrices;
    m = (Mtxf *) dynAllocate(n * (s32) sizeof(Mtxf));
    matrix_4x4_set_identity(&scale);
    scale.m[0][0] = scale.m[1][1] = scale.m[2][2] = def->payloadScale;
    matrix_4x4_multiply(&held[def->heldMatrix], &scale, &m[0]);
    for (i = 1; i < n; i++)
    {
        m[i] = m[0];
    }
    if (s_gevrGexPayloadModel.obj != ph)
    {
        modelInit(&s_gevrGexPayloadModel, ph, s_gevrGexPayloadRw);
    }
    s_gevrGexPayloadModel.render_pos = (RenderPosView *) m;
    modelUpdateRelationsQuick(&s_gevrGexPayloadModel, ph->RootNode);
    renderdata = *templ;
    renderdata.gdl = gdl;
    subdraw(&renderdata, &s_gevrGexPayloadModel);
    bondviewTransformManyPosToViewMatrix(s_gevrGexPayloadModel.render_pos, n);
    return renderdata.gdl;
}

void gevrGexPoseGun(ModelFileHeader *hdr, Model *model, Mtxf *rwmtx, GUNHAND hand)
{
    const GexWeaponDef *def = gevrGexWeaponForHand(hand);
    const GexReloadDef *reload = getCurrentPlayerWeaponId(GUNLEFT) > ITEM_FIST
        ? &def->dualReload : &def->reload;
    extern s32 g_gevrStereo;
    f32 off[3];
    const s32 mag = g_gevrStereo ? gevrGexMagState(hand, off) : GEVR_GEXMAG_IN;
    const s32 preview = hand == GUNRIGHT && gevrGexMagazineFitting()
        && mag != GEVR_GEXMAG_GRIPPED && mag != GEVR_GEXMAG_INHAND;
    const s32 installedPreview = hand == GUNRIGHT && gevrGexInstalledMagazineFitting();
    const s32 offHolds = preview || (hand == GUNRIGHT && (mag == GEVR_GEXMAG_GRIPPED || mag == GEVR_GEXMAG_INHAND));
    s32 p = get_cur_playernum();
    f32 phase = gevrReloadPhase(hand);
    f32 fire = (p >= 0 && p < MAX_PLAYER_COUNT) ? s_gevrGexFire[p][hand] : -1.0f;
    s32 anim = gevrGexRestAnim(def);
    f32 frame = 0.0f;
    s32 swapped = FALSE;

    if (phase >= 0.0f && reload->anim > 0)
    {
        /* lowering and swapping up to the ammo frame, raising after it */
        f32 last = (f32) (gevrPdAnimNumFrames(reload->anim) - 1);

        anim = reload->anim;
        frame = phase < 2.0f ? phase * 0.5f * reload->ammoFrame
                             : reload->ammoFrame + (phase - 2.0f) * (last - reload->ammoFrame);
        swapped = frame >= reload->magOut && frame < reload->ammoFrame;
    }
    else if (fire >= 0.0f && def->fireAnim > 0)
    {
        anim = def->fireAnim; frame = fire;
    }
    else if (def->fireAnimAlt > 0 && g_CurrentPlayer != NULL && (hand == GUNRIGHT || hand == GUNLEFT))
    {
        /* the fist: GoldenEye's punch, as GE-X's (1001 or 1002), on GoldenEye's clock */
        const s32 st = g_CurrentPlayer->hands[hand].weapon_action_state;
        const s32 punch = st == GUN_ANIM_STATE_PUNCH1_STRIKE || st == GUN_ANIM_STATE_PUNCH1_RECOVER ? def->fireAnim
                        : st == GUN_ANIM_STATE_PUNCH2_STRIKE || st == GUN_ANIM_STATE_PUNCH2_RECOVER ? def->fireAnimAlt : 0;

        if (punch > 0)
        {
            const f32 last = (f32) (gevrPdAnimNumFrames(punch) - 1);

            anim = punch;
            frame = g_CurrentPlayer->hands[hand].field_890 < last ? g_CurrentPlayer->hands[hand].field_890 : last;
        }
    }
    if (p >= 0 && p < MAX_PLAYER_COUNT && (phase >= 0.0f) != s_gevrGexReloading[p][hand])
    {
        s_gevrGexReloading[p][hand] = phase >= 0.0f;
        sysLogPrintf(LOG_NOTE, "gexanim: hand %d reload %s", hand, phase >= 0.0f ? "starts" : "ends");
    }

    s32 inGun = (mag == GEVR_GEXMAG_IN ? !swapped : mag == GEVR_GEXMAG_GRIPPED)
        && !(def->magMatrix < 0 && gevrGexItemSpent(def, hand));   /* a thrown knife or grenade's part */
    s32 inHand = mag == GEVR_GEXMAG_IN ? phase >= 0.0f && reload->heldShow >= 0
        && frame >= reload->heldShow && frame < reload->heldHide
        : (mag == GEVR_GEXMAG_INHAND && hand == GUNRIGHT);
    inHand |= preview;
    inGun |= installedPreview;
    s32 offSteady = FALSE;
    s32 fell = FALSE;
    Mtxf falling;

    if (!preview && offHolds && s_gevrGexFall[hand].on && s_gevrGexFall[hand].matrix == def->heldMatrix)
    {
        s_gevrGexFall[hand].on = FALSE;   /* the hand has a new one: the old one's joint is its */
    }
    gevrGexPoseFrom(hdr, rwmtx, !g_gevrStereo, anim, frame);
    if (g_gevrStereo && p >= 0 && p < MAX_PLAYER_COUNT)
        gevrGexPoseReady(hdr, rwmtx, s_gevrGexReadyFrame[p][hand]);
    if (g_gevrStereo && offHolds && gevrGexOpensWhileHeld(def))
        gevrGexPoseReady(hdr,rwmtx,def->holdFrame);
    /* a grenade cooking on the trigger (the grip held or not): GE-X's own lever
     * pops off it, the grenade staying in the tracked hand (user); gone once
     * flown, until the next one */
    f32 cookFrame = -1.0f;
    if (g_gevrStereo && def->cookAnim > 0 && (hand == GUNRIGHT || hand == GUNLEFT) && g_CurrentPlayer != NULL
        && g_CurrentPlayer->hands[hand].weapon_action_state == GUN_ANIM_STATE_TRIGGER_PRESS)
    {
        cookFrame = (f32) g_CurrentPlayer->hands[hand].field_890;
        if (cookFrame > def->cookEnd) cookFrame = def->cookEnd;
        gevrGexPoseMechanism(hdr, rwmtx, def->cookAnim, cookFrame);
    }
    /* the fist's hand fit is the headset's gesture hand on the controller: on the
     * screen GE-X's own punches play from its own pose (user: the arm stood up
     * in the air to the right) */
    if (hdr->numMatrices > def->gunMatrix && hdr->numMatrices <= 64 && (g_gevrStereo || def->fireAnimAlt <= 0))
    {
        gevrGexHandFitTo(def, rwmtx, hdr->numMatrices);   /* before the support point is taken from this hand */
    }
    if (g_gevrStereo && hand == GUNRIGHT && hdr->numMatrices > def->gunMatrix)
    {
        gevrGexForeFrom(rwmtx);   /* before a magazine in the hand moves the left hand */
    }
    /* the visible magazine's fit is the model's: the screen too (user) */
    if (gevrGexHasMagazine(def))
        gevrGexInstalledMagFitTo(def, &rwmtx[def->magMatrix], &rwmtx[def->gunMatrix]);
    if (offHolds)
    {
        gevrGexLeftHandTo(hdr, rwmtx, off);
        offSteady = (def->compact || def->trackedMagWrist || gevrGexArmsOn()) && gevrGexOffSteady(hdr, rwmtx);
        gevrGexHeldMagFitTo(def, &rwmtx[def->heldMatrix]);
    }
    if (g_gevrStereo && gevrGexHasAmmo(def) && hdr->numMatrices > def->heldMatrix && (hand == GUNRIGHT || hand == GUNLEFT))
    {
        /* Cache the installed pose before a falling mesh occupies this matrix. */
        s_gevrGexLastMag[hand] = rwmtx[def->magMatrix];
        s_gevrGexLastGun[hand] = rwmtx[def->gunMatrix];
        s_gevrGexLastValid[hand] = TRUE;
        if (hand == GUNRIGHT && offHolds)
        {
            s_gevrGexLastHeld = rwmtx[def->heldMatrix];
            s_gevrGexLastHeldValid = TRUE;
        }
        if (!preview && !installedPreview && gevrGexFallAt(hand, &falling))
        {
            rwmtx[s_gevrGexFall[hand].matrix] = falling;
            if (s_gevrGexFall[hand].matrix == def->heldMatrix)
            {
                inHand = TRUE;
                fell = TRUE;
            }
            else
            {
                inGun = TRUE;
            }
        }
    }
    if (!g_gevrStereo && phase >= 0.0f && inHand && def->gripMatrix)
        rwmtx[def->heldMatrix] = rwmtx[def->gripMatrix];
    if (hand == GUNRIGHT || hand == GUNLEFT)
    {
        s_gevrGexPayloadOn[hand] = def->payloadProp > 0 && inHand;
    }
    if (def->loaderRounds > 1 && inHand)
    {
        /* the loader's rounds: as held, as dropped, else full (screen reload, fit preview) */
        const s32 held = gevrGexHeldRoundCount();
        inHand = fell ? s_gevrGexFall[hand].rounds
               : (!preview && hand == GUNRIGHT && mag == GEVR_GEXMAG_INHAND && held > 0) ? held
               : def->loaderRounds;
    }
    gevrGexShowMagazines(hdr, model, inGun, inHand);
    if (def->reload.openHide > 0) {
        f32 chamberFrame = phase >= 0 ? frame : -1;
        if (g_gevrStereo && p >= 0 && p < MAX_PLAYER_COUNT) {
            chamberFrame = s_gevrGexReadyFrame[p][hand];
        }
        gevrGexShowChamber(hdr,model,gevrGexChamberOpen(reload,chamberFrame,g_gevrStereo && offHolds));
    }

    /* the headset's arms: the watch on the left one (a left gun's hand is mirrored into a left) */
    if (hand == GUNRIGHT || hand == GUNLEFT)
    {
        s_gevrGexWatch[hand].on = FALSE;
        if (gevrGexArmsOn() && hdr->numMatrices > GEVR_GEX_LHAND_WRIST)
        {
            if (hand == GUNRIGHT && !offHolds && s_gevrGexForeValid && gevrGexLeftHandShown())
            {
                /* the holding hand where Gun fit's grip mode put it */
                s32 i, j;

                const float *rotationFit = gevrGexSupportRotFit(def->item);
                if (def->compact || def->item != ITEM_AK47 || rotationFit[0] != 0 || rotationFit[1] != 0 || rotationFit[2] != 0)
                    gevrGexPistolSupportPose(hdr, rwmtx);
                else for (j = GEVR_GEX_LHAND_FIRST; j <= GEVR_GEX_LHAND_LAST; j++)
                {
                    for (i = 0; i < 3; i++)
                    {
                        rwmtx[j].m[3][i] += s_gevrGexForeOff[i];
                    }
                }
            }
            if (hand == GUNLEFT)
            {
                gevrGexWatchAt(hand, &rwmtx[GEVR_GEX_RHAND_ARM]);
            }
            else if (offHolds || gevrGexLeftHandShown())
            {
                gevrGexWatchAt(hand, &rwmtx[GEVR_GEX_LHAND_ARM]);
            }
            if (hand == GUNRIGHT)
            {
                gevrGexOffCache(hdr);
            }
        }
    }
    if (g_gevrStereo && gevrGexHasAmmo(def) && hand == GUNRIGHT && hdr->numMatrices > def->heldMatrix)
    {
        gevrGexMtxPoint(&s_gevrGexLastMag[hand], def->magCentre, s_gevrGexMagAt);
        gevrGexWellPoint(def, &s_gevrGexLastMag[hand], &s_gevrGexLastGun[hand], s_gevrGexWellAt);
        gevrGexMtxPoint(&rwmtx[def->heldMatrix], def->heldTop, s_gevrGexHeldAt);
        s_gevrGexMagPointsValid = 1 | (offHolds ? 2 : 0);
    }
    /*
     * A watch item in the headset (user: GE-X's watch laser rig): the tracked
     * left arm wears the watch (gevrGexDrawOffHand), so the rig's own device
     * goes; with the hand at the watch (bondview2.c gevrStereoWatchGrip) the
     * rig's right hand presses it as GE-X's raised arm has it, its left forearm
     * put on the tracked one's (the same joint of the same skeleton).
     */
    if (def->watch && g_gevrStereo && hand == GUNRIGHT && hdr->numMatrices <= 64)
    {
        extern s32 gevrStereoWatchGrip(void);   /* bondview2.c */
        Mtxf offArm[GEVR_GEX_LHAND_LAST + 1], inverse, onto, moved;
        s32 j;

        if (gevrStereoWatchGrip() && hdr->numMatrices > GEVR_GEX_LHAND_LAST
            && gevrGexOffHandPose(offArm, GEVR_GEX_LHAND_LAST + 1))
        {
            gevrGexScaledInverse(&rwmtx[GEVR_GEX_LHAND_ARM], &inverse);
            matrix_4x4_multiply(&offArm[GEVR_GEX_LHAND_ARM], &inverse, &onto);
            for (j = 0; j < hdr->numMatrices; j++)
            {
                matrix_4x4_multiply(&onto, &rwmtx[j], &moved);
                rwmtx[j] = moved;
            }
        }
        for (j = def->gunMatrix; j < hdr->numMatrices; j++)
        {
            gevrGexCollapse(&rwmtx[j]);
        }
    }
    /*
     * A joint the off hand holds (the remote mine's detonator; user: GE-X's
     * watch in the palm). In the headset the empty off hand is the player's
     * (gevrGexDrawOffHand): the joint goes there, as the rig's left wrist has
     * it; with no such hand it goes. On the screen the rig's own left hand
     * holds it.
     */
    if (def->offHandMatrix > 0 && def->offHandMatrix < hdr->numMatrices && g_gevrStereo)
    {
        extern s32 gevrStereoTwoHandGrip(void);
        Mtxf offArm[GEVR_GEX_LHAND_LAST + 1], inverse, onto, moved;
        s32 j, placed = FALSE;

        if (hand == GUNRIGHT && gevrGexArmsOn() && hdr->numMatrices > GEVR_GEX_LHAND_LAST && hdr->numMatrices <= 64
            && getCurrentPlayerWeaponId(GUNLEFT) == ITEM_UNARMED && !gevrStereoTwoHandGrip()
            && gevrGexOffHandPose(offArm, GEVR_GEX_LHAND_LAST + 1))
        {
            Mtxf holdFit, local, ontoItem;
            f32 about[3];

            gevrGexScaledInverse(&rwmtx[GEVR_GEX_LHAND_WRIST], &inverse);
            matrix_4x4_multiply(&offArm[GEVR_GEX_LHAND_WRIST], &inverse, &onto);
            /* the item itself as Gun fit's off hand mode has it in the hand (user) */
            gevrGexMtxPoint(&inverse, rwmtx[def->offHandMatrix].m[3], about);
            gevrGexOffHoldFit(def, about, &holdFit);
            matrix_4x4_multiply(&holdFit, &inverse, &local);
            matrix_4x4_multiply(&offArm[GEVR_GEX_LHAND_WRIST], &local, &ontoItem);
            /* GE-X's own left hand holding it (user: its watch in the palm), for
             * gevrGexDrawOffHand: the rig's hand as it holds it, on the tracked wrist */
            for (j = 0; j <= GEVR_GEX_LHAND_LAST; j++)
            {
                s_gevrGexOffHeld[j] = offArm[j];
            }
            for (j = GEVR_GEX_LHAND_FIRST; j <= GEVR_GEX_LHAND_LAST; j++)
            {
                matrix_4x4_multiply(&onto, &rwmtx[j], &s_gevrGexOffHeld[j]);
            }
            s_gevrGexOffHeldTimer = g_GlobalTimer;
            for (j = def->offHandMatrix; j < hdr->numMatrices; j++)
            {
                matrix_4x4_multiply(&ontoItem, &rwmtx[j], &moved);
                rwmtx[j] = moved;
            }
            s_gevrGexOffHeldItem = rwmtx[def->offHandMatrix];
            placed = TRUE;
        }
        if (!placed)
        {
            gevrGexCollapse(&rwmtx[def->offHandMatrix]);
        }
    }
    /* last, once nothing else reads them: a stray mesh, and a thrown item that
     * is no toggled part (the mines' own bodies on the gun matrix) */
    if (def->hideMatrix > 0 && def->hideMatrix < hdr->numMatrices)
    {
        gevrGexCollapse(&rwmtx[def->hideMatrix]);
    }
    if (def->spentMatrix > 0 && def->spentMatrix < hdr->numMatrices && gevrGexItemSpent(def, hand))
    {
        gevrGexCollapse(&rwmtx[def->spentMatrix]);
    }
    if (def->cookMatrix > 0 && def->cookMatrix < hdr->numMatrices && cookFrame >= def->cookEnd)
    {
        gevrGexCollapse(&rwmtx[def->cookMatrix]);   /* the lever has flown */
    }
}

static void gevrGexResetModel(s32 hand, s32 rebuildCache)
{
    s32 p = get_cur_playernum();
    if (p >= 0 && p < MAX_PLAYER_COUNT && hand >= 0 && hand < 2)
    {
        s_gevrGexFire[p][hand] = -1.0f;
        s_gevrGexReadyFrame[p][hand] = -1.0f;
        s_gevrGexFiring[p][hand] = s_gevrGexReloading[p][hand] = FALSE;
        if (netIsActive() && p != netGetLocalSlot()) return;
        s_gevrGexLastValid[hand] = s_gevrGexFall[hand].on = FALSE;
        s_gevrGexPayloadOn[hand] = FALSE;
        s_gevrGexWatch[hand].on = FALSE;
        if (hand == GUNRIGHT)
        {
            s_gevrGexMagPointsValid = s_gevrGexLastHeldValid = s_gevrGexForeValid = FALSE;
            if (rebuildCache) s_gevrGexOffStale = TRUE;
        }
    }
}

static void gevrGexGunDone(void)
{
    free(gevrGexPendingFile);
    gevrGexPendingFile = NULL;
    gevrGexPendingLen = 0;
    g_gevrHandPatchSkip = FALSE;
}

/*
 * GoldenEye X's hands (docs/gex-weapons.md, Hands). Perfect Dark draws a
 * hand model - per outfit; GE-X's Bond wears Ghand_jowetsuitZ - with the
 * gun model's own matrices (pdvr bondgun.c: handmodel.matrices =
 * gunmodel.matrices), every first-person gun carrying the same hand
 * skeleton: 0 the root, 1-16 the right hand, 17-32 the left. So it
 * follows the gun's animation. Built as the gun is and loaded into its
 * own buffer through the KF7's file (ob.c takes it in that one's place),
 * a stage at a time as the taser hand is (gunfire.c). Its two meshes are
 * its switches: the left hand (PD part 53) and the right (54).
 * In the headset the left one stays hidden, as Perfect Dark VR hides it:
 * the player's own off hand is there.
 */
#define GEVR_GEX_HAND_BUFSIZE   0x50000
#define GEVR_GEX_HAND_MODELSIZE 0x20000
#define GEVR_GEX_HAND_SW_LEFT   0
#define GEVR_GEX_HAND_SW_RIGHT  1

/* the hand's one GoldenEye texture (the others are GE-X's own), by pixels */
static const u16 s_gevrGexHandTextures[] = {
    1273, 1921,
    0
};

extern LEVELID bossGetStageNum(void);
extern s32 g_gevrStereo;

static u8 *s_gevrGexHandBuf;
static struct texpool s_gevrGexHandPool;
static ModelFileHeader s_gevrGexHandHeader;
static Model s_gevrGexHandModel;
static u32 s_gevrGexHandRw[128];
static s32 s_gevrGexHandStage = -1;
static s32 s_gevrGexHandReady;

static s32 gevrGexHandLoad(void)
{
    const s32 parts[2] = { 53, 54 };
    ModelFileHeader *tmpl = gitem_structs[ITEM_AK47].item_header;
    const char *carrier = (const char *) gitem_structs[ITEM_AK47].item_file_name;
    u32 len = 0;
    u16 mtx = 0, tex = 0;

    if (s_gevrGexHandStage == bossGetStageNum())
    {
        return s_gevrGexHandReady;
    }
    s_gevrGexHandStage = bossGetStageNum();
    s_gevrGexHandReady = FALSE;
    if (tmpl == NULL || carrier == NULL)
    {
        return FALSE;
    }
    if (s_gevrGexHandBuf == NULL)
    {
        s_gevrGexHandBuf = malloc(GEVR_GEX_HAND_BUFSIZE);
        if (s_gevrGexHandBuf == NULL)
        {
            return FALSE;
        }
    }
    gevrGexPendingFile = gevrGexBuildModel("Ghand_jowetsuitZ", 2, parts, s_gevrGexHandTextures, &len, &mtx, &tex);
    if (gevrGexPendingFile == NULL)
    {
        return FALSE;
    }
    gevrGexPendingLen = len;
    s_gevrGexHandHeader = *tmpl;
    s_gevrGexHandHeader.numSwitches = 2;
    s_gevrGexHandHeader.numMatrices = mtx;
    s_gevrGexHandHeader.numtextures = tex;
    g_gevrHandPatchSkip = TRUE;
    texInitPool(&s_gevrGexHandPool, s_gevrGexHandBuf + GEVR_GEX_HAND_MODELSIZE,
                GEVR_GEX_HAND_BUFSIZE - GEVR_GEX_HAND_MODELSIZE);
    load_object_fill_header(&s_gevrGexHandHeader, (u8 *) carrier, s_gevrGexHandBuf, GEVR_GEX_HAND_MODELSIZE,
                            &s_gevrGexHandPool);
    gevrGexGunDone();
    modelCalculateRwDataLen(&s_gevrGexHandHeader);
    if (s_gevrGexHandHeader.RootNode == NULL
        || (u32) s_gevrGexHandHeader.numRecords > ARRAYCOUNT(s_gevrGexHandRw)
        || s_gevrGexHandHeader.Switches[GEVR_GEX_HAND_SW_RIGHT] == NULL)
    {
        sysLogPrintf(LOG_ERROR, "gex: hand model did not load (%d records)", s_gevrGexHandHeader.numRecords);
        return FALSE;
    }
    sysLogPrintf(LOG_NOTE, "gex: hands loaded (%u bytes, %d matrices)", len, s_gevrGexHandHeader.numMatrices);
    s_gevrGexHandReady = TRUE;
    return TRUE;
}

/* gunfire.c, after a GoldenEye X gun: its hands, ready for the gun's
 * matrices, or NULL */
Model *gevrGexHands(GUNHAND hand)
{
    ModelFileHeader *hdr = &s_gevrGexHandHeader;
    const s32 mag = hand == GUNRIGHT ? gevrGexMagState(hand, NULL) : GEVR_GEXMAG_IN;
    const s32 leftShown = mag == GEVR_GEXMAG_GRIPPED || mag == GEVR_GEXMAG_INHAND   /* it holds the magazine */
                       || (hand == GUNRIGHT && (gevrGexLeftHandShown() || gevrGexMagazineFitting()));
    const s32 dual = getCurrentPlayerWeaponId(GUNLEFT) != ITEM_UNARMED;
    const s32 watch = g_CurrentPlayer->watch_animation_state != 0;   /* its arm is up */
    s32 i;

    if (!gevrGexHandLoad())
    {
        return NULL;
    }
    modelInit(&s_gevrGexHandModel, hdr, s_gevrGexHandRw);
    for (i = 0; i < hdr->numSwitches; i++)
    {
        s32 *visible = hdr->Switches[i] != NULL ? (s32 *) modelGetNodeRwData(&s_gevrGexHandModel, hdr->Switches[i]) : NULL;

        if (visible != NULL)
        {
            *visible = i == GEVR_GEX_HAND_SW_RIGHT || leftShown
                    || (!g_gevrStereo && !dual && !watch && (!gevrGexIsHandHeld(gevrGexWeaponForHand(hand))
                                                              || gevrGexWeaponForHand(hand)->offHandMatrix > 0));
        }
    }
    return &s_gevrGexHandModel;
}

/*
 * gunfire.c gevrRenderItemHand: a mission gadget's hand (user: GE-X's hand for
 * the gadgets, the gadget in its palm as GoldenEye's fist held it). The hand
 * model on its own skeleton (the combat hands' rig is the same) in the fist's
 * rest, 1001's frame 0, on the controller where the GE-X fist is fitted, so
 * the gadget - put into the fingers from the controller (bondview2.c
 * gevrStereoItemPose) - sits in it as in the fist. A left hand is the right
 * one mirrored, as the fist's was.
 */
Gfx *gevrGexDrawItemHand(Gfx *gdl, ModelRenderData *templ, GUNHAND hand, s32 mirror, s32 *drawn)
{
    extern s32 gevrStereoGunMatrixFit(s32 handnum, const f32 off[3], Mtxf *out);   /* bondview2.c */
    extern void matrix_4x4_7F058C64(void);
    extern void matrix_4x4_7F058C88(void);
    ModelFileHeader *hdr = &s_gevrGexHandHeader;
    const GexWeaponDef *fist = gevrGexWeaponGet(ITEM_FIST);
    ModelRenderData renderdata;
    Mtxf base;
    Mtxf *m;
    s32 i, n;

    *drawn = FALSE;
    if (!VrGexGuns || (g_gevrStereo && !gevrGexArmsOn()) || fist == NULL || fist->fireAnim <= 0
        || gevrPdAnimNumFrames(fist->fireAnim) <= 0
        || g_CurrentPlayer == NULL || !gevrGexHandLoad() || hdr->numMatrices <= GEVR_GEX_LHAND_LAST
        || hdr->numMatrices > 64)
    {
        return gdl;
    }
    if (!g_gevrStereo)
    {
        /* the screen: on the PP7's virtual controller (gevrGexScreenGadget), by the fist's fit */
        Mtxf anchor;

        if (s_gevrGexScreenRootTimer != g_GlobalTimer || hand != GUNRIGHT)
        {
            return gdl;
        }
        gevrGexScreenFitAnchor(gevrGexGunFit(ITEM_FIST), &anchor);
        matrix_4x4_multiply(&s_gevrGexScreenRoot, &anchor, &base);
    }
    else if (!gevrStereoGunMatrixFit(hand, gevrGexGunFit(ITEM_FIST), &base))
    {
        return gdl;
    }
    if (g_gevrStereo && hand == GUNLEFT)
    {
        for (i = 0; i < 4; i++)
        {
            base.m[i][0] = -base.m[i][0];   /* a left hand: mirrored in the model's own frame */
        }
    }
    for (i = 0; g_gevrStereo && i < 3; i++)
    {
        base.m[0][i] *= 0.1f;   /* the flat game's 0.1, as every viewmodel's (gunfire.c) */
        base.m[1][i] *= 0.1f;
        base.m[2][i] *= 0.1f;
    }
    n = hdr->numMatrices;
    m = (Mtxf *) dynAllocate(n * (s32) sizeof(Mtxf));
    gevrGexPoseWalk(hdr, &base, fist->fireAnim, 0.0f, m);

    modelInit(&s_gevrGexHandModel, hdr, s_gevrGexHandRw);
    for (i = 0; i < hdr->numSwitches; i++)
    {
        s32 *visible = hdr->Switches[i] != NULL ? (s32 *) modelGetNodeRwData(&s_gevrGexHandModel, hdr->Switches[i]) : NULL;

        if (visible != NULL)
        {
            *visible = i == GEVR_GEX_HAND_SW_RIGHT;
        }
    }
    s_gevrGexHandModel.render_pos = (RenderPosView *) m;
    renderdata = *templ;
    renderdata.gdl = gdl;
    renderdata.PropType = 4;
    renderdata.envcolour.word = g_CurrentPlayer->tileColor.a
                              | ((u32)g_CurrentPlayer->tileColor.r << 24)
                              | ((u32)g_CurrentPlayer->tileColor.g << 16)
                              | ((u32)g_CurrentPlayer->tileColor.b << 8);
    renderdata.zbufferenabled = 1;
    matrix_4x4_7F058C64();
    if (mirror)
    {
        gDPNoOpTag(renderdata.gdl++, 0x56580000); /* VR_CULL_MIRROR_BEGIN */
    }
    subdraw(&renderdata, &s_gevrGexHandModel);
    gdl = renderdata.gdl;
    if (mirror)
    {
        gDPNoOpTag(gdl++, 0x56580001); /* VR_CULL_MIRROR_END */
    }
    bondviewTransformManyPosToViewMatrix(s_gevrGexHandModel.render_pos, n);
    matrix_4x4_7F058C88();
    *drawn = TRUE;
    return gdl;
}

/*
 * bondview2.c's weapon panel (user: GE-X's models there too, unarmed and all):
 * its own copy of an item's model, built as GE-X's as a hand's is
 * (gevrGexGunPrepare), drawn at rest as the watch's pages draw a hand's
 * (gevrGexPoseStill). The fist shows its rig's own hands there, there being
 * no wetsuit hands drawn over it.
 */
/* before the panel loads `item` into hdr: TRUE when GE-X's model is pending for it */
s32 gevrGexPanelPrepare(s32 item, ModelFileHeader *hdr)
{
    s32 parts[64];
    u32 len = 0;
    u16 mtx = 0, tex = 0;
    s32 i;
    const s32 n = hdr->numSwitches;
    const GexWeaponDef *def = gevrGexWeaponGet(item);

    s_gevrGexPanelHdr = NULL;
    s_gevrGexPanelDef = NULL;
    if (!VrGexGuns || def == NULL || n < 0 || n + def->numParts > 64)
    {
        return FALSE;
    }
    for (i = 0; i < 64; i++)
    {
        parts[i] = -1;
    }
    if (n > 1)
    {
        parts[1] = 90;
    }
    for (i = 0; i < def->numParts; i++)
    {
        parts[n + i] = def->parts[i];
    }
    gevrGexPendingFile = gevrGexBuildModel(def->model, n + def->numParts, parts, def->texturePairs, &len, &mtx, &tex);
    if (gevrGexPendingFile == NULL || mtx > 64 || gevrPdAnimNumFrames(gevrGexRestAnim(def)) <= 0)
    {
        free(gevrGexPendingFile);
        gevrGexPendingFile = NULL;
        return FALSE;
    }
    gevrGexPendingLen = len;
    hdr->numSwitches = n + def->numParts;
    hdr->numMatrices = mtx;
    hdr->numtextures = tex;
    g_gevrHandPatchSkip = TRUE;
    s_gevrGexPanelHdr = hdr;
    s_gevrGexPanelDef = def;
    return TRUE;
}

/* after the panel's load (whether it took or not) */
void gevrGexPanelDone(s32 loaded)
{
    gevrGexGunDone();
    if (!loaded)
    {
        s_gevrGexPanelHdr = NULL;
        s_gevrGexPanelDef = NULL;
    }
}

/* gunfire.c, the panel's draw: TRUE when this is GE-X's model, posed here */
s32 gevrGexPanelPose(ModelFileHeader *hdr, Model *model, Mtxf *rwmtx)
{
    const GexWeaponDef *def = s_gevrGexPanelDef;
    s32 i;

    if (hdr == NULL || hdr != s_gevrGexPanelHdr || def == NULL)
    {
        return FALSE;
    }
    gevrGexPoseStill(hdr, model, rwmtx);
    for (i = 0; i < def->numParts; i++)
    {
        ModelNode *sw = hdr->Switches[hdr->numSwitches - def->numParts + i];
        s32 *visible = sw != NULL ? (s32 *) modelGetNodeRwData(model, sw) : NULL;

        if (visible != NULL && def->fireAnimAlt > 0)
        {
            *visible = def->parts[i] == 54;   /* the fist: its rig's own right hand */
        }
    }
    return TRUE;
}

/* the log, when what happened to an arm through the watch changes (user: it still showed GoldenEye's) */
static void gevrGexWatchNote(s32 *last, s32 code, const char *what)
{
    extern s32 g_gevrStereo;

    if (*last != code)
    {
        *last = code;
        sysLogPrintf(LOG_NOTE, "gex: watch %s (%d, watch state %d, stereo %d)", what, code,
                     g_CurrentPlayer != NULL ? g_CurrentPlayer->watch_animation_state : -1, g_gevrStereo);
    }
}

/* bondview2.c gevrRenderGexWatch: the watch it draws is the empty off hand's */
s32 g_gevrGexOffWatch;

/* gunfire.c, where the watch arm goes: the off hand, empty (gevrGexOffCache above) */
Gfx *gevrGexDrawOffHand(Gfx *gdl, ModelRenderData *templ, s32 *drawn)
{
    extern void matrix_4x4_7F058C64(void);
    extern void matrix_4x4_7F058C88(void);
    extern Gfx *gevrRenderGexWatch(Gfx *gdl, ModelRenderData *templ, const f32 pos[3], const f32 x[3],
                                   const f32 y[3]);   /* bondview2.c */
    extern s32 gevrStereoWatchItem(s32 item);   /* bondview2.c: the watch laser's and detonator's arm */
    extern Gfx *gevrRenderWatchFace(Gfx *gdl, const f32 centre[3], const f32 up[3], const f32 normal[3],
                                    f32 radius);   /* bondview2.c */
    ModelFileHeader *hdr = &s_gevrGexHandHeader;
    ModelRenderData renderdata;
    Mtxf *m;
    f32 pos[3], x[3], y[3], up[3], normal[3], radius = 0.0f;
    s32 mag, i, n, watch, face = FALSE;
    const s32 left = g_CurrentPlayer != NULL ? get_item_in_hand_or_watch_menu(GUNLEFT) : ITEM_UNARMED;
    static s32 s_note = -1;

    *drawn = FALSE;
    /* Gun-attached magazine hands own this render slot even if the optional
     * standalone GE-X arms are off. Do not append a second legacy arm. */
    if (g_CurrentPlayer != NULL && !g_CurrentPlayer->bonddead && gevrGexHeld(GUNRIGHT)
        && s_gevrGexHandReady && (left == ITEM_UNARMED || left == ITEM_SUIT_LF_HAND)
        && !gevrStereoWatchItem(get_item_in_hand_or_watch_menu(GUNRIGHT)))
    {
        mag = gevrGexMagState(GUNRIGHT, NULL);
        if (gevrGexOffHandConsumed(mag, drawn)) return gdl;
    }
    /* through the watch's pages too (user: keep GE-X's arm), but the watch's own
     * items fire from the watch arm's face (bondview2.c gevrStereoWatchPoint) */
    /* the watch items too (user): this arm wears their watch, GE-X's rig the hand at it */
    if (!gevrGexArmsOn() || !VrGexGuns || s_gevrGexOffFrom == NULL || g_CurrentPlayer == NULL
        || g_CurrentPlayer->bonddead || (left != ITEM_UNARMED && left != ITEM_SUIT_LF_HAND)
        || (gevrStereoWatchItem(get_item_in_hand_or_watch_menu(GUNRIGHT)) && !gevrGexHeld(GUNRIGHT))
        || gevrStereoTwoHandGrip())
    {
        if (g_CurrentPlayer != NULL && g_CurrentPlayer->watch_animation_state != 0)
        {
            gevrGexWatchNote(&s_note, !gevrGexArmsOn() ? 11 : s_gevrGexOffFrom == NULL ? 12
                                      : (left != ITEM_UNARMED && left != ITEM_SUIT_LF_HAND) ? 13 : 14,
                             "arm in the headset stays GoldenEye's");
        }
        return gdl;
    }
    if (g_CurrentPlayer->watch_animation_state != 0)
    {
        gevrGexWatchNote(&s_note, 10, "arm in the headset is GE-X's");
    }
    if (!gevrGexHandLoad() || hdr->numMatrices <= GEVR_GEX_LHAND_LAST)
    {
        return gdl;
    }
    n = hdr->numMatrices;
    m = (Mtxf *) dynAllocate(n * (s32) sizeof(Mtxf));
    if (s_gevrGexOffHeldTimer == g_GlobalTimer && gevrGexHeld(GUNRIGHT)
        && gevrGexWeaponForHand(GUNRIGHT)->offHandMatrix > 0)
    {
        /* holding the rig's own item (the remote mine's watch, gevrGexPoseGun):
         * GE-X's hand round it, and no second watch on the wrist */
        for (i = 0; i < n; i++)
        {
            m[i] = s_gevrGexOffHeld[i <= GEVR_GEX_LHAND_LAST ? i : 0];
        }
        watch = FALSE;
        /* its own watch carries the readout, and the look at it pauses (bondview2.c) */
        face = gevrGexFaceFrame(&s_gevrGexOffHeldItem, gevrGexWeaponForHand(GUNRIGHT)->offHandFace, pos, up,
                                normal, &radius);
    }
    else
    {
        if (!gevrGexOffHandPose(m, n))
        {
            return gdl;
        }
        watch = gevrGexWatchFrame(&m[GEVR_GEX_LHAND_ARM], pos, x, y);
    }

    modelInit(&s_gevrGexHandModel, hdr, s_gevrGexHandRw);
    for (i = 0; i < hdr->numSwitches; i++)
    {
        s32 *visible = hdr->Switches[i] != NULL ? (s32 *) modelGetNodeRwData(&s_gevrGexHandModel, hdr->Switches[i]) : NULL;

        if (visible != NULL)
        {
            *visible = i == GEVR_GEX_HAND_SW_LEFT;
        }
    }
    s_gevrGexHandModel.render_pos = (RenderPosView *) m;

    renderdata = *templ;
    renderdata.gdl = gdl;
    renderdata.PropType = 4;
    renderdata.envcolour.word = g_CurrentPlayer->tileColor.a
                              | ((u32)g_CurrentPlayer->tileColor.r << 24)
                              | ((u32)g_CurrentPlayer->tileColor.g << 16)
                              | ((u32)g_CurrentPlayer->tileColor.b << 8);
    renderdata.zbufferenabled = 1;
    matrix_4x4_7F058C64();
    subdraw(&renderdata, &s_gevrGexHandModel);
    /* its face is where the watch items' beam leaves and the hand presses (bondview2.c) */
    g_gevrGexOffWatch = TRUE;
    gdl = watch ? gevrRenderGexWatch(renderdata.gdl, &renderdata, pos, x, y)
        : face ? gevrRenderWatchFace(renderdata.gdl, pos, up, normal, radius) : renderdata.gdl;
    g_gevrGexOffWatch = FALSE;
    bondviewTransformManyPosToViewMatrix(s_gevrGexHandModel.render_pos, n);
    matrix_4x4_7F058C88();
    *drawn = TRUE;
    return gdl;
}

/*
 * The screen's watch (the pause button's raise, and every close; user: keep
 * GE-X's arm through the watch; bondview2.c bondviewRenderWatch draws the
 * pause's own watch arm): GoldenEye's arm keeps its watch, which the
 * pages are drawn on, without its hand or sleeve, and GE-X's left arm is
 * drawn under it, where the watch sits on it in the headset
 * (gevrGexWatchFrame, the other way round), in the KF7's resting grip (the
 * off hand's cache). On the screen and in the headset alike (user: the
 * screen game shows GE-X's arms too).
 */
static ModelNode *s_gevrGexSwapDl;
static Gfx *s_gevrGexSwapSaved[2];
static s32 s_gevrGexSwapCuff[10];

/* bondview2.c, round the pause's watch arm: begin hides its hand and sleeve (TRUE if it did), end puts them back */
s32 gevrGexWatchArmSwap(Model *arm, s32 begin)
{
    static s32 s_note = -1;
    ModelFileHeader *hdr = arm != NULL ? arm->obj : NULL;
    ModelNode *node;
    s32 i;

    if (!begin)
    {
        if (s_gevrGexSwapDl != NULL)
        {
            s_gevrGexSwapDl->Data->DisplayList.Primary = s_gevrGexSwapSaved[0];
            s_gevrGexSwapDl->Data->DisplayList.Secondary = s_gevrGexSwapSaved[1];
            s_gevrGexSwapDl = NULL;
        }
        for (i = 4; hdr != NULL && i < hdr->numSwitches && i < 10; i++)
        {
            s32 *visible = hdr->Switches[i] != NULL ? (s32 *) modelGetNodeRwData(arm, hdr->Switches[i]) : NULL;

            if (visible != NULL)
            {
                *visible = s_gevrGexSwapCuff[i];
            }
        }
        return FALSE;
    }
    if (hdr == NULL || !VrGexGuns || s_gevrGexOffFrom == NULL || hdr->numSwitches < 4
        || !gevrGexHandLoad())
    {
        gevrGexWatchNote(&s_note, hdr == NULL ? 1 : !VrGexGuns ? 4
                                  : s_gevrGexOffFrom == NULL ? 5 : hdr->numSwitches < 4 ? 6 : 7,
                         "arm on the screen stays GoldenEye's");
        return FALSE;
    }
    /* its hand: the first display list right under a joint, not under a switch */
    for (node = hdr->RootNode; node != NULL && s_gevrGexSwapDl == NULL;)
    {
        if ((node->Opcode & 0xff) == MODELNODE_OPCODE_DL && node->Parent != NULL
            && (node->Parent->Opcode & 0xff) == MODELNODE_OPCODE_GROUP)
        {
            s_gevrGexSwapDl = node;
        }
        else if (node->Child != NULL)
        {
            node = node->Child;
        }
        else
        {
            while (node != NULL && node->Next == NULL)
            {
                node = node->Parent;
            }
            node = node != NULL ? node->Next : NULL;
        }
    }
    if (s_gevrGexSwapDl == NULL)
    {
        gevrGexWatchNote(&s_note, 8, "arm on the screen: no hand list found");
        return FALSE;
    }
    gevrGexWatchNote(&s_note, 0, "arm on the screen is GE-X's");
    s_gevrGexSwapSaved[0] = s_gevrGexSwapDl->Data->DisplayList.Primary;
    s_gevrGexSwapSaved[1] = s_gevrGexSwapDl->Data->DisplayList.Secondary;
    s_gevrGexSwapDl->Data->DisplayList.Primary = NULL;
    s_gevrGexSwapDl->Data->DisplayList.Secondary = NULL;
    for (i = 4; i < hdr->numSwitches && i < 10; i++)
    {
        s32 *visible = hdr->Switches[i] != NULL ? (s32 *) modelGetNodeRwData(arm, hdr->Switches[i]) : NULL;

        s_gevrGexSwapCuff[i] = visible != NULL ? *visible : 0;
        if (visible != NULL)
        {
            *visible = 0;
        }
    }
    return TRUE;
}

/* bondview2.c, before the pause's watch arm: GE-X's left arm under its watch (w: its wrist, the watch's frame) */
Gfx *gevrGexArmOnWatch(Gfx *gdl, ModelRenderData *templ, const Mtxf *w)
{
    ModelFileHeader *hdr = &s_gevrGexHandHeader;
    ModelRenderData renderdata;
    Mtxf f, inv, wrist;
    Mtxf *m;
    const f32 *t = VrGexWatch;
    f32 x[3], y[3], z[3], lx = 0.0f, ly = 0.0f, k, u;
    s32 i, j, n;

    for (i = 0; i < 3; i++)
    {
        lx += w->m[0][i] * w->m[0][i];
        ly += w->m[1][i] * w->m[1][i];
    }
    lx = sqrtf(lx);
    ly = sqrtf(ly);
    if (lx < 1e-6f || ly < 1e-6f || hdr->numMatrices <= GEVR_GEX_LHAND_LAST)
    {
        return gdl;
    }
    for (i = 0; i < 3; i++)
    {
        x[i] = w->m[0][i] / lx;
        y[i] = w->m[1][i] / ly;
    }
    z[0] = x[1] * y[2] - x[2] * y[1];
    z[1] = x[2] * y[0] - x[0] * y[2];
    z[2] = x[0] * y[1] - x[1] * y[0];
    /* GE-X's units against the watch's: 0.085 cm, and 26 cm to 2392 (bondview2.c), at the watch's size */
    k = lx * (0.085f * 2392.0f / 26.0f) / (t[3] > 0.1f ? t[3] : 1.0f);
    u = k / 0.085f;
    for (i = 0; i < 3; i++)
    {
        f.m[0][i] = -z[i] * k;
        f.m[1][i] = y[i] * k;
        f.m[2][i] = x[i] * k;
        f.m[3][i] = w->m[3][i] - (t[0] * x[i] + t[1] * y[i] + t[2] * z[i]) * u - GEVR_GEX_CUFF_Z * x[i] * k;
    }
    f.m[0][3] = f.m[1][3] = f.m[2][3] = 0.0f;
    f.m[3][3] = 1.0f;
    gevrGexRigidInverse(&s_gevrGexOffChain[GEVR_GEX_LHAND_ARM], &inv);
    matrix_4x4_multiply(&f, &inv, &wrist);

    n = hdr->numMatrices;
    m = (Mtxf *) dynAllocate(n * (s32) sizeof(Mtxf));
    for (j = 0; j < n; j++)
    {
        if (j >= GEVR_GEX_LHAND_FIRST && j <= GEVR_GEX_LHAND_LAST)
        {
            matrix_4x4_multiply(&wrist, &s_gevrGexOffChain[j], &m[j]);
        }
        else
        {
            m[j] = wrist;   /* the right hand's joints: its mesh is switched off */
        }
    }
    modelInit(&s_gevrGexHandModel, hdr, s_gevrGexHandRw);
    for (i = 0; i < hdr->numSwitches; i++)
    {
        s32 *visible = hdr->Switches[i] != NULL ? (s32 *) modelGetNodeRwData(&s_gevrGexHandModel, hdr->Switches[i]) : NULL;

        if (visible != NULL)
        {
            *visible = i == GEVR_GEX_HAND_SW_LEFT;
        }
    }
    s_gevrGexHandModel.render_pos = (RenderPosView *) m;
    renderdata = *templ;
    renderdata.gdl = gdl;
    subdraw(&renderdata, &s_gevrGexHandModel);
    bondviewTransformManyPosToViewMatrix(s_gevrGexHandModel.render_pos, n);
    return renderdata.gdl;
}
#endif

void used_to_load_1st_person_model_on_demand(GUNHAND hand)
{
    u32              size_buffer_weapon;
    s8              *ptr_item_text;
    ModelFileHeader *ptr_weapon_model;
    u8              *buffer_weapon;
    enum ITEM_IDS    item;

    if (!g_CurrentPlayer->ptr_hand_weapon_buffer[hand]) return;

    if ((g_CurrentPlayer->hand_invisible[hand] < 0) && (g_CurrentPlayer->lock_hand_model[hand] == 0))
    {
        if ((g_CurrentPlayer->hand_invisible[hand] < -2) || (g_CurrentPlayer->hand_item[hand] == ITEM_UNARMED))
        {
            item             = g_CurrentPlayer->field_2A44[hand];
#ifdef GEVR
            gevrGexHandSet(hand, NULL);   /* until gevrGexGunPrepare says otherwise */
#endif
            ptr_item_text    = (s8 *)get_ptr_item_text_call_line(item);
            ptr_weapon_model = get_ptr_weapon_model_header_line(item);

            if ((ptr_item_text != NULL) && (ptr_weapon_model != NULL))
            {
                buffer_weapon      = getPlayerWeaponBufferForHand(hand);
                size_buffer_weapon = getSizeBufferWeaponInHand(hand);

                g_CurrentPlayer->copy_of_body_obj_header[hand] = *ptr_weapon_model;

                if (item == ITEM_SUIT_LF_HAND)
                {
#ifdef GEVR
                    /* D45 (gepc-ref): Csuit_lf_handZ expands to 0x16F9C. */
                    texInitPool(&g_CurrentPlayer->item_related[hand], buffer_weapon + 0x18000, size_buffer_weapon - 0x18000);
                    load_object_fill_header(&g_CurrentPlayer->copy_of_body_obj_header[hand], (u8 *)ptr_item_text, buffer_weapon, 0x18000, &g_CurrentPlayer->item_related[hand]);
#else
                    texInitPool(&g_CurrentPlayer->item_related[hand], buffer_weapon + 0xBD70, size_buffer_weapon + 0xFFFF4290);
                    load_object_fill_header(&g_CurrentPlayer->copy_of_body_obj_header[hand], (u8 *)ptr_item_text, buffer_weapon, 0xBD70, &g_CurrentPlayer->item_related[hand]);
#endif
                }
                else if ((item == ITEM_TRIGGER) || (item == ITEM_WATCHLASER))
                {
#ifdef GEVR
                    /* GoldenEye X's watch rig (user: the watch laser kept GoldenEye's arm,
                     * this branch never asked for it): built as a gun's, in a gun's room
                     * (gevrGexGunPrepare checks it fits); else GoldenEye's own arm */
                    gevrGexGunPrepare(hand, item, &g_CurrentPlayer->copy_of_body_obj_header[hand]);
                    if (gevrGexHeld(hand))
                    {
                        texInitPool(&g_CurrentPlayer->item_related[hand], &buffer_weapon[D_80032464[hand]], size_buffer_weapon - D_80032464[hand]);
                        load_object_fill_header(&g_CurrentPlayer->copy_of_body_obj_header[hand], (u8 *)ptr_item_text, buffer_weapon, D_80032464[hand], &g_CurrentPlayer->item_related[hand]);
                        gevrGexGunDone();
                        if (hand == GUNRIGHT && !(netIsActive() && get_cur_playernum() != netGetLocalSlot()))
                        {
                            gevrGexOffCache(&g_CurrentPlayer->copy_of_body_obj_header[hand]);
                        }
                    }
                    else
                    {
                    gevrGexGunDone();
                    /* D45 (gepc-ref): GtriggerZ / GwatchlaserZ expand to 0x16030. */
                    texInitPool(&g_CurrentPlayer->item_related[hand], buffer_weapon + 0x17000, size_buffer_weapon - 0x17000);
                    load_object_fill_header(&g_CurrentPlayer->copy_of_body_obj_header[hand], (u8 *)ptr_item_text, buffer_weapon, 0x17000, &g_CurrentPlayer->item_related[hand]);
                    }
#else
                    texInitPool(&g_CurrentPlayer->item_related[hand], buffer_weapon + 0xAFD0, size_buffer_weapon + 0xFFFF5030);
                    load_object_fill_header(&g_CurrentPlayer->copy_of_body_obj_header[hand], (u8 *)ptr_item_text, buffer_weapon, 0xAFD0, &g_CurrentPlayer->item_related[hand]);
#endif
                }
                else
                {
                    texInitPool(&g_CurrentPlayer->item_related[hand], &buffer_weapon[D_80032464[hand]], size_buffer_weapon - D_80032464[hand]);
#ifdef GEVR
                    gevrGexGunPrepare(hand, item, &g_CurrentPlayer->copy_of_body_obj_header[hand]);
#endif
                    load_object_fill_header(&g_CurrentPlayer->copy_of_body_obj_header[hand], (u8 *)ptr_item_text, buffer_weapon, D_80032464[hand], &g_CurrentPlayer->item_related[hand]);
#ifdef GEVR
                    gevrGexGunDone();
                    /* the new gun's empty off hand now, not at its first draw after the switch */
                    if (hand == GUNRIGHT && gevrGexHeld(GUNRIGHT)
                        && !(netIsActive() && get_cur_playernum() != netGetLocalSlot()))
                    {
                        gevrGexOffCache(&g_CurrentPlayer->copy_of_body_obj_header[hand]);
                    }
                    /* the prop the hand holds as this gun's payload, loaded as GoldenEye loads a projectile's */
                    if (gevrGexHeld(hand) && gevrGexWeaponForHand(hand)->payloadProp > 0)
                    {
                        extern s32 modelLoad(s32 modelid);

                        modelLoad(gevrGexWeaponForHand(hand)->payloadProp);
                    }
#endif
                }
            }

            g_CurrentPlayer->hand_invisible[hand] = 1;
            g_CurrentPlayer->hand_item[hand]      = item;
            g_CurrentPlayer->field_2A44[hand]     = -1;
        }
        else
        {
            g_CurrentPlayer->hand_invisible[hand]--;
        }
    }
}


// Called by unused functions.
ITEM_IDS sub_GAME_7F05D334(ITEM_IDS item, s32 arg1)
{
    while (arg1 > 0)
    {
        do
        {
            item = (item + 1) % ITEM_BOMBCASE;
        } while (bondinvItemAvailable(item) == 0);
        arg1--;
    }

    while (arg1 < 0)
    {
        do
        {
            item--;
            if (item < 0)
            {
                item = 0x20 - (-(item + 1) % ITEM_BOMBCASE);
            }
        } while (bondinvItemAvailable(item) == 0);
        arg1++;
    }

    return item;
}


ITEM_IDS get_next_weapon_in_cycle_for_hand(GUNHAND hand, s32 direction)
{
	if (g_CurrentPlayer->hands[hand].weapon_action_state == GUN_ANIM_STATE_SWITCH_LOWER) 
    {
		if (
			(direction < 0 && (g_CurrentPlayer->hands[hand].field_8B8 > 0)) ||
			(direction > 0 && (g_CurrentPlayer->hands[hand].field_8B8 < 0)) ) 
        {
			return getCurrentPlayerWeaponId(hand);
		}
		else 
        {
			return g_CurrentPlayer->hands[hand].weapon_next_weapon;
		}

    }
    
    if (g_CurrentPlayer->hands[hand].weapon_action_state == GUN_ANIM_STATE_SWITCH_SWAP) 
    {
        return g_CurrentPlayer->hands[hand].weapon_next_weapon;
    }

    return getCurrentPlayerWeaponId(hand);
}


void gunRequestHandWeaponChange(enum GUNHAND hand, s32 nextWeapon, s32 cycleDirection)
{
    if ((g_CurrentPlayer->hands[hand].weapon_action_state == GUN_ANIM_STATE_SWITCH_LOWER) || (g_CurrentPlayer->hands[hand].weapon_action_state == GUN_ANIM_STATE_SWITCH_SWAP))
    {
        g_CurrentPlayer->hands[hand].field_8B0 = g_CurrentPlayer->hands[hand].field_890;

#ifdef VERSION_EU
        if (getPlayerCount() == 1) {
            g_CurrentPlayer->hands[hand].field_8B0 += 0xE;
        } else {
            g_CurrentPlayer->hands[hand].field_8B0 += 0xA;
        }
#else
        if (getPlayerCount() == 1) {
            g_CurrentPlayer->hands[hand].field_8B0 += 0x11;
        } else {
            g_CurrentPlayer->hands[hand].field_8B0 += 0xD;
        }
#endif
    }

    /* A tap can reverse a queued request before its lowering tick starts. */
    s32 selected = get_next_weapon_in_cycle_for_hand(hand, 0);
#ifdef GEVR
    extern s32 g_gevrStereo;
    if (g_gevrStereo && g_CurrentPlayer->hands[hand].weapon_animation_trigger)
        selected = g_CurrentPlayer->hands[hand].weapon_next_weapon;
#endif
    if (selected != nextWeapon)
    {
        if ((g_CurrentPlayer->hands[hand].weapon_action_state != GUN_ANIM_STATE_SWITCH_LOWER) && (g_CurrentPlayer->hands[hand].weapon_action_state != GUN_ANIM_STATE_SWITCH_SWAP))
        {
            g_CurrentPlayer->hands[hand].weapon_current_animation = 5;
        }

        g_CurrentPlayer->hands[hand].weapon_next_weapon = nextWeapon;
        g_CurrentPlayer->hands[hand].weapon_animation_trigger = 1;
        g_CurrentPlayer->hands[hand].field_8B8 = cycleDirection;
    }
}


// Unused
void sub_GAME_7F05D610(GUNHAND hand)
{
    gunRequestHandWeaponChange(hand, sub_GAME_7F05D334(get_next_weapon_in_cycle_for_hand(hand, 0), 1), 0);
}


// Unused
void sub_GAME_7F05D650(GUNHAND hand)
{
    gunRequestHandWeaponChange(hand, sub_GAME_7F05D334(get_next_weapon_in_cycle_for_hand(hand, 0), -1), 0);
}


void sub_GAME_7F05D690(void)
{
    currentPlayerEquipWeaponWrapper(GUNRIGHT, g_CurrentPlayer->hands[GUNRIGHT].previous_weapon);
    currentPlayerEquipWeaponWrapper(GUNLEFT, g_CurrentPlayer->hands[GUNLEFT].previous_weapon);
}


void advance_through_inventory(void)
{
    ITEM_IDS nextright;
    ITEM_IDS nextleft;

    nextright = get_next_weapon_in_cycle_for_hand(GUNRIGHT, 1);
    nextleft = get_next_weapon_in_cycle_for_hand(GUNLEFT, 1);

    if ((nextright >= ITEM_BOMBCASE) || (nextleft >= ITEM_BOMBCASE))
    {
        nextright = g_CurrentPlayer->hands[GUNRIGHT].previous_weapon;
        nextleft = g_CurrentPlayer->hands[GUNLEFT].previous_weapon;
    }
    else
    {
        bondinvCycleForward(&nextright, &nextleft, FALSE);
    }

    gunRequestHandWeaponChange(GUNRIGHT, nextright, 1);
    gunRequestHandWeaponChange(GUNLEFT, nextleft, 1);
}


void backstep_through_inventory(void)
{
    ITEM_IDS nextright;
    ITEM_IDS nextleft;

    nextright = get_next_weapon_in_cycle_for_hand(GUNRIGHT, -1);
    nextleft = get_next_weapon_in_cycle_for_hand(GUNLEFT, -1);

    if ((nextright >= ITEM_BOMBCASE) || (nextleft >= ITEM_BOMBCASE))
    {
        nextright = g_CurrentPlayer->hands[GUNRIGHT].previous_weapon;
        nextleft = g_CurrentPlayer->hands[GUNLEFT].previous_weapon;
    }
    else
    {
        bondinvCycleBackward(&nextright, &nextleft, FALSE);
    }

    gunRequestHandWeaponChange(GUNRIGHT, nextright, -1);
    gunRequestHandWeaponChange(GUNLEFT, nextleft, -1);
}

void autoadvance_on_deplete_all_ammo(void)
{
#ifdef GEVR
    extern s32 g_gevrStereo;
    if (gevrGexMinesHold(GUNRIGHT))
    {
        return;   /* GE-X's remote mines: the watch detonates them, the hand stays */
    }
    if (g_gevrStereo)
    {
        gevrAutoAdvanceHand(GUNRIGHT);
        gevrAutoAdvanceHand(GUNLEFT);
        return;
    }
#endif

	ITEM_IDS nextright;
	ITEM_IDS nextleft;
	ITEM_IDS duperight;
	ITEM_IDS dupeleft;

    nextright = get_next_weapon_in_cycle_for_hand(GUNRIGHT, 1);
    duperight = nextright;

    nextleft = get_next_weapon_in_cycle_for_hand(GUNLEFT, 1);
    dupeleft = nextleft;

    if ((duperight >= ITEM_BOMBCASE) || (dupeleft >= ITEM_BOMBCASE))
    {
        duperight = g_CurrentPlayer->hands[GUNRIGHT].previous_weapon;
        dupeleft = g_CurrentPlayer->hands[GUNLEFT].previous_weapon;
    }
    else if ((duperight == ITEM_REMOTEMINE) && ((bondinvItemAvailable(ITEM_TRIGGER)))
#ifdef GEVR
             && !VrGexGuns   /* GE-X's remote mines detonate themselves: no Detonator */
#endif
             )
    {
        duperight = ITEM_TRIGGER;
        dupeleft = ITEM_UNARMED;
    }
    else
    {
        bondinvCycleForward(&duperight, &dupeleft, TRUE);

        if ((duperight < nextright) || ((duperight == nextright) && (nextleft >= dupeleft)))
        {
			duperight = nextright;
			dupeleft = nextleft;
			bondinvCycleBackward(&duperight, &dupeleft, TRUE);
        }
    }

    gunRequestHandWeaponChange(GUNRIGHT, duperight, 1);
    gunRequestHandWeaponChange(GUNLEFT, dupeleft, 1);
}

s32 currentPlayerEquipWeaponWrapper(GUNHAND hand, s32 next_weapon) {
    g_CurrentPlayer->hands[hand].weapon_current_animation = 5;
    g_CurrentPlayer->hands[hand].weapon_next_weapon = next_weapon;
    g_CurrentPlayer->hands[hand].weapon_animation_trigger = 0;
}

void attempt_reload_item_in_hand(GUNHAND hand) {
    s32 ammo_type = get_ammo_type_for_weapon(getCurrentPlayerWeaponId(hand));
    if (ammo_type != 0) {
        if (g_CurrentPlayer->hands[hand].weapon_current_animation == 0) {
            g_CurrentPlayer->hands[hand].weapon_current_animation = 9;
        }
    }
}

ITEM_IDS getCurrentPlayerWeaponId(GUNHAND hand) {
    return g_CurrentPlayer->hands[hand].weaponnum;
}

void draw_item_in_hand(GUNHAND hand, s32 next_weapon) {
	g_CurrentPlayer->hands[hand].weapon_current_animation = 0xE;
	g_CurrentPlayer->hands[hand].weapon_next_weapon = next_weapon;
}

ITEM_IDS get_item_in_hand_or_watch_menu(GUNHAND hand) {
	if (g_CurrentPlayer->hands[hand].weaponnum_watchmenu >= 0) {
		return g_CurrentPlayer->hands[hand].weaponnum_watchmenu;
	} else {
		return g_CurrentPlayer->hands[hand].weaponnum;
	}
}

void sub_GAME_7F05DA8C(GUNHAND hand, ITEM_IDS weaponnum_watchmenu) {
    place_item_in_hand_swap_and_make_visible(hand, weaponnum_watchmenu);
	g_CurrentPlayer->hands[hand].weaponnum_watchmenu = weaponnum_watchmenu;
}

void sub_GAME_7F05DAE4(GUNHAND hand) {
    if (g_CurrentPlayer->hands[hand].weaponnum_watchmenu >= 0) {
        place_item_in_hand_swap_and_make_visible(hand, g_CurrentPlayer->hands[hand].weaponnum);
		g_CurrentPlayer->hands[hand].weaponnum_watchmenu = -1;
    }
}


void currentPlayerUnEquipWeaponWrapper(enum GUNHAND hand, enum ITEM_IDS weapid)
{
    enum ITEM_IDS weapon_num;
    s32 ammo_type;

    weapon_num = g_CurrentPlayer->hands[hand].weaponnum;
    ammo_type = get_ammo_type_for_weapon(weapon_num);

    if (g_CurrentPlayer->hands[hand].weaponnum_watchmenu < 0)
    {
        place_item_in_hand_swap_and_make_visible(hand, weapid);
    }

    if (g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine > 0)
    {
        g_CurrentPlayer->ammoheldarr[ammo_type] += g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine;
    }

    if (weapon_num < ITEM_BOMBCASE)
    {
        g_CurrentPlayer->hands[hand].previous_weapon = weapon_num;
    }

    if (getPlayerCount() >= 2)
    {
        sub_GAME_7F09B368(hand);
    }

    sub_GAME_7F05FB00(hand);
    g_CurrentPlayer->hands[hand].weaponnum = weapid;
    g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine = 0;
    g_CurrentPlayer->hands[hand].field_A4C = 0;
    g_CurrentPlayer->hands[hand].field_A50 = 0;
    bondinvDetermineEquippedItem();
}


s8 get_hands_firing_status(GUNHAND hand) {
    return g_CurrentPlayer->hands[hand].weapon_firing_status;
}

f32 sub_GAME_7F05DCB8(GUNHAND hand) {
	return g_CurrentPlayer->hands[hand].field_A34;
}

/**
 * Positions the gun to the right or left side of the screen depending on which hand is holding it.
 */
f32 gunSetHorizontalOffset(GUNHAND hand)
{
	f32 offset;

	if (hand == GUNRIGHT)
	{
		offset = get_ptr_item_statistics(get_item_in_hand_or_watch_menu(GUNRIGHT))->PosX;
	}
	else
	{
		offset = -get_ptr_item_statistics(get_item_in_hand_or_watch_menu(GUNLEFT))->PosX;
	}

	return offset;
}

f32 get_item_in_hand_zoom(void) {
    if (get_item_in_hand_or_watch_menu(GUNRIGHT) == ITEM_SNIPERRIFLE) {
#ifdef GEVR
        /* the screen: the N64's closest, whatever the scope zoomed to (issue #58) */
        if (g_CurrentPlayer->sniper_zoom < 7.0f) {
            return 7.0f;
        }
#endif
        return g_CurrentPlayer->sniper_zoom;
    }
    if (get_item_in_hand_or_watch_menu(GUNRIGHT) == ITEM_CAMERA) {
        return g_CurrentPlayer->camera_zoom;
    }
    return get_ptr_item_statistics(get_item_in_hand_or_watch_menu(GUNRIGHT))->Zoom;
}

#ifdef GEVR
/* the sniper in either hand: in stereo the left hand can carry it, with its own scope */
static s32 gevrSniperHeld(void)
{
    extern s32 g_gevrStereo;

    return get_item_in_hand_or_watch_menu(GUNRIGHT) == ITEM_SNIPERRIFLE
        || (g_gevrStereo && get_item_in_hand_or_watch_menu(GUNLEFT) == ITEM_SNIPERRIFLE);
}
#else
#define gevrSniperHeld() (get_item_in_hand_or_watch_menu(GUNRIGHT) == ITEM_SNIPERRIFLE)
#endif

void camera_sniper_zoom_out(f32 zoom)
{
	if (gevrSniperHeld()) {
		g_CurrentPlayer->sniper_zoom *= (1.0f + (zoom * 0.1f));
#ifdef GEVR
		{
			/*
			 * Issue #58: in stereo no wider than the scope starts, the
			 * sniper's own zoom (4.4x). Out to the N64's 60 degrees the lens
			 * showed the world at its own size, which the eyes see round it
			 * anyway (user: "should be more zoomed than just normal").
			 */
			extern s32 g_gevrStereo;

			if (g_gevrStereo && g_CurrentPlayer->sniper_zoom > sniperrifle_stats.Zoom) {
				g_CurrentPlayer->sniper_zoom = sniperrifle_stats.Zoom;
			}
		}
#endif
		if (g_CurrentPlayer->sniper_zoom > 60.0f) {
			g_CurrentPlayer->sniper_zoom = 60.0f;
		}
	}
	else
	{
		if (get_item_in_hand_or_watch_menu(GUNRIGHT) == ITEM_CAMERA) {
			g_CurrentPlayer->camera_zoom *= (1.0f + (zoom * 0.1f));
			if (g_CurrentPlayer->camera_zoom > 60.0f) {
				g_CurrentPlayer->camera_zoom = 60.0f;
			}
		}
	}
}

void camera_sniper_zoom_in(f32 zoom)
{
	if (gevrSniperHeld()) {
		g_CurrentPlayer->sniper_zoom /= (1.0f + (zoom * 0.1f));
#ifdef GEVR
		{
			/*
			 * Issue #58: in stereo on to 2.65 degrees, 25x, past the
			 * N64's 7 degrees (9.4x). That filled the screen; the lens fills
			 * a fifth of the view. 15x (4.4 degrees, the figure players
			 * quote) still was not as much as it could (user).
			 */
			extern s32 g_gevrStereo;
			f32 closest = g_gevrStereo ? 2.65f : 7.0f;

			if (g_CurrentPlayer->sniper_zoom < closest) {
				g_CurrentPlayer->sniper_zoom = closest;
			}
		}
#else
		if (g_CurrentPlayer->sniper_zoom < 7.0f) {
			g_CurrentPlayer->sniper_zoom = 7.0f;
		}
#endif
	}
	else
	{
		if (get_item_in_hand_or_watch_menu(GUNRIGHT) == ITEM_CAMERA) {
			g_CurrentPlayer->camera_zoom /= (1.0f + (zoom * 0.1f));
			if (g_CurrentPlayer->camera_zoom < 7.0f) {
				g_CurrentPlayer->camera_zoom = 7.0f;
			}
		}
	}
}

f32 gunItemGetDestructionAmount(ITEM_IDS item)
{
  return get_ptr_item_statistics(item)->DestructionAmount;
}


f32 bondwalkItemGetForceOfImpact(ITEM_IDS item)
{
	return get_ptr_item_statistics(item)->ForceOfImpact;
}

/**
 * Address 0x7F05DFCC
 */
s8 bondwalkItemGetAutomaticFiringRate(ITEM_IDS item) {
    return get_ptr_item_statistics(item)->AutomaticFiringRate;
}


u8 bondwalkItemGetSoundTriggerRate(ITEM_IDS item) {
    return get_ptr_item_statistics(item)->SoundTriggerRate;
}


u16 bondwalkItemGetSound(ITEM_IDS item)
{
  return get_ptr_item_statistics(item)->Sound;
}


u8 bondwalkItemGetObjectsShootThrough(ITEM_IDS item)
{
  return get_ptr_item_statistics(item)->ObjectsShootThrough;
}


s32 bondwalkItemHasAmmo(ITEM_IDS item)
{
#ifdef GEVR
    /* GE-X's remote mines (gevrGexMineDetonates): the Detonator is theirs, out of
     * the lists; the mines stay while any are out to detonate */
    if (VrGexGuns && item == ITEM_TRIGGER)
    {
        return 0;
    }
    if (VrGexGuns && item == ITEM_REMOTEMINE && get_ammo_count_for_weapon(item) <= 0
        && bondinvItemAvailable(ITEM_TRIGGER) && gevrGexRemoteMinesOut())
    {
        return 1;
    }
#endif
    if (bondwalkItemCheckBitflags(item, WEAPONSTATBITFLAG_HAS_AMMO) != 0)
    {
        if ((get_ammo_type_for_weapon(item) == 0) || (get_ammo_count_for_weapon(item) > 0))
        {
            return 1;
        }
    }
    return 0;
}


u32 bondwalkItemCheckBitflags(ITEM_IDS item, u32 mask)
{
  return ((get_ptr_item_statistics(item)->BitFlags & mask) != 0);
}


void gunSetBondWeaponSway(f32 breathing, f32 arg1, f32 arg2, f32 arg3)
{
    f32 dampt[2];
    s32 i;
    u32 unused[2];
    f32 sp50 = arg2;
    f32 sp4c;
    u32 stack2;
    f32 minbreathing;

    if (sp50 < 0.0f) { sp50 = -sp50; }

    if (arg1 > 0.8f)
    {
        g_CurrentPlayer->gunposamplitude = 1.0f;
    }
    else
    {
        if (arg1 > 0.1f)
        {
            f32 tmp = (1.0f - cosf((arg1 - 0.1f) * M_TAU_F / 2.8f));
            g_CurrentPlayer->gunposamplitude = 0.8f * tmp + 0.2f;
        }
        else
        {
            g_CurrentPlayer->gunposamplitude = 0.1f;
        }
    }

    if (g_CurrentPlayer->gunposamplitude < (bondviewGetBondBreathing() * 0.3f))
    {
        g_CurrentPlayer->gunposamplitude = bondviewGetBondBreathing() * 0.3f;
    }

    if (g_CurrentPlayer->gunposamplitude < 0.5f * sp50)
    {
        g_CurrentPlayer->gunposamplitude = 0.5f * sp50;
    }

    for (i = 0; i < g_ClockTimer; i++)
    {
        g_CurrentPlayer->field_1080 = (g_CurrentPlayer->field_1080 * (PAL ? 0.9403f : 0.95f)) + g_CurrentPlayer->gunposamplitude;
    }

    g_CurrentPlayer->gunposamplitude = g_CurrentPlayer->field_1080 * (PAL ? 0.059700012f : 0.050000012f);

    minbreathing = 0.016666668f * sp50;
    if (breathing < minbreathing)
    {
        breathing = minbreathing;
    }

    for (i = 0; i < g_ClockTimer; i++)
    {
        g_CurrentPlayer->field_107C = (g_CurrentPlayer->field_107C * (PAL ? 0.9403f : 0.95f)) + breathing;
    }

    breathing = g_CurrentPlayer->field_107C * (PAL ? 0.059700012f : 0.050000012f);

    sp4c = breathing * g_GlobalTimerDelta;
    dampt[0] = g_CurrentPlayer->hands[0].dampt + sp4c;

    while (dampt[0] >= 1.0f)
    {
        bgunCalculateBlend(GUNRIGHT);
        dampt[0] -= 1.0f;
        g_CurrentPlayer->syncoffset++;
    }

    g_CurrentPlayer->synccount += g_GlobalTimerDelta;

    if (g_CurrentPlayer->synccount > 60.0f)
    {
        g_CurrentPlayer->synccount = 0.0f;
        g_CurrentPlayer->syncchange = (RANDOMFRAC() - 0.5f) * 0.2f / 60.0f;
    }

    if (g_CurrentPlayer->syncchange + sp4c > 0.0f)
    {
        g_CurrentPlayer->gunsync += g_CurrentPlayer->syncchange;
    }

    if (g_CurrentPlayer->gunsync > 0.5f)
    {
        g_CurrentPlayer->gunsync = 0.5f;
    }
    else if (g_CurrentPlayer->gunsync < -0.5f)
    {
        g_CurrentPlayer->gunsync = -0.5f;
    }
    else if (g_CurrentPlayer->gunsync < 0.1f && g_CurrentPlayer->gunsync > -0.1f)
    {
        if (g_CurrentPlayer->gunsync > 0.0f)
        {
            g_CurrentPlayer->gunsync = -0.1f;
        }
        else
        {
            g_CurrentPlayer->gunsync = 0.1f;
        }
    }

    dampt[1] = dampt[0] + g_CurrentPlayer->syncoffset + g_CurrentPlayer->gunsync;

    while (dampt[1] >= 1.0f)
    {
        bgunCalculateBlend(GUNLEFT);
        dampt[1] -= 1.0f;
        g_CurrentPlayer->syncoffset--;
    }

    for (i = 0; i < 2; i++)
    {
        g_CurrentPlayer->hands[i].dampt = dampt[i];
        g_CurrentPlayer->hands[i].weapon_theta_displacement = -1.75f * arg3;
        g_CurrentPlayer->hands[i].weapon_verta_displacement = -2.0f * arg2;
    }
}


void gunSetOffsetRelated(f32 param_1)
{
    g_CurrentPlayer->hands[GUNRIGHT].gunofs2_z = (1.0f - cosf(param_1)) * 5.0f;
    g_CurrentPlayer->hands[GUNLEFT].gunofs2_z = (1.0f - cosf(param_1)) * 5.0f;
}


f32 get_value_if_watch_is_on_hand_or_not(GUNHAND hand)
{
  if ((getCurrentPlayerWeaponId(hand) == ITEM_TRIGGER) || (getCurrentPlayerWeaponId(hand) == ITEM_WATCHLASER))
  {
    return 0.08726647f;
  }
  else
  {
    return 0.17453294f;
  }
}


void sub_GAME_7F05E6B4(enum GUNHAND hand, s32 arg1)
{
    if (arg1 != 0)
    {
        if (g_CurrentPlayer->hands[hand].field_A84 < get_value_if_watch_is_on_hand_or_not(hand))
        {
            g_CurrentPlayer->hands[hand].field_A84 += (0.029088823f * g_GlobalTimerDelta);
        }
        if (g_CurrentPlayer->hands[hand].field_A84 > get_value_if_watch_is_on_hand_or_not(hand)) {
            g_CurrentPlayer->hands[hand].field_A84 = get_value_if_watch_is_on_hand_or_not(hand);
        }
    }
    else
    {
        if (g_CurrentPlayer->hands[hand].field_A84 > 0.0f)
        {
            g_CurrentPlayer->hands[hand].field_A84 -= (0.017453294f * g_GlobalTimerDelta);
        }
        if (g_CurrentPlayer->hands[hand].field_A84 < 0.0f)
        {
            g_CurrentPlayer->hands[hand].field_A84 = 0.0f;
        }
    }
}


void sub_GAME_7F05E808(GUNHAND hand) {
	g_CurrentPlayer->hands[hand].field_A8C = 1;
}


void sub_GAME_7F05E83C(GUNHAND hand)
{
    f32 recoil_back;

    recoil_back = get_ptr_item_statistics(get_item_in_hand_or_watch_menu(hand))->BoltRecoilBack;

    if (g_CurrentPlayer->hands[hand].field_A8C != 0)
    {
        if (g_CurrentPlayer->hands[hand].field_A88 < recoil_back)
        {
            g_CurrentPlayer->hands[hand].field_A88 = (g_CurrentPlayer->hands[hand].field_A88 + (recoil_back * 0.25f * g_GlobalTimerDelta));

        }
        if (recoil_back <= g_CurrentPlayer->hands[hand].field_A88) {
            g_CurrentPlayer->hands[hand].field_A88 = recoil_back;
            g_CurrentPlayer->hands[hand].field_A8C = 0;
        }
    }
    else if (g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine > 0)
    {
        if (g_CurrentPlayer->hands[hand].field_A88 > 0.0f)
        {
            g_CurrentPlayer->hands[hand].field_A88 = (g_CurrentPlayer->hands[hand].field_A88 - (recoil_back * 0.16666667f * g_GlobalTimerDelta));

        }
        if (g_CurrentPlayer->hands[hand].field_A88 < 0.0f)
        {
            g_CurrentPlayer->hands[hand].field_A88 = 0.0f;
        }
    }
}


void sub_GAME_7F05E978(Model* model, s32 val)
{
    if (model->obj->Switches[8] != NULL)
    {
        modelGetNodeRwData(model, model->obj->Switches[8])->DisplayList.unk00 = val;
    }

    if (model->obj->Switches[9] != NULL)
    {
        modelGetNodeRwData(model, model->obj->Switches[9])->DisplayList.unk00 = val;
    }

    if (model->obj->Switches[10] != NULL)
    {
        modelGetNodeRwData(model, model->obj->Switches[10])->DisplayList.unk00 = val;
    }

    if (model->obj->Switches[11] != NULL)
    {
        modelGetNodeRwData(model, model->obj->Switches[11])->DisplayList.unk00 = val;
    }

    if (model->obj->Switches[12] != NULL)
    {
        modelGetNodeRwData(model, model->obj->Switches[12])->DisplayList.unk00 = val;
    }

    if (model->obj->Switches[13] != NULL)
    {
        modelGetNodeRwData(model, model->obj->Switches[13])->DisplayList.unk00 = val;
    }

    if (model->obj->numSwitches >= 0x24)
    {
        if (model->obj->Switches[35] != NULL)
        {
            modelGetNodeRwData(model, model->obj->Switches[35])->DisplayList.unk00 = val;
        }
    }
}


void sub_GAME_7F05EA94(Model* model, s32 val)
{
    ModelNode* switch_14;
    ModelNode* switch_15;

    if (model->obj->numSwitches >= 0x10)
    {
        switch_14 = model->obj->Switches[14];
        if (switch_14 != NULL)
        {
            // Guessing DisplayList here
            modelGetNodeRwData(model, switch_14)->DisplayList.unk00 = val;
        }

        switch_15 = model->obj->Switches[15];
        if (switch_15 != NULL)
        {
            // Guessing DisplayList here
            modelGetNodeRwData(model, switch_15)->DisplayList.unk00 = val;
        }
    }
}


/**
 * Address 0x7F05EB0C.
*/
void gunInitProjectileObject(ObjectRecord *obj, coord3d *pos, StandTile *stan, Mtxf *matrix, coord3d *velocity, Mtxf *arg5, PropRecord *owner)
{
    PropRecord *temp_s1;
    Projectile *temp_v0;

    temp_s1 = obj->prop;

    if (temp_s1 != NULL)
    {
        chrpropActivate(temp_s1);
        chrpropEnable(temp_s1);
        matrix_scalar_multiply(obj->model->scale, matrix);
        objChangeShading(obj, pos, matrix, stan);

        // loadobjectmodel.c
        setupUpdateObjectRoomPosition(obj);

        chrobjCollisionRelated(obj);
        sub_GAME_7F03FDA8(temp_s1);

        if (obj->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE)
        {
            temp_v0 = obj->projectile;
            temp_v0->flags |= 0x41;
            obj->projectile->ownerprop = owner;
            projectileSetSticky(temp_s1);
            matrix_4x4_copy(arg5, &obj->projectile->mtx);
            obj->projectile->speed.f[0] = velocity->f[0];
            obj->projectile->speed.f[1] = velocity->f[1];
            obj->projectile->speed.f[2] = velocity->f[2];
            obj->projectile->obj = obj;
            obj->projectile->unkE8 = D_80048380;
        }
    }
}


/**
 * Address: 7F05EC1C
 *
 * Determines where the projectile may safely enter the world. Ideally that is the targetpos position, but if targetpos
 * is obstructed the player's position used as a fallback. This prevents the player from launching
 * projectiles through nearby surfaces.
 */
void gunInitProjectileFromPlayer(ObjectRecord *obj, coord3d *targetpos, Mtxf *arg2, coord3d *velocity, Mtxf *arg4)
{
    PropRecord *playerprop;
    coord3d pos;
    StandTile *tile;
    u32 pad_c[2];
    f32 yhi;
    f32 ylo;
    s32 usedfallback;
    f32 stanheight;
    u8 rooms[2];
    s32 pad_rooms;
    u8 pad_a[0x4c];
    s32 sp54;
    s32 sp50;
    s32 pad_sp;

    // fake
    if (obj->prop);
    if (obj->prop == NULL) {
        return;
    }

    playerprop = getCurrentPlayerProp();
    stanheight = bondviewGetPlayerStanHeight(g_CurrentPlayer);

    usedfallback = 0;

    if (targetpos->y < playerprop->pos.y) {
        yhi = playerprop->pos.y - stanheight;
        ylo = targetpos->y - stanheight;
    }
    else
    {
        yhi = targetpos->y - stanheight;
        ylo = playerprop->pos.y - stanheight;
    }

    tile = playerprop->stan;
    bondviewUpdateGuardTankFlagsRelated(playerprop, 0);

    // If there is no obstruction, spawn the projectile at the target position.
    if (stanTestLineUnobstructed(&tile, playerprop->pos.x, playerprop->pos.z, targetpos->x, targetpos->z, 0x1f, yhi, ylo, 0.0f, 1.0f))
    {
        pos.x = targetpos->x;
        pos.y = targetpos->y;
        pos.z = targetpos->z;
    }
    // Otherwise spawn it from the player's position.
    else
    {
        tile = playerprop->stan;
        pos.x = playerprop->pos.x;
        pos.y = playerprop->pos.y;
        pos.z = playerprop->pos.z;
        usedfallback = 1;
    }

    bondviewUpdateGuardTankFlagsRelated(playerprop, 1);

    gunInitProjectileObject(obj, &pos, tile, arg2, velocity, arg4, playerprop);

    if (obj->runtime_bitflags & 0x80) {
        if (usedfallback) {
            obj->projectile->flags |= PROJECTILEFLAG_00000100;
            ((coord3d *)&obj->projectile->unkd4)->x = targetpos->x;
            ((coord3d *)&obj->projectile->unkd4)->y = targetpos->y;
            ((coord3d *)&obj->projectile->unkd4)->z = targetpos->z;
        }

        rooms[0] = bondviewGetCurrentPlayersRoom();
        rooms[1] = 0xff;

        bgFindRoomsAlongSegment(bondviewGetCurrentPlayersPosition3(), &pos, rooms, obj->projectile->unkCC, &sp54, &sp50, 0x14);
    }
}


/**
 * Address 0x7F05EE24 (NTSC)
 * Address 0x7F05F2DC (PAL)
*/
void generate_player_thrown_grenade(s32 hand)
{
    s32 padding;
    Mtxf spFC;
    struct coord3d throw_speed_vec;
    f32 base_velocity;
    struct coord3d spE0;
    Mtxf spA0_a;
    struct WeaponObjRecord *wor;
    s32 new_prop_type;
    s32 sp94; // sp148
    struct coord3d base_speed_vec; // sp136
    struct PropRecord* player_prop; // sp132
    struct coord3d *bondprevpos;  // sp128
    Mtxf sp40_f;
    ALSoundState *sfx_state;
    s32 current_weapon;
    s32 unused;

    wor = NULL;
    base_velocity = 16.666666f;

    player_prop = getCurrentPlayerProp();
    bondprevpos = getCurrentPlayerPrevPos();
    current_weapon = getCurrentPlayerWeaponId(hand);

    sub_GAME_7F057C14(&throw_speed_vec, &spFC);
    bullet_path_from_screen_center(&sp94, &base_speed_vec, hand);
    mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), (f32*)&base_speed_vec);

#ifdef GEVR
    if (g_gevrMotionThrowActive[hand])
    {
        throw_speed_vec.f[0] = g_gevrMotionThrowVel[hand].x;
        throw_speed_vec.f[1] = g_gevrMotionThrowVel[hand].y;
        throw_speed_vec.f[2] = g_gevrMotionThrowVel[hand].z;
        g_gevrMotionThrowActive[hand] = 0;
    }
    else
#endif
    {
        throw_speed_vec.f[0] = (base_speed_vec.f[0] * base_velocity);
        throw_speed_vec.f[1] = (base_speed_vec.f[1] * base_velocity) + 5.0f;
        throw_speed_vec.f[2] = (base_speed_vec.f[2] * base_velocity);
    }

    if (g_ClockTimer > 0)
    {
        throw_speed_vec.f[0] = ((player_prop->pos.f[0] - bondprevpos->f[0]) / g_GlobalTimerDelta) + throw_speed_vec.f[0];
        throw_speed_vec.f[1] = ((player_prop->pos.f[1] - bondprevpos->f[1]) / g_GlobalTimerDelta) + throw_speed_vec.f[1];
        throw_speed_vec.f[2] = ((player_prop->pos.f[2] - bondprevpos->f[2]) / g_GlobalTimerDelta) + throw_speed_vec.f[2];
    }

    spE0.f[0] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][0];
    spE0.f[1] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][1];
    spE0.f[2] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][2];

    matrix_4x4_set_identity(&spA0_a);
    matrix_4x4_copy(&g_CurrentPlayer->hands[hand].throw_item_pos_related, &sp40_f);
    sp40_f.m[3][0] = 0.0f;
    sp40_f.m[3][1] = 0.0f;
    sp40_f.m[3][2] = 0.0f;
    matrix_4x4_multiply_in_place(&sp40_f, &spA0_a);

#ifdef GEVR
    gevrNetProjectile(GEVR_NETPROJ_GRENADE, hand, &spE0, &throw_speed_vec, &spA0_a, NULL);
#endif

    wor = create_new_item_instance_of_model(PROP_CHRGRENADE, current_weapon);

    if (wor != NULL)
    {
        wor->timer = THROWN_ITEM_TIMER_DEFAULT - g_CurrentPlayer->last_z_trigger_timer;

        if (wor->timer < 0)
        {
            wor->timer = 0;
        }

        wor->runtime_bitflags &= ~(RUNTIMEBITFLAG_OWNER_ALL);
        wor->runtime_bitflags |= RUNTIME_OWNER_BITS(get_cur_playernum());

        gunInitProjectileFromPlayer(wor, &spE0, &spA0_a, &throw_speed_vec, &spFC);

        if ((wor->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) != 0)
        {
            wor->projectile->flags = (s32) (wor->projectile->flags | 2);

            wor->projectile->unk8C = 0.3f;
            wor->projectile->unk94 = 0.13333333f;
            wor->projectile->refreshrate = THROWN_ITEM_REFRESH_RATE;

            sfx_state = sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GRENADE_THROW_SFX, NULL);

            if (sfx_state != NULL)
            {
                chrobjSndCreatePostEventDefault(sfx_state, (struct coord3d *) &wor->runtime_pos);
            }
        }
    }
}


/**
 * Address 0x7F05F09C (NTSC)
 * Address 0x7F05F554 (PAL)
*/
void generate_player_thrown_knife(s32 hand)
{
    struct WeaponObjRecord *wor;
    Mtxf spFC;
    struct coord3d throw_speed_vec;
    f32 base_velocity;
    struct coord3d spE0;
    Mtxf spA0_a;
    s32 padding;
    s32 new_prop_type;
    s32 sp94;
    struct coord3d base_speed_vec;
    Mtxf sp40_f;
    struct PropRecord* player_prop;
    struct coord3d *bondprevpos;

    wor = NULL;
    base_velocity = 25.0f;

    player_prop = getCurrentPlayerProp();
    bondprevpos = getCurrentPlayerPrevPos();

    sub_GAME_7F057C14(&throw_speed_vec, &spFC);
    bullet_path_from_screen_center(&sp94, &base_speed_vec, hand);
    mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), (f32*)&base_speed_vec);

#ifdef GEVR
    if (g_gevrMotionThrowActive[hand])
    {
        throw_speed_vec.f[0] = g_gevrMotionThrowVel[hand].x;
        throw_speed_vec.f[1] = g_gevrMotionThrowVel[hand].y;
        throw_speed_vec.f[2] = g_gevrMotionThrowVel[hand].z;
        g_gevrMotionThrowActive[hand] = 0;
    }
    else
#endif
    {
        throw_speed_vec.f[0] = (base_speed_vec.f[0] * base_velocity);
        throw_speed_vec.f[1] = (base_speed_vec.f[1] * base_velocity) + 5.0f;
        throw_speed_vec.f[2] = (base_speed_vec.f[2] * base_velocity);
    }

    if (g_ClockTimer > 0)
    {
        throw_speed_vec.f[0] = ((player_prop->pos.f[0] - bondprevpos->f[0]) / g_GlobalTimerDelta) + throw_speed_vec.f[0];
        throw_speed_vec.f[1] = ((player_prop->pos.f[1] - bondprevpos->f[1]) / g_GlobalTimerDelta) + throw_speed_vec.f[1];
        throw_speed_vec.f[2] = ((player_prop->pos.f[2] - bondprevpos->f[2]) / g_GlobalTimerDelta) + throw_speed_vec.f[2];
    }

    spE0.f[0] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][0];
    spE0.f[1] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][1];
    spE0.f[2] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][2];

    matrix_4x4_set_rotation_around_z(4.712389f, &spA0_a);
    matrix_4x4_set_rotation_around_x(M_PI_F, &sp40_f);
    matrix_4x4_multiply_in_place(&sp40_f, &spA0_a);
    matrix_4x4_copy(&g_CurrentPlayer->hands[hand].throw_item_pos_related, &sp40_f);

    sp40_f.m[3][0] = 0.0f;
    sp40_f.m[3][1] = 0.0f;
    sp40_f.m[3][2] = 0.0f;
    matrix_4x4_multiply_in_place(&sp40_f, &spA0_a);

#ifdef GEVR
    gevrNetProjectile(GEVR_NETPROJ_KNIFE, hand, &spE0, &throw_speed_vec, &spA0_a, NULL);
#endif

    guRotateF(&spFC, 360.0f / ((randomGetNext() * (0.5f / (f32)INT_MAX)) + 12.1f), spA0_a.m[1][0], spA0_a.m[1][1], spA0_a.m[1][2]);

    wor = create_new_item_instance_of_model(PROP_CHRKNIFE, ITEM_THROWKNIFE);

    if (wor != NULL)
    {
        wor->runtime_bitflags &= ~(RUNTIMEBITFLAG_OWNER_ALL);
        wor->runtime_bitflags |= RUNTIME_OWNER_BITS(get_cur_playernum());

        gunInitProjectileFromPlayer(wor, &spE0, &spA0_a, &throw_speed_vec, &spFC);

        if ((wor->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) != 0)
        {
            wor->projectile->flags = (s32) (wor->projectile->flags | 2);

            wor->projectile->unk8C = 0.1f;
            wor->projectile->refreshrate = THROWN_ITEM_REFRESH_RATE;

            wor->runtime_bitflags |= RUNTIMEBITFLAG_THROWING_KNIFE_RELATED;
        }

        objUpdateThrowKnifeSound(wor);
    }
}





/**
 * Address 0x7F05F358 (NTSC)
 * Address 0x7F05F810 (PAL)
*/
void generate_player_thrown_object(s32 hand)
{
/*
    else {
        assertPrint_8291E690(".\\ported\\gun.cpp",0x8df,"throwmineremote - Not a mine!");
    }
*/

    s32 padding;
    Mtxf unk_mtxf;
    struct coord3d throw_speed_vec;
    f32 base_velocity;
    struct coord3d spE0;
    Mtxf spA0_a;
    struct WeaponObjRecord *wor;
    s32 new_prop_type;
    s32 sp94; // sp148
    struct coord3d base_speed_vec; // sp136
    struct PropRecord* player_prop; // sp132
    struct coord3d *bondprevpos;  // sp128
    Mtxf sp40_f;
    ALSoundState *sfx_state;
    s32 current_weapon;
    s32 unused;

    wor = NULL;
    base_velocity = 16.666666f;

    player_prop = getCurrentPlayerProp();
    bondprevpos = getCurrentPlayerPrevPos();
    current_weapon = getCurrentPlayerWeaponId(hand);

    if (current_weapon == ITEM_GOLDENEYEKEY)
    {
        base_velocity = 6.6666665f;
    }

    sub_GAME_7F057C14(&throw_speed_vec, &unk_mtxf);
    bullet_path_from_screen_center(&sp94, &base_speed_vec, hand);
    mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), (f32*)&base_speed_vec);

#ifdef GEVR
    if (g_gevrMotionThrowActive[hand])
    {
        throw_speed_vec.f[0] = g_gevrMotionThrowVel[hand].x;
        throw_speed_vec.f[1] = g_gevrMotionThrowVel[hand].y;
        throw_speed_vec.f[2] = g_gevrMotionThrowVel[hand].z;
        g_gevrMotionThrowActive[hand] = 0;
    }
    else
#endif
    {
        throw_speed_vec.f[0] = (base_speed_vec.f[0] * base_velocity);
        throw_speed_vec.f[1] = (base_speed_vec.f[1] * base_velocity) + 5.0f;
        throw_speed_vec.f[2] = (base_speed_vec.f[2] * base_velocity);
    }

    if (g_ClockTimer > 0)
    {
        throw_speed_vec.f[0] = ((player_prop->pos.f[0] - bondprevpos->f[0]) / g_GlobalTimerDelta) + throw_speed_vec.f[0];
        throw_speed_vec.f[1] = ((player_prop->pos.f[1] - bondprevpos->f[1]) / g_GlobalTimerDelta) + throw_speed_vec.f[1];
        throw_speed_vec.f[2] = ((player_prop->pos.f[2] - bondprevpos->f[2]) / g_GlobalTimerDelta) + throw_speed_vec.f[2];
    }

    spE0.f[0] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][0];
    spE0.f[1] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][1];
    spE0.f[2] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][2];

    matrix_4x4_set_identity(&spA0_a);
    matrix_4x4_copy(&g_CurrentPlayer->hands[hand].throw_item_pos_related, &sp40_f);
    sp40_f.m[3][0] = 0.0f;
    sp40_f.m[3][1] = 0.0f;
    sp40_f.m[3][2] = 0.0f;
    matrix_4x4_multiply_in_place(&sp40_f, &spA0_a);

#ifdef GEVR
    gevrNetProjectile(GEVR_NETPROJ_OBJECT, hand, &spE0, &throw_speed_vec, &spA0_a, NULL);
#endif

    if (current_weapon == ITEM_GOLDENEYEKEY)
    {
        wor = bondinvRemovePropWeaponByID(current_weapon);
        bondinvRemoveItemByID(current_weapon);

        if (wor != NULL)
        {
            objDetach(wor->prop);
        }

        sub_GAME_7F05D690();
    }

    if (wor == NULL)
    {
        new_prop_type = PROP_CHRREMOTEMINE;

        switch (current_weapon)
        {
        case ITEM_PROXIMITYMINE:
            new_prop_type = PROP_CHRPROXIMITYMINE;
            break;
        case ITEM_TIMEDMINE:
            new_prop_type = PROP_CHRTIMEDMINE;
            break;
        case ITEM_BOMBCASE:
            new_prop_type = PROP_CHRBOMBCASE;
            break;
        case ITEM_BUG:
            new_prop_type = PROP_CHRBUG;
            break;
        case ITEM_MICROCAMERA:
            new_prop_type = PROP_CHRMICROCAMERA;
            break;
        case ITEM_GOLDENEYEKEY:
            new_prop_type = PROP_CHRGOLDENEYEKEY;
            break;
        case ITEM_PLASTIQUE:
            new_prop_type = PROP_CHRPLASTIQUE;
            break;
#ifdef DEBUG
        default:
            assertmsg2(current_weapon = PROP_CHRREMOTEMINE, "throwmineremote - Not a mine!");
#endif

        }

        wor = create_new_item_instance_of_model(new_prop_type, current_weapon);
    }

    if (wor != NULL)
    {
        switch (current_weapon)
        {
            case ITEM_REMOTEMINE:
#ifdef GEVR
            if (gevrSoloRules())
#else
            if (getPlayerCount() == 1)
#endif
            {
                wor->timer = THROWN_ITEM_TIMER_SOLO;
            }
            else
            {
                wor->timer = THROWN_ITEM_TIMER_MULTI;
            }
            break;

            case ITEM_PROXIMITYMINE:
#ifdef GEVR
            if (gevrSoloRules())
#else
            if (getPlayerCount() == 1)
#endif
            {
                wor->timer = THROWN_ITEM_TIMER_SOLO;
            }
            else
            {
                wor->timer = THROWN_ITEM_TIMER_MULTI;
            }
            break;

            case ITEM_TIMEDMINE:
#ifdef GEVR
            if (gevrSoloRules())
#else
            if (getPlayerCount() == 1)
#endif
            {
                wor->timer = THROWN_ITEM_TIMER_SOLO;
            }
            else
            {
                wor->timer = THROWN_ITEM_TIMER_MULTI;
            }
            break;

            case ITEM_BOMBCASE:
#ifdef GEVR
            if (gevrSoloRules())
#else
            if (getPlayerCount() == 1)
#endif
            {
                wor->timer = THROWN_ITEM_TIMER_SOLO;
            }
            else
            {
                wor->timer = THROWN_ITEM_TIMER_MULTI;
            }
            break;

            case ITEM_PLASTIQUE:
            case ITEM_BUG:
            case ITEM_MICROCAMERA:
            case ITEM_GOLDENEYEKEY:
                wor->timer = 1;
            break;

            default:
                wor->timer = THROWN_ITEM_TIMER_DEFAULT;
            break;
        }

        wor->runtime_bitflags &= ~(RUNTIMEBITFLAG_OWNER_ALL);
        wor->runtime_bitflags |= RUNTIME_OWNER_BITS(get_cur_playernum());

        gunInitProjectileFromPlayer(wor, &spE0, &spA0_a, &throw_speed_vec, &unk_mtxf);

        if ((wor->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) != 0)
        {
            wor->projectile->flags = (s32) (wor->projectile->flags | 2);

            wor->projectile->unk8C = 0.1f;
            wor->projectile->refreshrate = THROWN_ITEM_REFRESH_RATE;

            sfx_state = sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GRENADE_THROW_SFX, NULL);

            if (sfx_state != NULL)
            {
                chrobjSndCreatePostEventDefault(sfx_state, (struct coord3d *) &wor->runtime_pos);
            }
        }
    }
}


/**
 * Address: 7F05F73C
 *
 * Spawns Grenade Launcher rounds and makes them inherit the player's momentum.
 */
void gunSpawnGLGrenade(s32 handnum)
{
    WeaponObjRecord *grenadeobj;
    struct hand *hand;
    Mtxf identitymtx;
    coord3d launchvel;
    s32 pad;
    Mtxf launchmtx;
    coord3d aimpos;
    coord3d aimdir;
    PropRecord *playerprop;
    coord3d *prevplayerpos;

    hand = &g_CurrentPlayer->hands[handnum];

    playerprop = getCurrentPlayerProp();
    prevplayerpos = getCurrentPlayerPrevPos();

    matrix_4x4_set_identity(&identitymtx);
    bullet_path_from_screen_center(&aimpos, &aimdir, handnum);
    mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), &aimdir);

    launchvel.x = aimdir.x * 33.333332f;
    launchvel.y = aimdir.y * 33.333332f;
    launchvel.z = aimdir.z * 33.333332f;

    if (g_ClockTimer > 0)
    {
        launchvel.x += (playerprop->pos.x - prevplayerpos->x) / g_GlobalTimerDelta;
        launchvel.y += (playerprop->pos.y - prevplayerpos->y) / g_GlobalTimerDelta;
        launchvel.z += (playerprop->pos.z - prevplayerpos->z) / g_GlobalTimerDelta;
    }

    matrix_4x4_copy(&g_CurrentPlayer->hands[handnum].throw_item_pos_related, &launchmtx);

    launchmtx.m[3][0] = 0.0f;
    launchmtx.m[3][1] = 0.0f;
    launchmtx.m[3][2] = 0.0f;

#ifdef GEVR
    coord3d glspawn = hand->field_B58;

    gevrNetProjectile(GEVR_NETPROJ_GLGRENADE, handnum, &glspawn, &launchvel, &launchmtx, NULL);
#endif

    grenadeobj = create_new_item_instance_of_model(PROP_CHRGRENADEROUND, ITEM_GRENADEROUND);

    if (grenadeobj != NULL)
    {
        grenadeobj->timer = GLGRENADE_TIMER;
        grenadeobj->runtime_bitflags &= ~(RUNTIMEBITFLAG_OWNER_ALL);
        grenadeobj->runtime_bitflags |= RUNTIME_OWNER_BITS(get_cur_playernum());

#ifdef GEVR
        gunInitProjectileFromPlayer(grenadeobj, &glspawn, &launchmtx, &launchvel, (s32 *)&identitymtx);
#else
        gunInitProjectileFromPlayer(grenadeobj, &hand->field_B58, &launchmtx, &launchvel, (s32 *)&identitymtx);
#endif

        if (grenadeobj->runtime_bitflags & RUNTIMEBITFLAG_00000080)
        {
            grenadeobj->projectile->unk8C = g_GLGrenadeLaunchUnk8C;
            grenadeobj->projectile->unk94 = g_GLGrenadeLaunchUnk94;
            grenadeobj->projectile->refreshrate = THROWN_ITEM_REFRESH_RATE;
        }
    }
}


/**
 * Address: 0x7F05F928
 * This function is responsible for attaching a rocket to the end of the Rocket Launcher and updating its matrices.
 */
void gunUpdateAttachedRocket(s32 handIndex)
{
    struct hand *entry;
    AttachedObj *attachedRocket;
    Model *rocketModel;
    Mtxf worldMtx;
    PropRecord *prop;
    AttachmentChild *attachmentChild;

    entry = &g_CurrentPlayer->hands[handIndex];

    attachedRocket = entry->rocket;

    if (attachedRocket == NULL)
    {
        return;
    }

    attachmentChild = attachedRocket->child;

    if (attachmentChild == NULL)
    {
        return;
    }

    prop = getCurrentPlayerProp();
    rocketModel = attachedRocket->model;

    matrix_4x4_copy(&entry->throw_item_pos_related, &worldMtx);

    worldMtx.m[3][0] = 0.0f;
    worldMtx.m[3][1] = 0.0f;
    worldMtx.m[3][2] = 0.0f;

    matrix_scalar_multiply(attachedRocket->model->scale, (f32 *)&worldMtx);

    objChangeShading(attachedRocket, &entry->field_B58, &worldMtx, prop->stan);
    chrobjCollisionRelated(attachedRocket);

    rocketModel->render_pos = dynAllocate((s32)rocketModel->obj->numMatrices << 6);

    matrix_4x4_copy(&attachedRocket->transform, &worldMtx);
    matrix_4x4_set_position((Mtxf *)&attachedRocket->position, &worldMtx);

    matrix_4x4_multiply_homogeneous(camGetWorldToScreenMtxf(), &worldMtx, rocketModel->render_pos);
    modelUpdateRelationsQuick(rocketModel, rocketModel->obj->RootNode);

    attachmentChild->flags1 |= 2;
    attachmentChild->unk18 = -rocketModel->render_pos->pos.m[3][2];
}


/*
* Address: 0x7f05fa7c
*/
void currentPlayerCreateRocket(GUNHAND hand)
{
    struct hand * hand_ptr;
    struct WeaponObjRecord * rocket;

    hand_ptr = &g_CurrentPlayer->hands[hand];

    if ((hand_ptr->rocket == NULL) && (hand_ptr->weapon_ammo_in_magazine > 0))
    {
        rocket = (struct WeaponObjRecord *)create_new_item_instance_of_model(PROP_CHRROCKET, ITEM_ROCKETROUND);

        if (rocket != NULL)
        {
            hand_ptr->rocket = (ObjectRecord *)rocket;
            hand_ptr->firedrocket = 0;
            rocket->timer = 1;
        }
    }
}


/*
* Address: 0x7F05FB00
* This function frees some sort of ObjectRecord from the given hand
*/
#if defined(VERSION_EU)
void sub_GAME_7F05FB00(enum GUNHAND hand)
{
    struct hand* hand_ptr;
    ObjectRecord* hand_obj_record;

    hand_ptr = &g_CurrentPlayer->hands[hand];
    hand_obj_record = hand_ptr->rocket;

    if (hand_obj_record != NULL)
    {
        objFreePermanently(hand_obj_record, 1);
        hand_ptr->rocket = NULL;
    }
}


extern f32 D_80053DDC;

/*
* Address: 0x7F05FB64
*/
void gunFireTankShell(s32 handnum)
{
    WeaponObjRecord *obj;
    struct hand *hand;
    Mtxf identitymtx;
    coord3d velocity;
    ObjectRecord *tankobj;
    coord3d unscaledvelocity;
    Mtxf shellmtx;
    coord3d screenpos;
    coord3d aimdir;
    PropRecord *playerprop;
    coord3d *prevplayerpos;
    ITEM_IDS weaponid;
    coord3d spawnpos;
    PropRecord *tankprop;

    hand = &g_CurrentPlayer->hands[handnum];

    playerprop = getCurrentPlayerProp();
    prevplayerpos = getCurrentPlayerPrevPos();
    weaponid = getCurrentPlayerWeaponId(handnum);

    matrix_4x4_set_identity(&identitymtx);

    if (weaponid == ITEM_TANKSHELLS) 
    {
        tankprop = get_ptr_for_players_tank();

        if (1);

        if ((tankprop != NULL) && (tankprop->flags & TANK_RUN_STATE_RUNNING)) 
        {
            bondviewSet3dCoord7F07CEB0(&aimdir);
        } 
        else 
        {
            sub_GAME_7F068190(&screenpos, &aimdir);
            mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), &aimdir);
        }

        velocity.x = aimdir.x * g_TankShellSpeed;
        velocity.y = aimdir.y * g_TankShellSpeed;
        velocity.z = aimdir.z * g_TankShellSpeed;

        if (g_ClockTimer > 0) {
            velocity.x += (playerprop->pos.x - prevplayerpos->x) / g_GlobalTimerDelta;
            velocity.y += (playerprop->pos.y - prevplayerpos->y) / g_GlobalTimerDelta;
            velocity.z += (playerprop->pos.z - prevplayerpos->z) / g_GlobalTimerDelta;
        }

        if ((tankprop != NULL) && (tankprop->flags & TANK_RUN_STATE_RUNNING)) 
        {
            tankobj = tankprop->obj;
            spawnpos.x = tankobj->model->render_pos[4].pos.m[3][0];
            spawnpos.y = tankobj->model->render_pos[4].pos.m[3][1];
            spawnpos.z = tankobj->model->render_pos[4].pos.m[3][2];

            mtx4TransformVecInPlace(currentPlayerGetViewToWorldMtxf(), &spawnpos);
        } 
        else 
        {
            spawnpos.x = playerprop->pos.x;
            spawnpos.y = playerprop->pos.y;
            spawnpos.z = playerprop->pos.z;
        }

        if ((g_CurrentPlayer && g_CurrentPlayer));

        setSixExplosionAndSmokeEntries();
    } 
    else 
    {
        bullet_path_from_screen_center(&screenpos, &aimdir, handnum);
        mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), &aimdir);

        spawnpos.x = hand->field_B58.x;
        spawnpos.y = hand->field_B58.y;
        spawnpos.z = hand->field_B58.z;

        if (1);

        unscaledvelocity.x = aimdir.x * D_80053DDC;
        unscaledvelocity.y = aimdir.y * D_80053DDC;
        unscaledvelocity.z = aimdir.z * D_80053DDC;

        velocity.x = unscaledvelocity.x * g_GlobalTimerDelta;
        velocity.y = unscaledvelocity.y * g_GlobalTimerDelta;
        velocity.z = unscaledvelocity.z * g_GlobalTimerDelta;

        if (g_ClockTimer > 0) 
        {
            velocity.x += (playerprop->pos.x - prevplayerpos->x) / g_GlobalTimerDelta;
            velocity.y += (playerprop->pos.y - prevplayerpos->y) / g_GlobalTimerDelta;
            velocity.z += (playerprop->pos.z - prevplayerpos->z) / g_GlobalTimerDelta;
        }
    }

    matrix_4x4_copy(&g_CurrentPlayer->hands[handnum].throw_item_pos_related, &shellmtx);

    shellmtx.m[3][0] = 0.0f;
    shellmtx.m[3][1] = 0.0f;
    shellmtx.m[3][2] = 0.0f;

    if (hand->rocket != NULL) 
    {
        obj = (WeaponObjRecord *) hand->rocket;
        hand->firedrocket = 1;
    } 
    else 
    {
        obj = (WeaponObjRecord *) create_new_item_instance_of_model(PROP_CHRROCKET, ITEM_ROCKETROUND);
    }

    if (obj == NULL) 
    {
        return;
    }

    obj->timer = -1;
    obj->runtime_bitflags &= ~(RUNTIMEBITFLAG_OWNER_ALL);
    obj->runtime_bitflags |= RUNTIME_OWNER_BITS(get_cur_playernum());

    gunInitProjectileFromPlayer(obj, &spawnpos, &shellmtx, &velocity, (s32 *) &identitymtx);

    if (obj->runtime_bitflags & RUNTIMEBITFLAG_00000080)
    {
        obj->projectile->flags |= PROJECTILEFLAG_LAUNCHING;

        if (weaponid != ITEM_TANKSHELLS)
        {
            obj->projectile->flags |= PROJECTILEFLAG_00000020;
            obj->projectile->unkB0 = obj->runtime_pos.y;
            obj->projectile->unkB4 = obj->projectile->speed.y;
            obj->projectile->unk10.x = unscaledvelocity.x;
            obj->projectile->unk10.y = unscaledvelocity.y;
            obj->projectile->unk10.z = unscaledvelocity.z;
            obj->projectile->refreshrate = THROWN_ITEM_REFRESH_RATE;

            if (obj->projectile->sounds[0] == NULL)
            {
                sndPlaySfx(g_musicSfxBufferPtr, 1, &obj->projectile->sounds[0]);
            } 
            else if (obj->projectile->sounds[1] == NULL)
            {
                sndPlaySfx(g_musicSfxBufferPtr, 1, &obj->projectile->sounds[1]);
            }
        }
    }
}
#endif

const char g_GunHudIntegerFormat[] = "%d\n";
const char aSD[] = "%s: %d\n";
const char g_GunDeathCountFormat[] = "%s %d %s\n";
const char aSD_0[] = "%s: %d\n";

