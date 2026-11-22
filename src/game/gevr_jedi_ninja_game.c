#include "gevr_jedi_ninja_game.h"
#include "gevr_jedi_ninja.h"
#include "gevr_ragdoll.h"
#include "gevr_ally.h"
#include "pdphysics.h"

#include <math.h>
#include <string.h>
#include <bondgame.h>
#include <bondconstants.h>
#include <bondaicommands.h>
#include "chr.h"
#include "chraction.h"
#include "model.h"
#include "player.h"
#include "lv.h"
#include "bg.h"
#include "dyn.h"
#include "bondview.h"
#include "stan.h"
#include "matrixmath.h"
#include "random.h"
#include "chrai.h"

extern int VrJediNinjaMode, VrJediHand, VrSaberColor, VrSaberStyle, VrNinjaSword, VrNinjaBoomerang;
extern int VrJediForcePowers, VrJediCatchProjectiles, VrJnPerfectParry, VrJnBladeTrails, VrJnAudio;
extern int VrNinjaShield, VrNinjaRicochet, VrNinjaShadowStep, VrNinjaBulletSlice;
extern int VrSithNpc, VrSithInNormalMode, VrSithScale, VrNinjaNpc, VrNinjaInNormalMode, VrAllyJedi;
extern int VrJediBlasters, VrJediPickupGuns, VrNinjaPickupGuns;
extern s32 g_gevrStereo, g_gevrExtraPass, g_StageNum, g_ClockTimer, g_GlobalTimer;
extern f32 D_800364CC;
extern s32 g_PlayerIsInTank;
extern s32 gevrJnSelectedPower, gevrJnSelectedNinjaSlot;

extern bool netIsActive(void);
extern int netGetLocalSlot(void);
extern int getPlayerCount(void);
extern s32 get_cur_playernum(void);
extern s32 lvlGetControlsLockedFlag(void);
extern s32 gevrSpectating(void);
extern s32 gevrCoopLocalDowned(void);
extern s32 gevrJnGetGripAxes(s32 ctrl, f32 pos[3], f32 right[3], f32 up[3], f32 back[3]);
extern s32 gevrJnGetHandPoseWorld(s32 ctrl, f32 posW[3], f32 rightW[3], f32 upW[3], f32 fwdW[3]);
extern s32 gevrJnGetHandVelocityWorld(s32 ctrl, f32 velCmS[3]);
extern int bondinvAddInvItem(ITEM_IDS item);
extern s32 currentPlayerEquipWeaponWrapper(GUNHAND hand, s32 next_weapon);
extern s32 getCurrentPlayerWeaponId(GUNHAND hand);
extern float get_analog_value(int hand_index, const char *input_name);
extern int vr_haptics_ready(void);
extern s32 trigger_haptic_vibration_c(int hand_index, float amplitude, float duration);
extern s32 plot_course_for_actor(ChrRecord *chr, coord3d *pos, StandTile *tile, SPEED speed);
extern StandTile *stanFindGroundAtCyl(coord3d *pos, f32 radius, u8 *rooms, StandTile *prefer, f32 *groundy, s32 *incentre);
extern Gfx *combiner_bayer_lod_perspective(Gfx *gdl);

/* -------------------------------------------------------------------------- */
/* State                                                                      */
/* -------------------------------------------------------------------------- */

#define JN_MAX_CUTS      8
#define JN_MAX_SPARKS    12
#define JN_MAX_TRACERS   8
#define JN_MAX_LIGHTNING 3
#define JN_MAX_SPECIAL   8
#define JN_MAX_SMOKE     12
#define JN_TRAIL_SAMPLES 7
#define JN_VR_HIT_SLOTS  16

enum {
    JN_NPC_SITH = 1,
    JN_NPC_NINJA = 2,
    JN_NPC_ALLY_JEDI = 3
};

/* World-space motion ribbon samples pushed once per tick, transformed per eye */
typedef struct {
    int count;
    float hilt[JN_TRAIL_SAMPLES][3];
    float tip[JN_TRAIL_SAMPLES][3];
    float age[JN_TRAIL_SAMPLES];
} JnBladeTrail;

typedef struct {
    int used;
    float pos[3];
    float vel[3];
    float radius;
    float age;
    float maxAge;
} JnSmokeCloud;

typedef struct {
    int used;
    float pos[3];
    float vel[3];
    int cutsLeft;
    float ttl;
} JnPendingCut;

typedef struct {
    int used;
    float pos[3];
    unsigned int rgba;
    float radius;
    float age;
    float maxAge;
} JnSpark;

typedef struct {
    int used;
    float from[3];
    float to[3];
    unsigned int rgba;
    float age;
    float maxAge;
} JnTracer;

typedef struct {
    int used;
    ChrRecord *chr;
    s16 chrnum;
    int kind;
    int color;
    int doubleBladed;
    float scale;
    int scaleApplied;
    GevrJnIgnition ignite;
    float meleeCd;
    float powerCd;
    float swingTimer;
    int handValid;
    s32 handTick;
    float handPosW[3];
    float handDirW[3];
    GevrJnThrown boomerang;
} JnSpecialNpc;

static GevrJnTrigger s_rtTrig, s_ltTrig;
static GevrJnSlash s_slash, s_offSlash;
static GevrJnIgnition s_fpIgnite;
static GevrJnThrown s_thrown;
static int s_blocking;
static float s_blockHoldSec;
static int s_ninjaShieldHeld;
static int s_thrownBounces;
static float s_saberLockTimer;
static float s_saberLockPulse;
static ChrRecord *s_saberLockChr;
static s16 s_saberLockChrnum;
static float s_shadowDashTimer;
static float s_shadowDashCd;
static int s_shadowApplied;
static int s_caughtProjIdx = -1;
static float s_wallSparkCd;
static float s_swingAudio01;
static float s_time;
static float s_vrPowerCd;
static int s_vrTrigWas[2];
static float s_vrThrowGuard;
static float s_vrWhooshCd[2];

/* Cached world-space tracked hand/blade state for damage interception & swept VR melee */
static int s_vrBladeValid;
static float s_vrBladeHiltW[3], s_vrBladeDirW[3], s_vrBladeAimW[3], s_vrBladeTipW[3];
static float s_vrBladeBackW[3];
static int s_vrOffBladeValid;
static float s_vrOffHiltW[3], s_vrOffDirW[3], s_vrOffTipW[3];
static int s_vrForceValid;
static float s_vrForcePosW[3], s_vrForceAimW[3];
static int s_prevTipValid[2];
static float s_prevTipW[2][3];
static ChrRecord *s_vrHitChr[2][JN_VR_HIT_SLOTS];
static float s_vrHitCd[2][JN_VR_HIT_SLOTS];

static ChrRecord *s_chokeChr;
static s16 s_chokeChrnum;
static int s_chokeActive;
static float s_chokeHoldSec;

static int s_grabBodySlot = -1;
static int s_thrownBodySlot = -1;
static float s_thrownBodyAge;
static float s_thrownBodyStart[3];
static GevrJnThrownBody s_thrownBody;
static ChrRecord *s_thrownBodyHit[8];
static int s_thrownBodyHitCount;

static int s_limbBallSuspended;
static float s_limbBallPos[3];

static int s_lightningActive;
static ChrRecord *s_lightningChrs[JN_MAX_LIGHTNING];
static s16 s_lightningChrnums[JN_MAX_LIGHTNING];
static int s_lightningCount;
static float s_lightningTickAccum;

static ChrRecord *s_throwHitChr[12];
static float s_throwHitCd[12];
static int s_throwHitCount;

static JnPendingCut s_cuts[JN_MAX_CUTS];
static JnSpark s_sparks[JN_MAX_SPARKS];
static JnTracer s_tracers[JN_MAX_TRACERS];
static JnSmokeCloud s_smoke[JN_MAX_SMOKE];
static JnBladeTrail s_trails[3];
static JnSpecialNpc s_specials[JN_MAX_SPECIAL];
static int s_specialsSpawned;
static int s_blasterGranted;
static int s_lastStage = -1;
static float s_sithLightningShow;
static float s_sithLightningFrom[3], s_sithLightningTo[3];

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static float jnViewScale(void)
{
    return (D_800364CC > 1e-6f) ? D_800364CC : 1.0f;
}

static float jnNorm3(const float v[3], float out[3])
{
    const float l2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (l2 < 1e-10f) {
        out[0] = 0.0f; out[1] = 0.0f; out[2] = -1.0f;
        return 0.0f;
    }
    const float l = sqrtf(l2);
    const float inv = 1.0f / l;
    out[0] = v[0] * inv; out[1] = v[1] * inv; out[2] = v[2] * inv;
    return l;
}

static void jnCross3(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static float jnDot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void jnHaptic(int ctrl, float amp, float dur)
{
    if (vr_haptics_ready()) {
        trigger_haptic_vibration_c(ctrl, amp, dur);
    }
}

static void jnSfx(int eventId, float intensity)
{
    if (VrJnAudio) {
        gevrJnAudioTrigger(eventId, intensity);
    }
}

static void jnRestoreShadowSpeed(void)
{
    if (s_shadowApplied && g_CurrentPlayer) {
        g_CurrentPlayer->speedboost = 1.0f;
        s_shadowApplied = 0;
    }
}

static void jnTrailClear(void)
{
    memset(s_trails, 0, sizeof(s_trails));
}

static void jnTrailStepAge(float dt)
{
    for (int t = 0; t < 3; t++) {
        JnBladeTrail *tr = &s_trails[t];
        int keep = 0;
        for (int i = 0; i < tr->count; i++) {
            tr->age[i] += dt;
            if (tr->age[i] <= 0.14f) {
                keep = i + 1;
            } else {
                break;
            }
        }
        tr->count = keep;
    }
}

static void jnTrailPushWorld(int idx, const float hiltW[3], const float tipW[3])
{
    if (idx < 0 || idx >= 3 || !VrJnBladeTrails) return;
    JnBladeTrail *tr = &s_trails[idx];
    if (tr->count > 0) {
        const float dx = tipW[0] - tr->tip[0][0];
        const float dy = tipW[1] - tr->tip[0][1];
        const float dz = tipW[2] - tr->tip[0][2];
        if (dx * dx + dy * dy + dz * dz < 4.0f * 4.0f) return;
    }
    const int n = tr->count < JN_TRAIL_SAMPLES ? tr->count : JN_TRAIL_SAMPLES - 1;
    for (int i = n; i > 0; i--) {
        memcpy(tr->hilt[i], tr->hilt[i - 1], sizeof(tr->hilt[0]));
        memcpy(tr->tip[i], tr->tip[i - 1], sizeof(tr->tip[0]));
        tr->age[i] = tr->age[i - 1];
    }
    memcpy(tr->hilt[0], hiltW, sizeof(tr->hilt[0]));
    memcpy(tr->tip[0], tipW, sizeof(tr->tip[0]));
    tr->age[0] = 0.0f;
    if (tr->count < JN_TRAIL_SAMPLES) tr->count++;
}

static int jnProjValidAirborne(int idx)
{
    if (idx < 0 || idx >= PROJECTILES_ARR_MAX) return 0;
    const Projectile *pr = &g_Projectiles[idx];
    if ((pr->flags & PROJECTILEFLAG_FREE) || !(pr->flags & PROJECTILEFLAG_AIRBORNE)) return 0;
    if ((pr->flags & PROJECTILEFLAG_STICKY) || !pr->obj || !pr->obj->prop) return 0;
    return 1;
}

static int jnModeActive(void)
{
    return VrJediNinjaMode != 0
        && g_CurrentPlayer != NULL
        && (!netIsActive() || getPlayerCount() == 1);
}

static int jnUsingGunsOverride(void)
{
    return VrJediNinjaMode == GEVR_JN_JEDI && gevrJnSelectedPower == GEVR_JN_FORCE_GUNS;
}

static int jnInGameplay(void)
{
    if (!g_CurrentPlayer || g_CurrentPlayer->bonddead || g_PlayerIsInTank == 1) return 0;
    if (g_StageNum == LEVELID_TITLE || g_CurrentPlayer->pause_state != 0) return 0;
    if (g_CurrentPlayer->watch_animation_state != 0 || lvlGetControlsLockedFlag()) return 0;
    if (gevrSpectating() || gevrCoopLocalDowned()) return 0;
    return 1;
}

static int jnEffectiveNinjaSword(void)
{
    if (VrJediNinjaMode != GEVR_JN_NINJA) return VrNinjaSword;
    const int gold = gevrJnIsGoldSword(VrNinjaSword);
    switch (gevrJnSelectedNinjaSlot) {
        case GEVR_JN_NINJA_SLOT_SHORT:
            return gold ? GEVR_JN_SWORD_GOLD_SHORT : GEVR_JN_SWORD_SHORT;
        case GEVR_JN_NINJA_SLOT_DUAL:
            return gold ? GEVR_JN_SWORD_DUAL_GOLD_SHORT : GEVR_JN_SWORD_DUAL_SHORT;
        case GEVR_JN_NINJA_SLOT_KATANA:
        case GEVR_JN_NINJA_SLOT_SHIELD:
        case GEVR_JN_NINJA_SLOT_BOOMERANG:
        default:
            return VrNinjaSword;
    }
}

static int jnHasNinjaShield(void)
{
    return VrJediNinjaMode == GEVR_JN_NINJA && VrNinjaShield != 0
        && gevrJnSelectedNinjaSlot == GEVR_JN_NINJA_SLOT_SHIELD;
}

static int jnChrAliveValid(ChrRecord *c, s16 num)
{
    if (!c || !c->prop || !c->model || !c->prop->stan) return 0;
    if (num >= 0 && c->chrnum != num) return 0;
    if (c->chrnum < 0 || c->prop->type != PROP_TYPE_CHR) return 0;
    if (chrIsDead(c) || (c->hidden & CHRHIDDEN_REMOVE) || (c->chrflags & CHRFLAG_HIDDEN)) return 0;
    return 1;
}

static int jnIsEnemyGuard(ChrRecord *c)
{
    if (!jnChrAliveValid(c, -1)) return 0;
    if (gevrAllyIs(c)) return 0;
    if (c->chrflags & CHRFLAG_COUNT_DEATH_AS_CIVILIAN) return 0;
    if (c->actiontype == ACT_SURRENDER) return 0;
    return 1;
}

static JnSpecialNpc *jnFindSpecial(ChrRecord *c)
{
    if (!c) return NULL;
    for (int i = 0; i < JN_MAX_SPECIAL; i++) {
        if (s_specials[i].used && s_specials[i].chr == c && s_specials[i].chrnum == c->chrnum) {
            return &s_specials[i];
        }
    }
    return NULL;
}

/* Wall, door, and pathblocker/glass line-of-sight verification */
static int jnHasLos(const float from[3], StandTile *fromStan, const float tileOrigin[3], const float to[3])
{
    if (!from || !to) return 0;
    if (fromStan != NULL) {
        StandTile *stan = fromStan;
        const float *o = tileOrigin ? tileOrigin : from;
        if (stanTestLineUnobstructed(&stan, o[0], o[2], to[0], to[2],
                                     CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER,
                                     from[1], from[1], to[1], to[1]) == 0) {
            return 0;
        }
    }
    if (pdphysIsReady() && pdphysRaycastAnyRoom(from, to, NULL, NULL, NULL)) {
        return 0;
    }
    return 1;
}

static void jnAddSpark(const float pos[3], unsigned int rgba, float radius, float maxAge)
{
    int slot = 0;
    float oldest = -1.0f;
    for (int i = 0; i < JN_MAX_SPARKS; i++) {
        if (!s_sparks[i].used) { slot = i; break; }
        if (s_sparks[i].age > oldest) { oldest = s_sparks[i].age; slot = i; }
    }
    s_sparks[slot].used = 1;
    memcpy(s_sparks[slot].pos, pos, sizeof(s_sparks[slot].pos));
    s_sparks[slot].rgba = rgba;
    s_sparks[slot].radius = radius;
    s_sparks[slot].age = 0.0f;
    s_sparks[slot].maxAge = maxAge;
}

static void jnAddTracer(const float from[3], const float to[3], unsigned int rgba, float maxAge)
{
    int slot = 0;
    float oldest = -1.0f;
    for (int i = 0; i < JN_MAX_TRACERS; i++) {
        if (!s_tracers[i].used) { slot = i; break; }
        if (s_tracers[i].age > oldest) { oldest = s_tracers[i].age; slot = i; }
    }
    s_tracers[slot].used = 1;
    memcpy(s_tracers[slot].from, from, sizeof(s_tracers[slot].from));
    memcpy(s_tracers[slot].to, to, sizeof(s_tracers[slot].to));
    s_tracers[slot].rgba = rgba;
    s_tracers[slot].age = 0.0f;
    s_tracers[slot].maxAge = maxAge;
}

static void jnAddSmoke(const float pos[3], const float vel[3], float radius, float maxAge)
{
    int slot = 0;
    float oldest = -1.0f;
    for (int i = 0; i < JN_MAX_SMOKE; i++) {
        if (!s_smoke[i].used) { slot = i; break; }
        if (s_smoke[i].age > oldest) { oldest = s_smoke[i].age; slot = i; }
    }
    s_smoke[slot].used = 1;
    memcpy(s_smoke[slot].pos, pos, sizeof(s_smoke[slot].pos));
    memcpy(s_smoke[slot].vel, vel, sizeof(s_smoke[slot].vel));
    s_smoke[slot].radius = radius;
    s_smoke[slot].age = 0.0f;
    s_smoke[slot].maxAge = maxAge;
}

static void jnActivateShadowStep(const float origin[3], const float fwd[3])
{
    if (!VrNinjaShadowStep || s_shadowDashCd > 0.0f) return;
    for (int i = 0; i < 6; i++) {
        const float a = (float)i * (6.2831853f / 6.0f);
        const float sp[3] = {
            origin[0] + cosf(a) * 45.0f + fwd[0] * 25.0f,
            origin[1] - 55.0f + (float)(i & 1) * 30.0f,
            origin[2] + sinf(a) * 45.0f + fwd[2] * 25.0f
        };
        const float sv[3] = { cosf(a) * 35.0f, 32.0f + (float)(i % 3) * 10.0f, sinf(a) * 35.0f };
        jnAddSmoke(sp, sv, 68.0f + (float)(i & 1) * 20.0f, 3.4f);
    }
    s_shadowDashTimer = 0.65f;
    s_shadowDashCd = 2.2f;
    if (g_CurrentPlayer) {
        g_CurrentPlayer->speedboost = 1.65f;
        s_shadowApplied = 1;
    }
    const s32 n = get_numguards();
    for (s32 i = 0; i < n; i++) {
        ChrRecord *c = &g_ChrSlots[i];
        if (!jnIsEnemyGuard(c)) continue;
        const float dx = c->prop->pos.x - origin[0];
        const float dy = (c->prop->pos.y + 90.0f) - origin[1];
        const float dz = c->prop->pos.z - origin[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 280.0f * 280.0f || d2 < 1.0f) continue;
        c->chrflags |= CHRFLAG_WAS_DAMAGED;
        if (c->accuracyrating > 25) c->accuracyrating -= 20;
    }
    jnSfx(GEVR_JN_SFX_SHADOW_STEP, 1.15f);
    jnHaptic(VrJediHand ? 1 : 0, 0.7f, 0.08f);
}

static void jnQueueCut(const float pos[3], const float vel[3], int maxCuts)
{
    if (gevrRagdollForceCutNear(pos, 115.0f, vel, maxCuts) > 0) {
        return;
    }
    for (int i = 0; i < JN_MAX_CUTS; i++) {
        if (!s_cuts[i].used) {
            s_cuts[i].used = 1;
            memcpy(s_cuts[i].pos, pos, sizeof(s_cuts[i].pos));
            memcpy(s_cuts[i].vel, vel, sizeof(s_cuts[i].vel));
            s_cuts[i].cutsLeft = maxCuts;
            s_cuts[i].ttl = 0.25f;
            return;
        }
    }
}

static void jnGetPlayerCameraWorld(float eye[3], float right[3], float up[3], float fwd[3])
{
    Mtxf *v2w = currentPlayerGetViewToWorldMtxf();
    if (!v2w) {
        eye[0] = eye[1] = eye[2] = 0.0f;
        right[0] = 1.0f; right[1] = 0.0f; right[2] = 0.0f;
        up[0] = 0.0f; up[1] = 1.0f; up[2] = 0.0f;
        fwd[0] = 0.0f; fwd[1] = 0.0f; fwd[2] = -1.0f;
        return;
    }
    eye[0] = v2w->m[3][0]; eye[1] = v2w->m[3][1]; eye[2] = v2w->m[3][2];
    right[0] = v2w->m[0][0]; right[1] = v2w->m[0][1]; right[2] = v2w->m[0][2];
    up[0] = v2w->m[1][0]; up[1] = v2w->m[1][1]; up[2] = v2w->m[1][2];
    fwd[0] = -v2w->m[2][0]; fwd[1] = -v2w->m[2][1]; fwd[2] = -v2w->m[2][2];
    jnNorm3(right, right);
    jnNorm3(up, up);
    jnNorm3(fwd, fwd);
}

static void jnGetPlayerTileOrigin(float outOrigin[3], StandTile **outStan)
{
    PropRecord *me = getCurrentPlayerProp();
    if (me && me->stan) {
        outOrigin[0] = me->pos.x;
        outOrigin[1] = me->pos.y;
        outOrigin[2] = me->pos.z;
        *outStan = me->stan;
    } else {
        outOrigin[0] = outOrigin[1] = outOrigin[2] = 0.0f;
        *outStan = NULL;
    }
}

static void jnMoveChrProp(ChrRecord *c, const float w[3])
{
    PropRecord *prop = c->prop;
    StandTile *tile = prop->stan;
    if (tile && walkTilesBetweenPoints_NoCallback(&tile, prop->pos.x, prop->pos.z, w[0], w[2]) == TRUE && tile) {
        prop->stan = tile;
    } else {
        coord3d q;
        q.x = w[0]; q.y = w[1]; q.z = w[2];
        StandTile *below = stanFindTileBelowPos(&q, NULL, NULL);
        if (below) prop->stan = below;
    }
    prop->pos.x = w[0]; prop->pos.y = w[1]; prop->pos.z = w[2];
    c->prevpos = prop->pos;
    c->ground = c->manground = w[1];
    c->sumground = w[1] * 0.1f;
    c->fallspeed.x = c->fallspeed.y = c->fallspeed.z = 0.0f;
    if (prop->stan) {
        prop->rooms[0] = prop->stan->room;
        prop->rooms[1] = 0xFF;
    }
}

static void jnHitGuardWithBlade(ChrRecord *c, const float dir[3], float dmgAmount, int forceCutCount, int isBlade)
{
    if (!jnChrAliveValid(c, -1)) return;
    coord3d d;
    d.x = dir[0]; d.y = dir[1]; d.z = dir[2];
    JnSpecialNpc *sp = jnFindSpecial(c);
    const s32 wep = (isBlade || forceCutCount > 0) ? ITEM_KNIFE : ITEM_FIST;
    const f32 targetDmg = c->damage + dmgAmount;

    if (targetDmg < c->maxdamage) {
        /* Non-lethal hit: raise maxdamage temporarily so handles_shot_actors runs
         * flinch, blood and audio without falsely triggering lethal death or stats. */
        const f32 savedMax = c->maxdamage;
        c->maxdamage = targetDmg + 1000.0f;
        handles_shot_actors(c, HIT_CHEST, &d, wep, FALSE);
        c->maxdamage = savedMax;
        if (!chrIsDead(c)) {
            c->damage = targetDmg;
        }
    } else {
        c->damage = c->maxdamage;
        handles_shot_actors(c, HIT_CHEST, &d, wep, TRUE);
    }

    const float cpos[3] = { c->prop->pos.x, c->prop->pos.y + 115.0f, c->prop->pos.z };
    unsigned int core = 0xFFFFFFFFu, glow = 0xFFD040FFu;
    if (VrJediNinjaMode == GEVR_JN_JEDI) gevrJnSaberPalette(VrSaberColor, &core, &glow);
    jnAddSpark(cpos, sp ? 0xFFE860FFu : glow, sp ? 28.0f : 22.0f, 0.22f);
    if (forceCutCount > 0 && chrIsDead(c)) {
        const float cutVel[3] = { dir[0] * 420.0f, dir[1] * 260.0f + 160.0f, dir[2] * 420.0f };
        jnQueueCut(c->prop->pos.f, cutVel, forceCutCount);
    }
}

static void jnPerformMeleeSweep(const float eye[3], const float fwd[3], float reachCm, float dmg)
{
    float pOrigin[3];
    StandTile *pStan = NULL;
    jnGetPlayerTileOrigin(pOrigin, &pStan);

    const float reach2 = (reachCm + 55.0f) * (reachCm + 55.0f);
    const s32 n = get_numguards();
    for (s32 i = 0; i < n; i++) {
        ChrRecord *c = &g_ChrSlots[i];
        if (!jnIsEnemyGuard(c)) continue;
        const float tgt[3] = { c->prop->pos.x, c->prop->pos.y + 100.0f, c->prop->pos.z };
        const float dx = tgt[0] - eye[0];
        const float dy = tgt[1] - eye[1];
        const float dz = tgt[2] - eye[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > reach2 || d2 < 1.0f) continue;
        const float d = sqrtf(d2);
        const float to[3] = { dx / d, dy / d, dz / d };
        if (jnDot3(fwd, to) < 0.35f) continue;
        if (!jnHasLos(eye, pStan, pOrigin, tgt)) continue;
        jnHitGuardWithBlade(c, to, dmg, 2, 1);
        jnHaptic(VrJediHand ? 0 : 1, 0.8f, 0.08f);
    }
}

static int jnVrCanHitChr(int handSlot, ChrRecord *c)
{
    for (int i = 0; i < JN_VR_HIT_SLOTS; i++) {
        if (s_vrHitChr[handSlot][i] == c && s_vrHitCd[handSlot][i] > 0.0f) {
            return 0;
        }
    }
    return 1;
}

static void jnVrMarkHitChr(int handSlot, ChrRecord *c, float cd)
{
    int freeIdx = 0;
    float minCd = 1e9f;
    for (int i = 0; i < JN_VR_HIT_SLOTS; i++) {
        if (s_vrHitChr[handSlot][i] == c) {
            s_vrHitCd[handSlot][i] = cd;
            return;
        }
        if (s_vrHitCd[handSlot][i] <= 0.0f) {
            freeIdx = i;
            break;
        }
        if (s_vrHitCd[handSlot][i] < minCd) {
            minCd = s_vrHitCd[handSlot][i];
            freeIdx = i;
        }
    }
    s_vrHitChr[handSlot][freeIdx] = c;
    s_vrHitCd[handSlot][freeIdx] = cd;
}

static int jnVrSweepBladeAgainstGuards(int handSlot, const float hiltW[3], const float tipW[3],
                                       const float backTipW[3], int hasBackTip,
                                       const float prevTipW[3], int hasPrevTip,
                                       const float velDirW[3], float dmg, int hapticCtrl)
{
    float pOrigin[3];
    StandTile *pStan = NULL;
    jnGetPlayerTileOrigin(pOrigin, &pStan);

    int hits = 0;
    const s32 n = get_numguards();
    for (s32 i = 0; i < n; i++) {
        ChrRecord *c = &g_ChrSlots[i];
        if (!jnIsEnemyGuard(c) || !jnVrCanHitChr(handSlot, c)) continue;

        JnSpecialNpc *sp = jnFindSpecial(c);
        const float r = (c->chrwidth > 12.0f ? c->chrwidth : 20.0f) + 18.0f;
        const float h = 185.0f * (sp ? sp->scale : 1.0f);
        float hitPt[3];
        int hit = gevrJnSegmentHitCylinder(hiltW, tipW, c->prop->pos.f, r, h, hitPt);
        if (!hit && hasBackTip) {
            hit = gevrJnSegmentHitCylinder(hiltW, backTipW, c->prop->pos.f, r, h, hitPt);
        }
        if (!hit && hasPrevTip) {
            hit = gevrJnSegmentHitCylinder(prevTipW, tipW, c->prop->pos.f, r, h, hitPt);
        }
        if (!hit) continue;

        const float tgt[3] = { c->prop->pos.x, c->prop->pos.y + 100.0f, c->prop->pos.z };
        if (!jnHasLos(hiltW, pStan, pOrigin, tgt)) continue;

        jnVrMarkHitChr(handSlot, c, 0.24f);
        jnHitGuardWithBlade(c, velDirW, dmg, 2, 1);
        jnAddSpark(hitPt, 0xFFF480FFu, 22.0f, 0.18f);
        jnHaptic(hapticCtrl, 0.85f, 0.08f);
        hits++;
    }
    return hits;
}

/* -------------------------------------------------------------------------- */
/* Lifecycle & NPC Hand Hooks                                                 */
/* -------------------------------------------------------------------------- */

void gevrJnGameReset(void)
{
    gevrJnTriggerReset(&s_rtTrig);
    gevrJnTriggerReset(&s_ltTrig);
    memset(&s_slash, 0, sizeof(s_slash));
    memset(&s_offSlash, 0, sizeof(s_offSlash));
    memset(&s_fpIgnite, 0, sizeof(s_fpIgnite));
    memset(&s_thrown, 0, sizeof(s_thrown));
    memset(s_cuts, 0, sizeof(s_cuts));
    memset(s_sparks, 0, sizeof(s_sparks));
    memset(s_tracers, 0, sizeof(s_tracers));
    memset(s_smoke, 0, sizeof(s_smoke));
    memset(s_specials, 0, sizeof(s_specials));
    memset(s_vrHitChr, 0, sizeof(s_vrHitChr));
    memset(s_vrHitCd, 0, sizeof(s_vrHitCd));
    jnTrailClear();
    gevrJnAudioReset();
    jnRestoreShadowSpeed();
    s_blocking = 0;
    s_blockHoldSec = 0.0f;
    s_ninjaShieldHeld = 0;
    s_thrownBounces = 0;
    s_saberLockTimer = 0.0f;
    s_saberLockPulse = 0.0f;
    s_saberLockChr = NULL;
    s_saberLockChrnum = -1;
    s_shadowDashTimer = 0.0f;
    s_shadowDashCd = 0.0f;
    s_caughtProjIdx = -1;
    s_wallSparkCd = 0.0f;
    s_swingAudio01 = 0.0f;
    s_time = 0.0f;
    s_vrPowerCd = 0.0f;
    s_vrTrigWas[0] = s_vrTrigWas[1] = 0;
    s_vrThrowGuard = 0.0f;
    s_vrWhooshCd[0] = s_vrWhooshCd[1] = 0.0f;
    s_vrBladeValid = 0;
    s_vrOffBladeValid = 0;
    s_vrForceValid = 0;
    s_prevTipValid[0] = s_prevTipValid[1] = 0;
    s_chokeChr = NULL;
    s_chokeChrnum = -1;
    s_chokeActive = 0;
    s_chokeHoldSec = 0.0f;
    s_grabBodySlot = -1;
    s_thrownBodySlot = -1;
    s_thrownBodyAge = 0.0f;
    s_thrownBodyHitCount = 0;
    s_limbBallSuspended = 0;
    s_lightningActive = 0;
    s_lightningCount = 0;
    s_throwHitCount = 0;
    s_specialsSpawned = 0;
    s_blasterGranted = 0;
    s_sithLightningShow = 0.0f;
    s_lastStage = g_StageNum;
}

s32 gevrJnSuppressNormalFire(void)
{
    return jnModeActive() && !jnUsingGunsOverride() && jnInGameplay();
}

s32 gevrJnHideNormalGun(void)
{
    return jnModeActive() && !jnUsingGunsOverride() && jnInGameplay();
}

s32 gevrJnBlastersActive(void)
{
    return jnModeActive() && VrJediNinjaMode == GEVR_JN_JEDI && VrJediBlasters != 0;
}

s32 gevrJnIsMeleeNpc(ChrRecord *chr)
{
    return (jnFindSpecial(chr) != NULL) ? 1 : 0;
}

void gevrJnNoteNpcHand(ChrRecord *chr, s32 hand, const Mtxf *handMtxCam)
{
    JnSpecialNpc *sp = jnFindSpecial(chr);
    Mtxf *v2w;
    coord3d c, upC, fwdC;

    if (!sp || !handMtxCam || g_gevrExtraPass || (v2w = currentPlayerGetViewToWorldMtxf()) == NULL) {
        return;
    }
    if (hand != GUNRIGHT && sp->handValid) {
        return;
    }
    c.x = handMtxCam->m[3][0];
    c.y = handMtxCam->m[3][1];
    c.z = handMtxCam->m[3][2];
    if (!isfinite(c.x) || !isfinite(c.y) || !isfinite(c.z)
        || fabsf(c.x) > 1.0e6f || fabsf(c.y) > 1.0e6f || fabsf(c.z) > 1.0e6f) {
        return;
    }
    mtx4TransformVecInPlace(v2w, &c);
    sp->handPosW[0] = c.x;
    sp->handPosW[1] = c.y;
    sp->handPosW[2] = c.z;

    upC.x = handMtxCam->m[1][0]; upC.y = handMtxCam->m[1][1]; upC.z = handMtxCam->m[1][2];
    fwdC.x = handMtxCam->m[2][0]; fwdC.y = handMtxCam->m[2][1]; fwdC.z = handMtxCam->m[2][2];
    mtx4RotateVecInPlace(v2w, &upC);
    mtx4RotateVecInPlace(v2w, &fwdC);
    const float mix[3] = {
        upC.x * 0.72f + fwdC.x * 0.68f,
        upC.y * 0.72f + fwdC.y * 0.68f,
        upC.z * 0.72f + fwdC.z * 0.68f
    };
    jnNorm3(mix, sp->handDirW);
    sp->handValid = 1;
    sp->handTick = g_GlobalTimer;
}

s32 gevrJnCanPickupProp(PropRecord *prop)
{
    if (!jnModeActive() || prop == NULL || prop->obj == NULL) return 1;
    if (VrJediNinjaMode == GEVR_JN_JEDI && VrJediPickupGuns != 0) return 1;
    if (VrJediNinjaMode == GEVR_JN_NINJA && VrNinjaPickupGuns != 0) return 1;

    ObjectRecord *obj = prop->obj;
    switch (obj->type) {
        case PROPDEF_KEY:
        case PROPDEF_ARMOUR:
            return 1;
        case PROPDEF_MAGAZINE:
        case PROPDEF_AMMO:
            return 0;
        case PROPDEF_COLLECTABLE: {
            const s32 w = GEVR_ITEM_AS(((WeaponObjRecord *)obj)->weaponnum);
            if (w == ITEM_GOLDENGUN && g_StageNum == LEVELID_EGYPT) {
                return 1;
            }
            if ((w >= ITEM_KNIFE && w <= ITEM_LASER)
                || (w >= ITEM_GRENADELAUNCH && w <= ITEM_TANKSHELLS)
                || w == ITEM_FLAREPISTOL
                || w == ITEM_ROCKETROUND
                || w == ITEM_GRENADEROUND) {
                return 0;
            }
            return 1;
        }
        default:
            return 1;
    }
}

static void jnSpawnSpecialNpcs(void)
{
    if (s_specialsSpawned || g_StageNum == LEVELID_TITLE) return;
    const s32 n = get_numguards();
    if (n <= 0) return;

    const int wantSith = VrSithNpc && (VrJediNinjaMode != 0 || VrSithInNormalMode != 0)
        ? (VrSithNpc >= 2 ? 3 : 1) : 0;
    const int wantNinja = VrNinjaNpc && (VrJediNinjaMode != 0 || VrNinjaInNormalMode != 0)
        ? (VrNinjaNpc >= 2 ? 3 : 1) : 0;
    if (!wantSith && !wantNinja) {
        s_specialsSpawned = 1;
        return;
    }

    int candidates[128], nc = 0;
    for (s32 i = 0; i < n && nc < 128; i++) {
        ChrRecord *c = &g_ChrSlots[i];
        if (jnIsEnemyGuard(c) && (c->weapons_held[0] || c->weapons_held[1])) {
            candidates[nc++] = i;
        }
    }
    if (nc == 0) return;

    /* Deterministic pseudo-random shuffle using game seed */
    for (int i = nc - 1; i > 0; i--) {
        int j = (int)(randomGetNext() % (u32)(i + 1));
        int tmp = candidates[i]; candidates[i] = candidates[j]; candidates[j] = tmp;
    }

    int idx = 0, slot = 0;
    for (int k = 0; k < wantSith && idx < nc && slot < JN_MAX_SPECIAL; k++, idx++) {
        ChrRecord *c = &g_ChrSlots[candidates[idx]];
        JnSpecialNpc *sp = &s_specials[slot++];
        memset(sp, 0, sizeof(*sp));
        sp->used = 1;
        sp->chr = c;
        sp->chrnum = c->chrnum;
        sp->kind = JN_NPC_SITH;
        sp->color = GEVR_JN_COLOR_RED;
        sp->doubleBladed = ((c->chrnum + g_StageNum) % 3) == 0 ? 1 : 0;
        sp->scale = gevrJnSithScaleFactor(VrSithScale, (unsigned int)(c->chrnum * 7919 + g_StageNum));
        c->maxdamage *= 5.0f;
        if (c->maxdamage < 40.0f) c->maxdamage = 40.0f;
        c->speedrating = 100;
        c->accuracyrating = 90;
    }
    for (int k = 0; k < wantNinja && idx < nc && slot < JN_MAX_SPECIAL; k++, idx++) {
        ChrRecord *c = &g_ChrSlots[candidates[idx]];
        JnSpecialNpc *sp = &s_specials[slot++];
        memset(sp, 0, sizeof(*sp));
        sp->used = 1;
        sp->chr = c;
        sp->chrnum = c->chrnum;
        sp->kind = JN_NPC_NINJA;
        sp->color = 0;
        sp->scale = 1.0f;
        c->maxdamage *= 2.5f;
        if (c->maxdamage < 25.0f) c->maxdamage = 25.0f;
        c->speedrating = 95;
    }
    s_specialsSpawned = 1;
}

static void jnTickSpecialNpcs(const float playerPos[3], float dt)
{
    float pOrigin[3];
    StandTile *pStan = NULL;
    jnGetPlayerTileOrigin(pOrigin, &pStan);

    for (int i = 0; i < JN_MAX_SPECIAL; i++) {
        JnSpecialNpc *sp = &s_specials[i];
        if (!sp->used) continue;
        if (!jnChrAliveValid(sp->chr, sp->chrnum)) {
            sp->used = 0;
            continue;
        }
        ChrRecord *c = sp->chr;
        if (!sp->scaleApplied && sp->scale != 1.0f && c->model) {
            modelSetScale(c->model, c->model->scale * sp->scale);
            sp->scaleApplied = 1;
        }
        gevrJnIgnitionUpdate(&sp->ignite, 1, 0, dt);
        if (sp->meleeCd > 0.0f) sp->meleeCd -= dt;
        if (sp->powerCd > 0.0f) sp->powerCd -= dt;
        if (sp->swingTimer > 0.0f) sp->swingTimer -= dt;

        /* Enemy thrown boomerang update */
        if (sp->boomerang.active) {
            const float handPos[3] = { c->prop->pos.x, c->prop->pos.y + 115.0f * sp->scale, c->prop->pos.z };
            float prev[3];
            memcpy(prev, sp->boomerang.pos, sizeof(prev));
            int hitWall = 0;
            if (!sp->boomerang.returning && pdphysIsReady()) {
                float nxt[3] = {
                    prev[0] + sp->boomerang.vel[0] * dt,
                    prev[1] + sp->boomerang.vel[1] * dt,
                    prev[2] + sp->boomerang.vel[2] * dt
                };
                hitWall = pdphysRaycastAnyRoom(prev, nxt, NULL, NULL, NULL);
            }
            const float pdx = sp->boomerang.pos[0] - playerPos[0];
            const float pdy = sp->boomerang.pos[1] - playerPos[1];
            const float pdz = sp->boomerang.pos[2] - playerPos[2];
            if (pdx * pdx + pdy * pdy + pdz * pdz <= 65.0f * 65.0f) {
                /* Bullet/projectile travel direction: attacker -> player */
                const float vx = playerPos[0] - sp->boomerang.pos[0];
                const float vz = playerPos[2] - sp->boomerang.pos[2];
                if (!gevrJnInterceptPlayerDamage(1.2f, vx, vz)) {
                    record_damage_kills(1.2f, vx, vz, -1, 1);
                }
                sp->boomerang.returning = 1;
            }
            gevrJnThrownStep(&sp->boomerang, sp->handValid ? sp->handPosW : handPos, hitWall, dt);
        }

        if (sp->kind == JN_NPC_ALLY_JEDI) continue;
        if (s_chokeActive && s_chokeChr == c) continue; /* suspended & helpless */

        const float npcEye[3] = { c->prop->pos.x, c->prop->pos.y + 115.0f * sp->scale, c->prop->pos.z };
        const float dx = playerPos[0] - c->prop->pos.x;
        const float dy = playerPos[1] - (c->prop->pos.y + 100.0f * sp->scale);
        const float dz = playerPos[2] - c->prop->pos.z;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 2200.0f * 2200.0f) continue;

        if (c->alertness < 50 && d2 > 900.0f * 900.0f) continue;

        const float d = sqrtf(fmaxf(1.0f, d2));
        const int hasLos = jnHasLos(npcEye, c->prop->stan, c->prop->pos.f, playerPos);
        if (!hasLos) continue;

        if (d > 110.0f && g_CurrentPlayer && g_CurrentPlayer->prop && g_CurrentPlayer->prop->stan) {
            if (c->actiontype != ACT_GOPOS) {
                plot_course_for_actor(c, &g_CurrentPlayer->prop->pos, g_CurrentPlayer->prop->stan, SPEED_RUN);
            }
            /* Ground/stair-validated chase step toward player */
            if (d > 140.0f && d < 1200.0f) {
                const float spd = (sp->kind == JN_NPC_SITH ? 220.0f : 185.0f) * dt;
                coord3d probe;
                probe.x = c->prop->pos.x + (dx / d) * spd;
                probe.y = c->prop->pos.y + 40.0f;
                probe.z = c->prop->pos.z + (dz / d) * spd;
                StandTile *walkTile = c->prop->stan;
                f32 groundY = c->prop->pos.y;
                s32 incentre = 0;
                if (walkTile && stanTestLineUnobstructed(&walkTile, c->prop->pos.x, c->prop->pos.z, probe.x, probe.z,
                                                         CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER,
                                                         c->prop->pos.y + 50.0f, c->prop->pos.y + 50.0f,
                                                         c->prop->pos.y + 50.0f, c->prop->pos.y + 50.0f) != 0) {
                    StandTile *groundTile = stanFindGroundAtCyl(&probe, c->chrwidth, c->prop->rooms, walkTile, &groundY, &incentre);
                    if (groundTile && fabsf(groundY - c->prop->pos.y) <= 45.0f) {
                        const float nxt[3] = { probe.x, groundY, probe.z };
                        if (!pdphysIsReady() || !pdphysRaycastAnyRoom(npcEye, nxt, NULL, NULL, NULL)) {
                            jnMoveChrProp(c, nxt);
                        }
                    }
                }
            }
        }

        /* Melee saber / katana swing */
        const float meleeRange = 145.0f * (sp->scale > 1.0f ? sp->scale : 1.0f);
        if (d <= meleeRange && sp->meleeCd <= 0.0f) {
            sp->swingTimer = 0.28f;
            if (sp->kind == JN_NPC_SITH && VrJediNinjaMode == GEVR_JN_JEDI
                && VrJnPerfectParry && (s_blocking || s_slash.active)
                && jnModeActive() && !jnUsingGunsOverride() && s_saberLockTimer <= 0.0f) {
                s_saberLockTimer = 0.85f;
                s_saberLockPulse = 0.0f;
                s_saberLockChr = c;
                s_saberLockChrnum = c->chrnum;
                sp->meleeCd = 1.15f;
                const float clashPt[3] = {
                    (playerPos[0] + c->prop->pos.x) * 0.5f,
                    c->prop->pos.y + 115.0f * sp->scale,
                    (playerPos[2] + c->prop->pos.z) * 0.5f
                };
                jnAddSpark(clashPt, 0xFFF078FFu, 30.0f, 0.25f);
                jnSfx(GEVR_JN_SFX_CLASH_SABER, 1.3f);
                jnHaptic(VrJediHand ? 0 : 1, 1.0f, 0.10f);
            } else {
                sp->meleeCd = 0.85f;
                const float vx = playerPos[0] - c->prop->pos.x;
                const float vz = playerPos[2] - c->prop->pos.z;
                if (!gevrJnInterceptPlayerDamage(1.5f, vx, vz)) {
                    record_damage_kills(1.5f, vx, vz, -1, 1);
                }
            }
        }

        /* Special attack: Sith Force Lightning / Evil Ninja Boomerang */
        if (sp->powerCd <= 0.0f && d > 180.0f && d < 950.0f) {
            if (sp->kind == JN_NPC_SITH) {
                sp->powerCd = 4.5f;
                const float vx = playerPos[0] - c->prop->pos.x;
                const float vz = playerPos[2] - c->prop->pos.z;
                s_sithLightningShow = 0.45f;
                if (sp->handValid) {
                    memcpy(s_sithLightningFrom, sp->handPosW, sizeof(s_sithLightningFrom));
                } else {
                    s_sithLightningFrom[0] = c->prop->pos.x;
                    s_sithLightningFrom[1] = c->prop->pos.y + 115.0f * sp->scale;
                    s_sithLightningFrom[2] = c->prop->pos.z;
                }
                memcpy(s_sithLightningTo, playerPos, sizeof(s_sithLightningTo));
                if (!gevrJnInterceptPlayerDamage(0.9f, vx, vz)) {
                    record_damage_kills(0.9f, vx, vz, -1, 1);
                }
            } else if (sp->kind == JN_NPC_NINJA && !sp->boomerang.active) {
                sp->powerCd = 4.2f;
                const float fallback[3] = { c->prop->pos.x, c->prop->pos.y + 115.0f, c->prop->pos.z };
                const float *origin = sp->handValid ? sp->handPosW : fallback;
                const float aim[3] = { dx / d, dy / d, dz / d };
                gevrJnThrownLaunch(&sp->boomerang, 1, origin, aim);
            }
        }
    }
    (void)pOrigin;
    (void)pStan;
}

s32 gevrJnAllyThink(ChrRecord *ally)
{
    if (!VrAllyJedi || !jnChrAliveValid(ally, -1)) return 0;

    JnSpecialNpc *sp = jnFindSpecial(ally);
    if (!sp) {
        for (int i = 0; i < JN_MAX_SPECIAL; i++) {
            if (!s_specials[i].used) {
                sp = &s_specials[i];
                memset(sp, 0, sizeof(*sp));
                sp->used = 1;
                sp->chr = ally;
                sp->chrnum = ally->chrnum;
                sp->kind = JN_NPC_ALLY_JEDI;
                sp->color = (VrSaberColor == GEVR_JN_COLOR_BLUE) ? GEVR_JN_COLOR_GREEN : GEVR_JN_COLOR_BLUE;
                sp->scale = 1.0f;
                break;
            }
        }
    }
    if (sp && sp->meleeCd <= 0.0f) {
        const s32 n = get_numguards();
        ChrRecord *best = NULL;
        float bestD2 = 175.0f * 175.0f;
        const float allyPt[3] = { ally->prop->pos.x, ally->prop->pos.y + 100.0f, ally->prop->pos.z };
        for (s32 i = 0; i < n; i++) {
            ChrRecord *e = &g_ChrSlots[i];
            if (!jnIsEnemyGuard(e)) continue;
            const float tgt[3] = { e->prop->pos.x, e->prop->pos.y + 100.0f, e->prop->pos.z };
            const float dx = tgt[0] - allyPt[0];
            const float dy = e->prop->pos.y - ally->prop->pos.y;
            const float dz = tgt[2] - allyPt[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < bestD2 && jnHasLos(allyPt, ally->prop->stan, ally->prop->pos.f, tgt)) {
                bestD2 = d2;
                best = e;
            }
        }
        if (best) {
            const float dir[3] = {
                best->prop->pos.x - ally->prop->pos.x,
                0.0f,
                best->prop->pos.z - ally->prop->pos.z
            };
            float nd[3];
            jnNorm3(dir, nd);
            jnHitGuardWithBlade(best, nd, 10.0f, 1, 1);
            sp->swingTimer = 0.28f;
            sp->meleeCd = 0.7f;
        }
    }
    return 0; /* let gevrAllyThink run normal follow/target pathing too */
}

static ChrRecord *jnFindForceTarget(const float origin[3], const float dir[3], float maxRangeCm, float minDot)
{
    float pOrigin[3];
    StandTile *pStan = NULL;
    jnGetPlayerTileOrigin(pOrigin, &pStan);

    ChrRecord *best = NULL;
    float bestScore = 1e30f;
    const s32 n = get_numguards();
    for (s32 i = 0; i < n; i++) {
        ChrRecord *c = &g_ChrSlots[i];
        if (!jnIsEnemyGuard(c)) continue;
        const float tgt[3] = { c->prop->pos.x, c->prop->pos.y + 100.0f, c->prop->pos.z };
        const float dx = tgt[0] - origin[0];
        const float dy = tgt[1] - origin[1];
        const float dz = tgt[2] - origin[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < 100.0f || d2 > maxRangeCm * maxRangeCm) continue;
        const float d = sqrtf(d2);
        const float dot = (dx * dir[0] + dy * dir[1] + dz * dir[2]) / d;
        if (dot < minDot) continue;
        if (!jnHasLos(origin, pStan, pOrigin, tgt)) continue;
        const float score = d * (1.4f - dot);
        if (score < bestScore) { bestScore = score; best = c; }
    }
    return best;
}

static void jnTriggerForcePush(const float origin[3], const float dir[3])
{
    float pOrigin[3];
    StandTile *pStan = NULL;
    jnGetPlayerTileOrigin(pOrigin, &pStan);

    const s32 n = get_numguards();
    for (s32 i = 0; i < n; i++) {
        ChrRecord *c = &g_ChrSlots[i];
        if (!jnIsEnemyGuard(c)) continue;
        const float tgt[3] = { c->prop->pos.x, c->prop->pos.y + 90.0f, c->prop->pos.z };
        const float dx = tgt[0] - origin[0];
        const float dy = tgt[1] - origin[1];
        const float dz = tgt[2] - origin[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 650.0f * 650.0f || d2 < 1.0f) continue;
        const float d = sqrtf(d2);
        const float to[3] = { dx / d, dy / d, dz / d };
        if (jnDot3(dir, to) < 0.55f) continue;
        if (!jnHasLos(origin, pStan, pOrigin, tgt)) continue;
        jnHitGuardWithBlade(c, to, 1.2f, 0, 0);
        const float spark[3] = { c->prop->pos.x, c->prop->pos.y + 105.0f, c->prop->pos.z };
        jnAddSpark(spark, 0xA0D8FFE0u, 24.0f, 0.25f);
    }
    for (int slot = 0; slot < 24; slot++) {
        float pos[3];
        if (!gevrRagdollGetBodyPos(slot, pos)) continue;
        const float dx = pos[0] - origin[0], dy = pos[1] - origin[1], dz = pos[2] - origin[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 750.0f * 750.0f || d2 < 1.0f) continue;
        const float d = sqrtf(d2);
        const float to[3] = { dx / d, dy / d, dz / d };
        if (jnDot3(dir, to) < 0.50f) continue;
        if (!jnHasLos(origin, pStan, pOrigin, pos)) continue;
        const float vel[3] = { dir[0] * 1350.0f, dir[1] * 1350.0f + 260.0f, dir[2] * 1350.0f };
        gevrRagdollThrowBody(slot, vel);
    }
    if (VrJediForcePowers && VrJediCatchProjectiles && g_CurrentPlayer && g_CurrentPlayer->prop) {
        for (int i = 0; i < PROJECTILES_ARR_MAX; i++) {
            if (!jnProjValidAirborne(i)) continue;
            Projectile *pr = &g_Projectiles[i];
            const float dx = pr->obj->prop->pos.x - origin[0];
            const float dy = pr->obj->prop->pos.y - origin[1];
            const float dz = pr->obj->prop->pos.z - origin[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (i != s_caughtProjIdx) {
                if (d2 > 750.0f * 750.0f || d2 < 1.0f) continue;
                const float d = sqrtf(d2);
                const float to[3] = { dx / d, dy / d, dz / d };
                if (jnDot3(dir, to) < 0.55f) continue;
                if (!jnHasLos(origin, pStan, pOrigin, pr->obj->prop->pos.f)) continue;
            }
            float spd = sqrtf(pr->speed.x * pr->speed.x + pr->speed.y * pr->speed.y + pr->speed.z * pr->speed.z);
            if (spd < 18.0f) spd = 22.0f;
            pr->speed.x = dir[0] * spd;
            pr->speed.y = dir[1] * spd;
            pr->speed.z = dir[2] * spd;
            pr->ownerprop = g_CurrentPlayer->prop;
            jnAddSpark(pr->obj->prop->pos.f, 0x88E0FFFFu, 26.0f, 0.25f);
        }
        s_caughtProjIdx = -1;
    }
    jnSfx(GEVR_JN_SFX_FORCE_PUSH, 1.15f);
    jnHaptic(VrJediHand ? 1 : 0, 0.7f, 0.08f);
}

/* Compute flat-screen animated world blade segment so trails advance once per tick cleanly */
static void jnFlatBladeWorld(const float eye[3], const float right[3], const float up[3], const float fwd[3],
                             float sideSign, float reachCm, float hiltW[3], float tipW[3])
{
    float lx, ly, lz, dx, dy, dz;
    if (s_blocking) {
        lx = -14.0f * sideSign; ly = -6.0f; lz = -34.0f;
        dx = 0.65f * sideSign; dy = 0.72f; dz = -0.22f;
    } else if (s_slash.active) {
        const float u = s_slash.duration > 0.0f ? s_slash.t / s_slash.duration : 0.0f;
        const float swing = (s_slash.dir == 0 ? 1.0f - 2.0f * u : -1.0f + 2.0f * u) * sideSign;
        lx = 14.0f * swing; ly = -10.0f - 4.0f * sinf(u * 3.14159f); lz = -36.0f;
        dx = -0.55f * swing; dy = 0.35f - 0.5f * u; dz = -0.75f;
    } else {
        lx = 17.0f * sideSign; ly = -14.0f; lz = -32.0f;
        dx = -0.22f * sideSign; dy = 0.72f; dz = -0.66f;
    }
    float localDir[3] = { dx, dy, dz };
    jnNorm3(localDir, localDir);
    float worldDir[3];
    for (int k = 0; k < 3; k++) {
        hiltW[k] = eye[k] + right[k] * lx + up[k] * ly - fwd[k] * lz;
        worldDir[k] = right[k] * localDir[0] + up[k] * localDir[1] - fwd[k] * localDir[2];
        tipW[k] = hiltW[k] + worldDir[k] * reachCm;
    }
}

/* -------------------------------------------------------------------------- */
/* Per-Tick Simulation                                                        */
/* -------------------------------------------------------------------------- */

void gevrJnGameTick(s32 ticks60)
{
    if (g_StageNum != s_lastStage) {
        gevrJnGameReset();
    }
    if (ticks60 <= 0) return;
    float dt = (float)ticks60 / 60.0f;
    if (dt > 0.1f) dt = 0.1f;
    s_time += dt;
    jnTrailStepAge(dt);
    if (s_wallSparkCd > 0.0f) s_wallSparkCd -= dt;
    if (s_shadowDashCd > 0.0f) s_shadowDashCd -= dt;
    if (s_vrWhooshCd[0] > 0.0f) s_vrWhooshCd[0] -= dt;
    if (s_vrWhooshCd[1] > 0.0f) s_vrWhooshCd[1] -= dt;
    for (int h = 0; h < 2; h++) {
        for (int i = 0; i < JN_VR_HIT_SLOTS; i++) {
            if (s_vrHitCd[h][i] > 0.0f) s_vrHitCd[h][i] -= dt;
        }
    }
    if (s_shadowDashTimer > 0.0f) {
        s_shadowDashTimer -= dt;
        if (s_shadowDashTimer <= 0.0f) {
            s_shadowDashTimer = 0.0f;
            jnRestoreShadowSpeed();
        }
    }

    /* Pending cuts after ragdoll capture */
    for (int i = 0; i < JN_MAX_CUTS; i++) {
        if (!s_cuts[i].used) continue;
        if (gevrRagdollForceCutNear(s_cuts[i].pos, 120.0f, s_cuts[i].vel, s_cuts[i].cutsLeft) > 0) {
            s_cuts[i].used = 0;
        } else {
            s_cuts[i].ttl -= dt;
            if (s_cuts[i].ttl <= 0.0f) s_cuts[i].used = 0;
        }
    }

    for (int i = 0; i < JN_MAX_SPARKS; i++) {
        if (s_sparks[i].used) {
            s_sparks[i].age += dt;
            if (s_sparks[i].age >= s_sparks[i].maxAge) s_sparks[i].used = 0;
        }
    }
    for (int i = 0; i < JN_MAX_TRACERS; i++) {
        if (s_tracers[i].used) {
            s_tracers[i].age += dt;
            if (s_tracers[i].age >= s_tracers[i].maxAge) s_tracers[i].used = 0;
        }
    }
    for (int i = 0; i < JN_MAX_SMOKE; i++) {
        if (s_smoke[i].used) {
            s_smoke[i].pos[0] += s_smoke[i].vel[0] * dt;
            s_smoke[i].pos[1] += s_smoke[i].vel[1] * dt;
            s_smoke[i].pos[2] += s_smoke[i].vel[2] * dt;
            const float damp = fmaxf(0.0f, 1.0f - 1.5f * dt);
            s_smoke[i].vel[0] *= damp; s_smoke[i].vel[1] *= damp; s_smoke[i].vel[2] *= damp;
            s_smoke[i].age += dt;
            if (s_smoke[i].age >= s_smoke[i].maxAge) s_smoke[i].used = 0;
        }
    }
    if (s_sithLightningShow > 0.0f) s_sithLightningShow -= dt;

    if (!jnInGameplay()) {
        s_blocking = 0;
        s_blockHoldSec = 0.0f;
        s_ninjaShieldHeld = 0;
        s_lightningActive = 0;
        s_saberLockTimer = 0.0f;
        s_caughtProjIdx = -1;
        s_vrBladeValid = s_vrOffBladeValid = s_vrForceValid = 0;
        s_prevTipValid[0] = s_prevTipValid[1] = 0;
        jnRestoreShadowSpeed();
        gevrJnAudioSetState(0, 0, 0.0f, 0.0f, 0, 0, 0, 0);
        return;
    }

    /* Spawn & update special NPCs even in normal mode if enabled */
    jnSpawnSpecialNpcs();
    float eye[3], right[3], up[3], fwd[3];
    jnGetPlayerCameraWorld(eye, right, up, fwd);
    jnTickSpecialNpcs(eye, dt);
    if (VrAllyJedi) {
        const s32 n = get_numguards();
        for (s32 i = 0; i < n; i++) {
            ChrRecord *c = &g_ChrSlots[i];
            if (jnChrAliveValid(c, -1) && gevrAllyIs(c)) {
                gevrJnAllyThink(c);
            }
        }
    }

    if (!gevrJnBlastersActive()) {
        s_blasterGranted = 0;
    } else if (!s_blasterGranted) {
        bondinvAddInvItem(ITEM_LASER);
        currentPlayerEquipWeaponWrapper(GUNRIGHT, ITEM_LASER);
        currentPlayerEquipWeaponWrapper(GUNLEFT, ITEM_UNARMED);
        s_blasterGranted = 1;
    }

    if (!jnModeActive() || jnUsingGunsOverride()) {
        s_blocking = 0;
        s_blockHoldSec = 0.0f;
        s_ninjaShieldHeld = 0;
        s_lightningActive = 0;
        s_saberLockTimer = 0.0f;
        s_caughtProjIdx = -1;
        s_vrBladeValid = s_vrOffBladeValid = s_vrForceValid = 0;
        s_prevTipValid[0] = s_prevTipValid[1] = 0;
        jnRestoreShadowSpeed();
        gevrJnAudioSetState(0, 0, 0.0f, 0.0f, 0, 0, 0, 0);
        return;
    }

    /* Determine RT / LT actions & world-space tracked VR poses */
    const int bCtrl = VrJediHand ? 0 : 1;
    const int fCtrl = 1 - bCtrl;
    const int sword = jnEffectiveNinjaSword();
    const int dbl = (VrJediNinjaMode == GEVR_JN_JEDI && VrSaberStyle == GEVR_JN_STYLE_DOUBLE);
    const int dualOffhand = gevrJnIsDualSword(VrJediNinjaMode, sword)
        || (VrJediNinjaMode == GEVR_JN_JEDI && VrSaberStyle == GEVR_JN_STYLE_DUAL);
    const float reachCm = (VrJediNinjaMode == GEVR_JN_JEDI) ? 115.0f : gevrJnWeaponReachCm(GEVR_JN_NINJA, sword);

    int bladeTap = 0, bladeBlock = 0, bladeThrow = 0;
    int offHold = 0, offRelease = 0, offDouble = 0;
    float bVelCm[3] = { 0.0f, 0.0f, 0.0f };
    float fVelCm[3] = { 0.0f, 0.0f, 0.0f };
    float vrSwing01 = 0.0f;

    float bladeHandW[3], forceHandW[3], forceDirW[3], bladeAimW[3];
    memcpy(bladeHandW, eye, sizeof(bladeHandW));
    memcpy(forceHandW, eye, sizeof(forceHandW));
    memcpy(forceDirW, fwd, sizeof(forceDirW));
    memcpy(bladeAimW, fwd, sizeof(bladeAimW));
    for (int k = 0; k < 3; k++) {
        bladeHandW[k] += right[k] * (VrJediHand ? -18.0f : 18.0f) - up[k] * 12.0f + fwd[k] * 32.0f;
        forceHandW[k] += right[k] * (VrJediHand ? 18.0f : -18.0f) - up[k] * 12.0f + fwd[k] * 32.0f;
    }

    s_vrBladeValid = 0;
    s_vrOffBladeValid = 0;
    s_vrForceValid = 0;

    if (g_gevrStereo) {
        const int bDown = get_analog_value(bCtrl, "trigger") > 0.5f;
        const int fDown = get_analog_value(fCtrl, "trigger") > 0.5f;
        if (s_vrPowerCd > 0.0f) s_vrPowerCd -= dt;
        if (s_vrThrowGuard > 0.0f) s_vrThrowGuard -= dt;

        bladeBlock = bDown;
        gevrJnGetHandVelocityWorld(bCtrl, bVelCm);
        gevrJnGetHandVelocityWorld(fCtrl, fVelCm);

        float br[3], bu[3], bf[3], fr[3], fu[3], ff[3];
        if (gevrJnGetHandPoseWorld(bCtrl, s_vrBladeHiltW, br, bu, bf)) {
            const float bd[3] = {
                bf[0] * 0.35f + bu[0] * 0.94f,
                bf[1] * 0.35f + bu[1] * 0.94f,
                bf[2] * 0.35f + bu[2] * 0.94f
            };
            jnNorm3(bd, s_vrBladeDirW);
            jnNorm3(bf, s_vrBladeAimW);
            const float activeLen = (VrJediNinjaMode == GEVR_JN_JEDI) ? (91.0f * s_fpIgnite.extend + 24.0f) : reachCm;
            for (int k = 0; k < 3; k++) {
                s_vrBladeTipW[k] = s_vrBladeHiltW[k] + s_vrBladeDirW[k] * activeLen;
                s_vrBladeBackW[k] = s_vrBladeHiltW[k] - s_vrBladeDirW[k] * (activeLen * 0.92f);
            }
            s_vrBladeValid = 1;
            memcpy(bladeHandW, s_vrBladeHiltW, sizeof(bladeHandW));
            memcpy(bladeAimW, s_vrBladeAimW, sizeof(bladeAimW));
        }

        if (gevrJnGetHandPoseWorld(fCtrl, s_vrForcePosW, fr, fu, ff)) {
            jnNorm3(ff, s_vrForceAimW);
            s_vrForceValid = 1;
            memcpy(forceHandW, s_vrForcePosW, sizeof(forceHandW));
            memcpy(forceDirW, s_vrForceAimW, sizeof(forceDirW));

            if (dualOffhand) {
                const float od[3] = {
                    ff[0] * 0.35f + fu[0] * 0.94f,
                    ff[1] * 0.35f + fu[1] * 0.94f,
                    ff[2] * 0.35f + fu[2] * 0.94f
                };
                memcpy(s_vrOffHiltW, s_vrForcePosW, sizeof(s_vrOffHiltW));
                jnNorm3(od, s_vrOffDirW);
                const float oLen = (VrJediNinjaMode == GEVR_JN_JEDI) ? (81.0f * s_fpIgnite.extend + 24.0f) : 70.0f;
                for (int k = 0; k < 3; k++) {
                    s_vrOffTipW[k] = s_vrOffHiltW[k] + s_vrOffDirW[k] * oLen;
                }
                s_vrOffBladeValid = 1;
            }
        }

        if (!bDown && s_vrTrigWas[bCtrl] && gevrJnVrWristThrow(bVelCm, bladeAimW, 1)) {
            bladeThrow = 1;
        }
        const float bSpeed = sqrtf(jnDot3(bVelCm, bVelCm));
        const float fSpeed = sqrtf(jnDot3(fVelCm, fVelCm));
        vrSwing01 = fminf(1.0f, fmaxf(bSpeed, fSpeed) / 220.0f);

        /* Real tracked VR blade collision */
        const int allowPrimaryBladeHit = s_vrBladeValid && !bDown && !s_thrown.active
            && !(VrJediNinjaMode == GEVR_JN_NINJA && gevrJnSelectedNinjaSlot == GEVR_JN_NINJA_SLOT_SHADOW)
            && !(VrJediNinjaMode == GEVR_JN_NINJA && gevrJnSelectedNinjaSlot == GEVR_JN_NINJA_SLOT_BOOMERANG);
        if (allowPrimaryBladeHit && bSpeed >= 105.0f) {
            float vDir[3];
            jnNorm3(bVelCm, vDir);
            if (s_vrWhooshCd[0] <= 0.0f && bSpeed >= 145.0f) {
                jnSfx(VrJediNinjaMode == GEVR_JN_JEDI ? GEVR_JN_SFX_SLASH_SABER : GEVR_JN_SFX_SLASH_STEEL, fminf(1.3f, bSpeed / 180.0f));
                s_vrWhooshCd[0] = 0.24f;
            }
            jnVrSweepBladeAgainstGuards(0, s_vrBladeHiltW, s_vrBladeTipW, s_vrBladeBackW, dbl,
                                        s_prevTipW[0], s_prevTipValid[0], vDir, 14.0f, bCtrl);
        } else if (VrJediNinjaMode == GEVR_JN_NINJA
                   && (gevrJnSelectedNinjaSlot == GEVR_JN_NINJA_SLOT_BOOMERANG || gevrJnSelectedNinjaSlot == GEVR_JN_NINJA_SLOT_SHADOW)
                   && !bDown && bSpeed >= 150.0f && s_vrWhooshCd[0] <= 0.0f) {
            bladeTap = 1;
            s_vrWhooshCd[0] = 0.30f;
        }
        if (s_vrBladeValid) {
            memcpy(s_prevTipW[0], s_vrBladeTipW, sizeof(s_prevTipW[0]));
            s_prevTipValid[0] = 1;
        } else {
            s_prevTipValid[0] = 0;
        }

        if (dualOffhand && s_vrOffBladeValid && !fDown && fSpeed >= 105.0f) {
            float vDir[3];
            jnNorm3(fVelCm, vDir);
            if (s_vrWhooshCd[1] <= 0.0f && fSpeed >= 145.0f) {
                jnSfx(VrJediNinjaMode == GEVR_JN_JEDI ? GEVR_JN_SFX_SLASH_SABER : GEVR_JN_SFX_SLASH_STEEL, fminf(1.2f, fSpeed / 180.0f));
                s_vrWhooshCd[1] = 0.24f;
            }
            jnVrSweepBladeAgainstGuards(1, s_vrOffHiltW, s_vrOffTipW, NULL, 0,
                                        s_prevTipW[1], s_prevTipValid[1], vDir, 12.0f, fCtrl);
        }
        if (s_vrOffBladeValid) {
            memcpy(s_prevTipW[1], s_vrOffTipW, sizeof(s_prevTipW[1]));
            s_prevTipValid[1] = 1;
        } else {
            s_prevTipValid[1] = 0;
        }

        offHold = fDown;
        if (!fDown && s_vrTrigWas[fCtrl]) {
            offRelease = 1;
            if (gevrJnVrWristThrow(fVelCm, forceDirW, 1)) offDouble = 1;
        }
        if (fDown && s_vrPowerCd <= 0.0f && gevrJnVrWristPull(fVelCm, forceDirW, 1)) {
            s_vrPowerCd = 0.25f;
        }
        s_vrTrigWas[bCtrl] = bDown;
        s_vrTrigWas[fCtrl] = fDown;
    } else {
        int rtDown = 0, ltDown = 0, isMouse = 0;
        gevrJnInputReadFlat(&rtDown, &ltDown, &isMouse);
        (void)isMouse;
        const int rtEv = gevrJnTriggerUpdate(&s_rtTrig, rtDown, dt);
        const int ltEv = gevrJnTriggerUpdate(&s_ltTrig, ltDown, dt);

        if (rtEv == GEVR_JN_TRIG_TAP) bladeTap = 1;
        if (rtEv == GEVR_JN_TRIG_DOUBLE_TAP) bladeThrow = 1;
        if (rtEv == GEVR_JN_TRIG_HOLD_START || rtEv == GEVR_JN_TRIG_HOLD_ACTIVE) bladeBlock = 1;

        if (ltEv == GEVR_JN_TRIG_HOLD_START || ltEv == GEVR_JN_TRIG_HOLD_ACTIVE) offHold = 1;
        if (ltEv == GEVR_JN_TRIG_HOLD_RELEASE) offRelease = 1;
        if (ltEv == GEVR_JN_TRIG_DOUBLE_TAP) offDouble = 1;
    }

    s_blocking = bladeBlock && !s_thrown.active;
    s_ninjaShieldHeld = jnHasNinjaShield() && offHold;
    if (s_blocking || s_ninjaShieldHeld) s_blockHoldSec += dt;
    else s_blockHoldSec = 0.0f;

    /* Ignition animation + snap-hiss SFX */
    if (VrJediNinjaMode == GEVR_JN_JEDI) {
        const float prevExt = s_fpIgnite.extend;
        gevrJnIgnitionUpdate(&s_fpIgnite, !s_thrown.active, 1, dt);
        if (prevExt <= 0.01f && s_fpIgnite.extend > 0.01f) {
            jnSfx(GEVR_JN_SFX_IGNITE, 1.0f);
        }
    }

    /* Sith Saber Lock clash resolution */
    if (s_saberLockTimer > 0.0f) {
        if (!jnChrAliveValid(s_saberLockChr, s_saberLockChrnum)) {
            s_saberLockTimer = 0.0f;
            s_saberLockChr = NULL;
        } else {
            s_saberLockTimer -= dt;
            s_saberLockPulse -= dt;
            if (s_saberLockPulse <= 0.0f) {
                s_saberLockPulse = 0.14f;
                const float clashPt[3] = {
                    (eye[0] + s_saberLockChr->prop->pos.x) * 0.5f,
                    s_saberLockChr->prop->pos.y + 115.0f,
                    (eye[2] + s_saberLockChr->prop->pos.z) * 0.5f
                };
                jnAddSpark(clashPt, 0xFFF480FFu, 26.0f, 0.18f);
                jnHaptic(bCtrl, 0.65f, 0.04f);
            }
            const float pushIn = g_gevrStereo ? jnDot3(bVelCm, bladeAimW) : 0.0f;
            if (bladeTap || pushIn > 150.0f) {
                float dir[3] = { s_saberLockChr->prop->pos.x - eye[0], 0.0f, s_saberLockChr->prop->pos.z - eye[2] };
                jnNorm3(dir, dir);
                jnHitGuardWithBlade(s_saberLockChr, dir, 18.0f, 0, 1);
                JnSpecialNpc *sp = jnFindSpecial(s_saberLockChr);
                if (sp) sp->meleeCd = 1.6f;
                jnSfx(GEVR_JN_SFX_CLASH_SABER, 1.5f);
                jnHaptic(bCtrl, 1.0f, 0.12f);
                s_saberLockTimer = 0.0f;
                s_saberLockChr = NULL;
            }
        }
    }

    /* Shadow-Step mobility activation in Ninja mode */
    if (VrJediNinjaMode == GEVR_JN_NINJA && VrNinjaShadowStep) {
        const int shadowSlot = (gevrJnSelectedNinjaSlot == GEVR_JN_NINJA_SLOT_SHADOW);
        if ((shadowSlot && (bladeTap || offDouble))
            || (offDouble && !s_chokeActive && s_grabBodySlot < 0)) {
            jnActivateShadowStep(eye, fwd);
        }
    }

    /* Flat-screen melee sweep (VR uses real tracked blade collision above) */
    if (!g_gevrStereo && bladeTap && !s_blocking && !s_thrown.active
        && !(VrJediNinjaMode == GEVR_JN_NINJA && gevrJnSelectedNinjaSlot == GEVR_JN_NINJA_SLOT_SHADOW)
        && !(VrJediNinjaMode == GEVR_JN_NINJA && gevrJnSelectedNinjaSlot == GEVR_JN_NINJA_SLOT_BOOMERANG)) {
        if (gevrJnSlashStart(&s_slash, s_blocking)) {
            const float reachMul = dbl ? 1.12f : 1.0f;
            jnPerformMeleeSweep(eye, fwd, gevrJnWeaponReachCm(VrJediNinjaMode, sword) * reachMul, 14.0f);
            jnSfx(VrJediNinjaMode == GEVR_JN_JEDI ? GEVR_JN_SFX_SLASH_SABER : GEVR_JN_SFX_SLASH_STEEL, 1.0f);
            if (VrJediNinjaMode == GEVR_JN_JEDI && VrSaberStyle == GEVR_JN_STYLE_DUAL && !s_offSlash.active) {
                gevrJnSlashStart(&s_offSlash, 0);
            }
        }
    }
    gevrJnSlashStep(&s_slash, dt);
    gevrJnSlashStep(&s_offSlash, dt);

    /* Sample world-space motion ribbons once per tick (fixes stereo per-eye trail corruption) */
    if (VrJnBladeTrails && !s_thrown.active) {
        if (VrJediNinjaMode == GEVR_JN_JEDI && s_fpIgnite.extend > 0.25f) {
            if (g_gevrStereo && s_vrBladeValid) {
                const float emitOff = dbl ? 16.0f : 24.0f;
                const float emitW[3] = {
                    s_vrBladeHiltW[0] + s_vrBladeDirW[0] * emitOff,
                    s_vrBladeHiltW[1] + s_vrBladeDirW[1] * emitOff,
                    s_vrBladeHiltW[2] + s_vrBladeDirW[2] * emitOff
                };
                jnTrailPushWorld(0, emitW, s_vrBladeTipW);
                if (dbl) {
                    const float emitB[3] = {
                        s_vrBladeHiltW[0] - s_vrBladeDirW[0] * 16.0f,
                        s_vrBladeHiltW[1] - s_vrBladeDirW[1] * 16.0f,
                        s_vrBladeHiltW[2] - s_vrBladeDirW[2] * 16.0f
                    };
                    jnTrailPushWorld(1, emitB, s_vrBladeBackW);
                }
            } else {
                const float sideSign = VrJediHand ? -1.0f : 1.0f;
                float hw[3], tw[3];
                jnFlatBladeWorld(eye, right, up, fwd, sideSign, 91.0f * s_fpIgnite.extend + 24.0f, hw, tw);
                jnTrailPushWorld(0, hw, tw);
            }
            if (VrSaberStyle == GEVR_JN_STYLE_DUAL && g_gevrStereo && s_vrOffBladeValid) {
                jnTrailPushWorld(2, s_vrOffHiltW, s_vrOffTipW);
            }
        } else if (VrJediNinjaMode == GEVR_JN_NINJA && gevrJnSelectedNinjaSlot != GEVR_JN_NINJA_SLOT_BOOMERANG) {
            if (g_gevrStereo && s_vrBladeValid) {
                jnTrailPushWorld(0, s_vrBladeHiltW, s_vrBladeTipW);
            } else {
                const float sideSign = VrJediHand ? -1.0f : 1.0f;
                float hw[3], tw[3];
                jnFlatBladeWorld(eye, right, up, fwd, sideSign, reachCm, hw, tw);
                jnTrailPushWorld(0, hw, tw);
            }
            if (gevrJnIsDualSword(GEVR_JN_NINJA, sword) && g_gevrStereo && s_vrOffBladeValid) {
                jnTrailPushWorld(2, s_vrOffHiltW, s_vrOffTipW);
            }
        }
    }

    /* Wall / floor plasma scorch sparks along actual tracked blade segment (or flat sweep) */
    if (VrJediNinjaMode == GEVR_JN_JEDI && VrJnBladeTrails && !s_thrown.active
        && s_fpIgnite.extend > 0.45f && pdphysIsReady()) {
        float hitPos[3];
        int hit = 0;
        if (g_gevrStereo && s_vrBladeValid) {
            hit = pdphysRaycastAnyRoom(s_vrBladeHiltW, s_vrBladeTipW, hitPos, NULL, NULL);
            if (!hit && dbl) {
                hit = pdphysRaycastAnyRoom(s_vrBladeHiltW, s_vrBladeBackW, hitPos, NULL, NULL);
            }
        } else {
            float wDir[3] = {
                fwd[0] * 0.65f + up[0] * 0.72f + (s_slash.active ? right[0] * 0.25f : 0.0f),
                fwd[1] * 0.65f + up[1] * 0.72f,
                fwd[2] * 0.65f + up[2] * 0.72f
            };
            jnNorm3(wDir, wDir);
            const float tipW[3] = {
                bladeHandW[0] + wDir[0] * 110.0f,
                bladeHandW[1] + wDir[1] * 110.0f,
                bladeHandW[2] + wDir[2] * 110.0f
            };
            hit = pdphysRaycastAnyRoom(bladeHandW, tipW, hitPos, NULL, NULL);
        }
        if (hit && s_wallSparkCd <= 0.0f) {
            jnAddSpark(hitPos, 0xFF9838FFu, 15.0f, 0.14f);
            jnHaptic(bCtrl, 0.25f, 0.02f);
            s_wallSparkCd = 0.055f;
        }
    }

    /* Throw Lightsaber / Ninja Boomerang */
    const int wantThrow = bladeThrow || (VrJediNinjaMode == GEVR_JN_JEDI && gevrJnSelectedPower == GEVR_JN_FORCE_THROW && offDouble)
        || (VrJediNinjaMode == GEVR_JN_NINJA && gevrJnSelectedNinjaSlot == GEVR_JN_NINJA_SLOT_BOOMERANG && bladeTap);
    if (wantThrow && !s_thrown.active && !s_blocking) {
        const int isBoom = (VrJediNinjaMode == GEVR_JN_NINJA);
        gevrJnThrownLaunch(&s_thrown, isBoom, bladeHandW, bladeAimW);
        s_thrownBounces = 0;
        s_throwHitCount = 0;
        jnTrailClear();
        jnSfx(isBoom ? GEVR_JN_SFX_SLASH_STEEL : GEVR_JN_SFX_SLASH_SABER, 1.2f);
        jnHaptic(bCtrl, 0.7f, 0.08f);
    }

    /* Update flying thrown blade / boomerang (+ corridor wall ricochets) */
    if (s_thrown.active) {
        for (int i = 0; i < s_throwHitCount; i++) {
            if (s_throwHitCd[i] > 0.0f) s_throwHitCd[i] -= dt;
        }
        float prev[3];
        memcpy(prev, s_thrown.pos, sizeof(prev));
        int hitWall = 0;
        if (!s_thrown.returning && pdphysIsReady()) {
            float nxt[3] = {
                prev[0] + s_thrown.vel[0] * dt,
                prev[1] + s_thrown.vel[1] * dt,
                prev[2] + s_thrown.vel[2] * dt
            };
            float hitPos[3], hitN[3];
            if (pdphysRaycastAnyRoom(prev, nxt, hitPos, hitN, NULL)) {
                if (s_thrown.isBoomerang && VrNinjaRicochet && s_thrownBounces < 2
                    && fabsf(jnDot3(s_thrown.dir, hitN)) < 0.72f) {
                    float refl[3];
                    gevrJnReflectVec(s_thrown.dir, hitN, refl);
                    jnNorm3(refl, s_thrown.dir);
                    s_thrown.vel[0] = s_thrown.dir[0] * s_thrown.speed;
                    s_thrown.vel[1] = s_thrown.dir[1] * s_thrown.speed;
                    s_thrown.vel[2] = s_thrown.dir[2] * s_thrown.speed;
                    s_thrown.pos[0] = hitPos[0] + hitN[0] * 8.0f;
                    s_thrown.pos[1] = hitPos[1] + hitN[1] * 8.0f;
                    s_thrown.pos[2] = hitPos[2] + hitN[2] * 8.0f;
                    s_thrownBounces++;
                    jnAddSpark(hitPos, 0xFFE870FFu, 20.0f, 0.20f);
                    jnSfx(GEVR_JN_SFX_CLASH_METAL, 0.95f);
                } else {
                    hitWall = 1;
                    jnAddSpark(hitPos, s_thrown.isBoomerang ? 0xFFE870FFu : 0xFF9838FFu, 22.0f, 0.20f);
                    jnSfx(s_thrown.isBoomerang ? GEVR_JN_SFX_CLASH_METAL : GEVR_JN_SFX_CLASH_SABER, 0.9f);
                }
            }
        }
        gevrJnThrownStep(&s_thrown, bladeHandW, hitWall, dt);

        const s32 n = get_numguards();
        for (s32 i = 0; i < n; i++) {
            ChrRecord *c = &g_ChrSlots[i];
            if (!jnIsEnemyGuard(c)) continue;
            const float dx = c->prop->pos.x - s_thrown.pos[0];
            const float dy = (c->prop->pos.y + 100.0f) - s_thrown.pos[1];
            const float dz = c->prop->pos.z - s_thrown.pos[2];
            if (dx * dx + dy * dy + dz * dz > 85.0f * 85.0f) continue;

            int cooldownIdx = -1;
            for (int h = 0; h < s_throwHitCount; h++) {
                if (s_throwHitChr[h] == c) { cooldownIdx = h; break; }
            }
            if (cooldownIdx >= 0 && s_throwHitCd[cooldownIdx] > 0.0f) continue;
            if (cooldownIdx < 0 && s_throwHitCount < 12) {
                cooldownIdx = s_throwHitCount++;
                s_throwHitChr[cooldownIdx] = c;
            }
            if (cooldownIdx >= 0) s_throwHitCd[cooldownIdx] = 0.25f;

            float dir[3];
            jnNorm3(s_thrown.vel, dir);
            jnHitGuardWithBlade(c, dir, 18.0f, 2, 1);
        }
    }

    /* Force Lightning vs Telekinesis (Force Grab/Choke, Pull, Push, Limb Ball) */
    const int allowForce = (VrJediNinjaMode == GEVR_JN_JEDI && VrJediForcePowers != 0)
        || (VrJediNinjaMode == GEVR_JN_NINJA && !jnHasNinjaShield());
    const int useLightning = (VrJediNinjaMode == GEVR_JN_JEDI && gevrJnSelectedPower == GEVR_JN_FORCE_LIGHTNING);
    const float *forceOrigin = (g_gevrStereo && s_vrForceValid) ? forceHandW : eye;
    const float *forceAim = (g_gevrStereo && s_vrForceValid) ? forceDirW : fwd;

    s_lightningActive = 0;
    s_lightningCount = 0;

    if (!allowForce) {
        s_chokeActive = 0;
        s_chokeChr = NULL;
        s_grabBodySlot = -1;
        s_caughtProjIdx = -1;
    } else if (useLightning && offHold) {
        float pOrigin[3];
        StandTile *pStan = NULL;
        jnGetPlayerTileOrigin(pOrigin, &pStan);

        s_lightningActive = 1;
        s_lightningTickAccum += dt;
        const int doDmg = (s_lightningTickAccum >= 0.12f);
        if (doDmg) s_lightningTickAccum = 0.0f;

        const s32 n = get_numguards();
        for (s32 i = 0; i < n && s_lightningCount < JN_MAX_LIGHTNING; i++) {
            ChrRecord *c = &g_ChrSlots[i];
            if (!jnIsEnemyGuard(c)) continue;
            const float tgt[3] = { c->prop->pos.x, c->prop->pos.y + 100.0f, c->prop->pos.z };
            const float dx = tgt[0] - forceOrigin[0];
            const float dy = tgt[1] - forceOrigin[1];
            const float dz = tgt[2] - forceOrigin[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > 900.0f * 900.0f || d2 < 1.0f) continue;
            const float d = sqrtf(d2);
            const float dir[3] = { dx / d, dy / d, dz / d };
            if (jnDot3(forceAim, dir) < 0.65f) continue;
            if (!jnHasLos(forceOrigin, pStan, pOrigin, tgt)) continue;
            s_lightningChrs[s_lightningCount] = c;
            s_lightningChrnums[s_lightningCount] = c->chrnum;
            s_lightningCount++;
            if (doDmg) {
                jnHitGuardWithBlade(c, dir, 0.55f, 0, 0);
            }
        }
    } else if (allowForce) {
        float pOrigin[3];
        StandTile *pStan = NULL;
        jnGetPlayerTileOrigin(pOrigin, &pStan);

        float chokeTarget[3];
        gevrJnChokeTargetPos(forceOrigin, forceAim, chokeTarget);
        float ballCenter[3] = {
            forceOrigin[0] + forceAim[0] * 105.0f,
            forceOrigin[1] + forceAim[1] * 105.0f - 8.0f,
            forceOrigin[2] + forceAim[2] * 105.0f
        };

        if (offHold) {
            if (s_chokeActive) {
                if (!jnChrAliveValid(s_chokeChr, s_chokeChrnum)) {
                    s_chokeActive = 0;
                    s_chokeChr = NULL;
                } else {
                    s_chokeHoldSec += dt;
                    float cur[3] = { s_chokeChr->prop->pos.x, s_chokeChr->prop->pos.y, s_chokeChr->prop->pos.z };
                    float footTarget[3] = { chokeTarget[0], chokeTarget[1] - 110.0f, chokeTarget[2] };
                    const float blend = fminf(1.0f, dt * 10.0f);
                    float next[3] = {
                        cur[0] + (footTarget[0] - cur[0]) * blend,
                        cur[1] + (footTarget[1] - cur[1]) * blend,
                        cur[2] + (footTarget[2] - cur[2]) * blend
                    };
                    jnMoveChrProp(s_chokeChr, next);
                    s_chokeChr->actiontype = ACT_STAND;
                    chrSetFiring(s_chokeChr, GUNRIGHT, FALSE);
                    chrSetFiring(s_chokeChr, GUNLEFT, FALSE);
                    s_chokeChr->damage += dt * 1.4f;

                    /* VR Force Slam downward gesture while choking */
                    if (g_gevrStereo && VrJediForcePowers && fVelCm[1] < -200.0f && s_chokeHoldSec > 0.15f) {
                        const float slamDir[3] = { 0.0f, -1.0f, 0.0f };
                        const float slamVel[3] = { 0.0f, -900.0f, 0.0f };
                        memcpy(s_thrownBodyStart, s_chokeChr->prop->pos.f, sizeof(s_thrownBodyStart));
                        jnHitGuardWithBlade(s_chokeChr, slamDir, 45.0f, 0, 0);
                        int bslot = gevrRagdollFindNearestBody(s_thrownBodyStart, slamDir, 260.0f, -1.0f, NULL);
                        if (bslot >= 0) gevrRagdollThrowBody(bslot, slamVel);
                        jnAddSpark(s_thrownBodyStart, 0x88C0FFFFu, 32.0f, 0.25f);
                        jnSfx(GEVR_JN_SFX_FORCE_PUSH, 1.35f);
                        jnHaptic(fCtrl, 1.0f, 0.12f);
                        s_chokeActive = 0;
                        s_chokeChr = NULL;
                    }
                }
            } else if (s_grabBodySlot >= 0) {
                float p[3];
                if (gevrRagdollGetBodyPos(s_grabBodySlot, p)) {
                    gevrRagdollPullBody(s_grabBodySlot, chokeTarget, 1600.0f, 7.5f);
                } else {
                    s_grabBodySlot = -1;
                }
            } else if (VrJediNinjaMode == GEVR_JN_JEDI && VrJediCatchProjectiles && s_caughtProjIdx >= 0) {
                if (jnProjValidAirborne(s_caughtProjIdx)) {
                    Projectile *pr = &g_Projectiles[s_caughtProjIdx];
                    pr->speed.x = (forceOrigin[0] + forceAim[0] * 45.0f - pr->obj->prop->pos.x) * 0.14f;
                    pr->speed.y = (forceOrigin[1] + forceAim[1] * 45.0f - pr->obj->prop->pos.y) * 0.14f;
                    pr->speed.z = (forceOrigin[2] + forceAim[2] * 45.0f - pr->obj->prop->pos.z) * 0.14f;
                } else {
                    s_caughtProjIdx = -1;
                }
            } else {
                int caughtProj = 0;
                if (VrJediNinjaMode == GEVR_JN_JEDI && VrJediCatchProjectiles) {
                    float bestD2 = 1100.0f * 1100.0f;
                    int bestIdx = -1;
                    for (int pi = 0; pi < PROJECTILES_ARR_MAX; pi++) {
                        if (!jnProjValidAirborne(pi)) continue;
                        Projectile *pr = &g_Projectiles[pi];
                        const float dx = pr->obj->prop->pos.x - forceOrigin[0];
                        const float dy = pr->obj->prop->pos.y - forceOrigin[1];
                        const float dz = pr->obj->prop->pos.z - forceOrigin[2];
                        const float d2 = dx * dx + dy * dy + dz * dz;
                        if (d2 >= bestD2 || d2 < 4.0f) continue;
                        const float d = sqrtf(d2);
                        if ((dx * forceAim[0] + dy * forceAim[1] + dz * forceAim[2]) / d < 0.72f) continue;
                        if (!jnHasLos(forceOrigin, pStan, pOrigin, pr->obj->prop->pos.f)) continue;
                        bestD2 = d2;
                        bestIdx = pi;
                    }
                    if (bestIdx >= 0) {
                        s_caughtProjIdx = bestIdx;
                        caughtProj = 1;
                        jnHaptic(fCtrl, 0.5f, 0.05f);
                    }
                }
                ChrRecord *closeNpc = caughtProj ? NULL : jnFindForceTarget(forceOrigin, forceAim, GEVR_JN_GRAB_RANGE_CM, 0.72f);
                if (caughtProj) {
                    /* held by off-hand */
                } else if (closeNpc) {
                    s_chokeChr = closeNpc;
                    s_chokeChrnum = closeNpc->chrnum;
                    s_chokeActive = 1;
                    s_chokeHoldSec = 0.0f;
                    jnHaptic(fCtrl, 0.5f, 0.06f);
                } else {
                    float bodyPos[3];
                    int bslot = gevrRagdollFindNearestBody(forceOrigin, forceAim, 900.0f, 0.75f, bodyPos);
                    if (bslot >= 0 && jnHasLos(forceOrigin, pStan, pOrigin, bodyPos)) {
                        s_grabBodySlot = bslot;
                        gevrRagdollPullBody(bslot, chokeTarget, 1600.0f, 7.5f);
                    } else if (VrJediNinjaMode == GEVR_JN_JEDI) {
                        int pulled = gevrRagdollPullLooseLimbs(forceOrigin, ballCenter, GEVR_JN_PULL_RANGE_CM, s_time, GEVR_JN_MAX_LIMBS);
                        if (pulled > 0) {
                            s_limbBallSuspended = 1;
                            memcpy(s_limbBallPos, ballCenter, sizeof(s_limbBallPos));
                        }
                        ChrRecord *farNpc = jnFindForceTarget(forceOrigin, forceAim, GEVR_JN_PULL_RANGE_CM, 0.84f);
                        if (farNpc) {
                            float p[3] = { farNpc->prop->pos.x, farNpc->prop->pos.y, farNpc->prop->pos.z };
                            const float toHand[3] = { forceOrigin[0] - p[0], forceOrigin[1] - 100.0f - p[1], forceOrigin[2] - p[2] };
                            float nd[3];
                            const float d = jnNorm3(toHand, nd);
                            if (d > 140.0f) {
                                const float step = fminf(d - 110.0f, 680.0f * dt);
                                float nxt[3] = { p[0] + nd[0] * step, p[1] + nd[1] * step, p[2] + nd[2] * step };
                                jnMoveChrProp(farNpc, nxt);
                            }
                        }
                    }
                }
            }
        }

        if (offRelease) {
            if (s_chokeActive && jnChrAliveValid(s_chokeChr, s_chokeChrnum)) {
                float launchDir[3] = { forceAim[0], forceAim[1], forceAim[2] };
                if (g_gevrStereo && sqrtf(jnDot3(fVelCm, fVelCm)) > 240.0f) {
                    jnNorm3(fVelCm, launchDir);
                }
                const float throwVel[3] = { launchDir[0] * 1600.0f, launchDir[1] * 1600.0f + 160.0f, launchDir[2] * 1600.0f };
                memcpy(s_thrownBodyStart, s_chokeChr->prop->pos.f, sizeof(s_thrownBodyStart));
                jnHitGuardWithBlade(s_chokeChr, launchDir, 40.0f, 0, 0);
                int bslot = gevrRagdollFindNearestBody(s_thrownBodyStart, launchDir, 260.0f, -1.0f, NULL);
                if (bslot >= 0) {
                    gevrRagdollThrowBody(bslot, throwVel);
                    s_thrownBodySlot = bslot;
                    s_thrownBodyAge = 0.0f;
                    s_thrownBodyHitCount = 0;
                    gevrJnThrownBodyInit(&s_thrownBody, 80.0f, 1600.0f);
                }
                jnSfx(GEVR_JN_SFX_FORCE_PUSH, 1.0f);
                s_chokeActive = 0;
                s_chokeChr = NULL;
                jnHaptic(fCtrl, 0.9f, 0.10f);
            } else if (s_grabBodySlot >= 0) {
                float launchDir[3] = { forceAim[0], forceAim[1], forceAim[2] };
                if (g_gevrStereo && sqrtf(jnDot3(fVelCm, fVelCm)) > 240.0f) {
                    jnNorm3(fVelCm, launchDir);
                }
                const float throwVel[3] = { launchDir[0] * 1600.0f, launchDir[1] * 1600.0f + 180.0f, launchDir[2] * 1600.0f };
                gevrRagdollGetBodyPos(s_grabBodySlot, s_thrownBodyStart);
                gevrRagdollThrowBody(s_grabBodySlot, throwVel);
                s_thrownBodySlot = s_grabBodySlot;
                s_thrownBodyAge = 0.0f;
                s_thrownBodyHitCount = 0;
                gevrJnThrownBodyInit(&s_thrownBody, 80.0f, 1600.0f);
                jnSfx(GEVR_JN_SFX_FORCE_PUSH, 0.95f);
                s_grabBodySlot = -1;
                jnHaptic(fCtrl, 0.9f, 0.10f);
            } else if (s_caughtProjIdx >= 0) {
                if (jnProjValidAirborne(s_caughtProjIdx) && g_CurrentPlayer && g_CurrentPlayer->prop) {
                    Projectile *pr = &g_Projectiles[s_caughtProjIdx];
                    pr->speed.x = forceAim[0] * 28.0f;
                    pr->speed.y = forceAim[1] * 28.0f;
                    pr->speed.z = forceAim[2] * 28.0f;
                    pr->ownerprop = g_CurrentPlayer->prop;
                    jnAddSpark(pr->obj->prop->pos.f, 0x88E0FFFFu, 22.0f, 0.22f);
                    jnSfx(GEVR_JN_SFX_FORCE_PUSH, 1.05f);
                }
                s_caughtProjIdx = -1;
            }
            s_chokeActive = 0;
            s_chokeChr = NULL;
            s_grabBodySlot = -1;
        }

        if (s_limbBallSuspended && VrJediNinjaMode == GEVR_JN_JEDI) {
            int kept = gevrRagdollPullLooseLimbs(forceOrigin, ballCenter, 360.0f, s_time, GEVR_JN_MAX_LIMBS);
            if (kept <= 0 && !offHold) {
                s_limbBallSuspended = 0;
            } else {
                memcpy(s_limbBallPos, ballCenter, sizeof(s_limbBallPos));
            }
        }

        if (offDouble && VrJediNinjaMode == GEVR_JN_JEDI) {
            if (s_limbBallSuspended) {
                gevrRagdollLaunchLooseLimbs(s_limbBallPos, forceAim, 1800.0f, 190.0f);
                const s32 n = get_numguards();
                for (s32 i = 0; i < n; i++) {
                    ChrRecord *c = &g_ChrSlots[i];
                    if (!jnIsEnemyGuard(c)) continue;
                    const float tgt[3] = { c->prop->pos.x, c->prop->pos.y + 100.0f, c->prop->pos.z };
                    const float dx = tgt[0] - forceOrigin[0];
                    const float dy = tgt[1] - forceOrigin[1];
                    const float dz = tgt[2] - forceOrigin[2];
                    const float d = sqrtf(dx * dx + dy * dy + dz * dz);
                    if (d < 1.0f || d > 950.0f) continue;
                    const float dir[3] = { dx / d, dy / d, dz / d };
                    if (jnDot3(forceAim, dir) < 0.65f) continue;
                    if (!jnHasLos(forceOrigin, pStan, pOrigin, tgt)) continue;
                    const float dmg = gevrJnLimbBlastDamage(d);
                    if (dmg >= 80.0f) {
                        jnHitGuardWithBlade(c, dir, 24.0f, 1, 0);
                    } else if (dmg > 0.0f) {
                        jnHitGuardWithBlade(c, dir, dmg * 0.05f, 0, 0);
                    }
                }
                s_limbBallSuspended = 0;
                jnHaptic(fCtrl, 1.0f, 0.12f);
            } else {
                jnTriggerForcePush(forceOrigin, forceAim);
            }
        }
    }

    /* Thrown body momentum & dismemberment impacts */
    if (s_thrownBodySlot >= 0) {
        s_thrownBodyAge += dt;
        float bpos[3];
        if (s_thrownBodyAge > 1.2f || !gevrRagdollGetBodyPos(s_thrownBodySlot, bpos)) {
            s_thrownBodySlot = -1;
        } else {
            const float tdx = bpos[0] - s_thrownBodyStart[0];
            const float tdy = bpos[1] - s_thrownBodyStart[1];
            const float tdz = bpos[2] - s_thrownBodyStart[2];
            const float travel = sqrtf(tdx * tdx + tdy * tdy + tdz * tdz);
            const s32 n = get_numguards();
            for (s32 i = 0; i < n; i++) {
                ChrRecord *c = &g_ChrSlots[i];
                if (!jnIsEnemyGuard(c)) continue;
                int seen = 0;
                for (int h = 0; h < s_thrownBodyHitCount; h++) {
                    if (s_thrownBodyHit[h] == c) { seen = 1; break; }
                }
                if (seen) continue;
                const float dx = c->prop->pos.x - bpos[0];
                const float dy = (c->prop->pos.y + 90.0f) - bpos[1];
                const float dz = c->prop->pos.z - bpos[2];
                if (dx * dx + dy * dy + dz * dz > 110.0f * 110.0f) continue;

                if (s_thrownBodyHitCount < 8) s_thrownBodyHit[s_thrownBodyHitCount++] = c;
                float knock = 0.0f;
                const int impact = gevrJnThrownBodyImpact(&s_thrownBody, travel, &knock);
                if (impact == GEVR_JN_IMPACT_DISMEMBER) {
                    jnHitGuardWithBlade(c, forceAim, 24.0f, 3, 0);
                } else if (impact == GEVR_JN_IMPACT_RAGDOLL_KILL) {
                    jnHitGuardWithBlade(c, forceAim, 12.0f, 0, 0);
                } else if (impact == GEVR_JN_IMPACT_KNOCKDOWN) {
                    jnHitGuardWithBlade(c, forceAim, 0.8f, 0, 0);
                }
                if (knock <= 120.0f) {
                    s_thrownBodySlot = -1;
                    break;
                }
            }
        }
    }

    /* Update procedural DSP audio state */
    const float slashAudio = (s_slash.active || s_offSlash.active) ? 0.85f : 0.0f;
    s_swingAudio01 = fmaxf(vrSwing01, slashAudio);
    if (VrJnAudio) {
        gevrJnAudioSetState(1, VrJediNinjaMode == GEVR_JN_JEDI, s_fpIgnite.extend, s_swingAudio01,
                            (s_chokeActive || s_grabBodySlot >= 0 || s_limbBallSuspended || s_caughtProjIdx >= 0) ? 1 : 0,
                            s_lightningActive, s_thrown.active, s_thrown.isBoomerang);
    } else {
        gevrJnAudioSetState(0, 0, 0.0f, 0.0f, 0, 0, 0, 0);
    }
}

/* -------------------------------------------------------------------------- */
/* Bullet & Melee Interception                                                */
/* -------------------------------------------------------------------------- */

s32 gevrJnInterceptPlayerDamage(f32 damageAmount, f32 vecX, f32 vecZ)
{
    (void)damageAmount;
    if (!jnModeActive() || jnUsingGunsOverride() || !jnInGameplay()) return 0;

    float eye[3], right[3], up[3], fwd[3];
    jnGetPlayerCameraWorld(eye, right, up, fwd);

    /* Ninja bullet-slice mid-swing (flat animation OR fast VR tracked blade swing) */
    const int vrSwinging = g_gevrStereo && s_vrBladeValid && s_vrWhooshCd[0] > 0.05f;
    if (VrJediNinjaMode == GEVR_JN_NINJA && VrNinjaBulletSlice
        && (s_slash.active || s_offSlash.active || vrSwinging)) {
        const float cutPt[3] = {
            (g_gevrStereo && s_vrBladeValid) ? (s_vrBladeHiltW[0] + s_vrBladeTipW[0]) * 0.5f : (eye[0] + fwd[0] * 55.0f),
            (g_gevrStereo && s_vrBladeValid) ? (s_vrBladeHiltW[1] + s_vrBladeTipW[1]) * 0.5f : (eye[1] + fwd[1] * 55.0f),
            (g_gevrStereo && s_vrBladeValid) ? (s_vrBladeHiltW[2] + s_vrBladeTipW[2]) * 0.5f : (eye[2] + fwd[2] * 55.0f)
        };
        jnAddSpark(cutPt, 0xF0F8FFFFu, 26.0f, 0.18f);
        jnSfx(GEVR_JN_SFX_CLASH_METAL, 1.15f);
        jnHaptic(VrJediHand ? 0 : 1, 0.7f, 0.06f);
        return 1;
    }

    /* GoldenEye record_damage_kills passes bullet travel vector (shooter -> player);
     * negate to obtain direction from player toward the shooter. */
    const float travel[3] = { vecX, 0.0f, vecZ };
    float toShooter[3];
    if (!gevrJnBulletTravelToShooter(travel, toShooter)) {
        toShooter[0] = fwd[0];
        toShooter[1] = fwd[1];
        toShooter[2] = fwd[2];
    }

    float reflectDir[3] = { 0.0f, 0.0f, 0.0f };
    int res;
    if (g_gevrStereo && (s_vrBladeValid || s_vrForceValid)) {
        const float *bDir = s_vrBladeValid ? s_vrBladeDirW : fwd;
        const float *handAim = s_ninjaShieldHeld
            ? (s_vrForceValid ? s_vrForceAimW : fwd)
            : (s_vrBladeValid ? s_vrBladeAimW : fwd);
        res = gevrJnCheckVrBladeBlock(VrJediNinjaMode, s_blocking || s_ninjaShieldHeld,
                                      s_ninjaShieldHeld, fwd, bDir, handAim, toShooter, reflectDir);
    } else {
        res = gevrJnCheckBlock(VrJediNinjaMode, s_blocking || s_ninjaShieldHeld,
                               s_ninjaShieldHeld, fwd, toShooter, reflectDir);
    }
    if (res == GEVR_JN_BLOCK_NONE) return 0;

    const int perfect = (VrJnPerfectParry != 0 && s_blockHoldSec <= 0.22f && res == GEVR_JN_BLOCK_DEFLECT);
    float bladePt[3];
    if (g_gevrStereo && s_ninjaShieldHeld && s_vrForceValid) {
        memcpy(bladePt, s_vrForcePosW, sizeof(bladePt));
    } else if (g_gevrStereo && s_vrBladeValid) {
        bladePt[0] = s_vrBladeHiltW[0] + s_vrBladeDirW[0] * 55.0f;
        bladePt[1] = s_vrBladeHiltW[1] + s_vrBladeDirW[1] * 55.0f;
        bladePt[2] = s_vrBladeHiltW[2] + s_vrBladeDirW[2] * 55.0f;
    } else {
        bladePt[0] = eye[0] + fwd[0] * 48.0f + right[0] * (s_ninjaShieldHeld ? -10.0f : 6.0f);
        bladePt[1] = eye[1] + fwd[1] * 48.0f - up[1] * 6.0f;
        bladePt[2] = eye[2] + fwd[2] * 48.0f + right[2] * (s_ninjaShieldHeld ? -10.0f : 6.0f);
    }

    unsigned int core = 0xFFFFFFFFu, glow = 0xFFD860FFu;
    if (VrJediNinjaMode == GEVR_JN_JEDI) {
        gevrJnSaberPalette(VrSaberColor, &core, &glow);
    }
    jnAddSpark(bladePt, perfect ? 0xFFF480FFu : (res == GEVR_JN_BLOCK_DEFLECT && VrJediNinjaMode == GEVR_JN_JEDI ? glow : 0xFFE860FFu),
               perfect ? 38.0f : (VrJediNinjaMode == GEVR_JN_JEDI ? 28.0f : 20.0f), 0.22f);
    jnSfx(res == GEVR_JN_BLOCK_DEFLECT ? GEVR_JN_SFX_DEFLECT : GEVR_JN_SFX_CLASH_METAL, perfect ? 1.45f : 1.0f);
    jnHaptic(VrJediHand ? 0 : 1, perfect ? 1.0f : 0.85f, perfect ? 0.10f : 0.07f);

    if (res == GEVR_JN_BLOCK_DEFLECT) {
        ChrRecord *shooter = jnFindForceTarget(bladePt, reflectDir, 2200.0f, 0.70f);
        if (!shooter && g_gevrStereo) {
            shooter = jnFindForceTarget(eye, fwd, 2200.0f, 0.70f);
        }
        if (shooter) {
            const float tgt[3] = {
                shooter->prop->pos.x,
                shooter->prop->pos.y + 115.0f,
                shooter->prop->pos.z
            };
            const float dir[3] = { tgt[0] - bladePt[0], tgt[1] - bladePt[1], tgt[2] - bladePt[2] };
            float nd[3];
            jnNorm3(dir, nd);
            const unsigned int boltCol = gevrJnBlastersActive()
                ? (perfect ? 0xFFF480FFu : 0x58E8FFFFu)
                : (VrJediNinjaMode == GEVR_JN_JEDI ? glow : 0xFFD840FFu);
            jnAddTracer(bladePt, tgt, boltCol, 0.18f);
            jnHitGuardWithBlade(shooter, nd, perfect ? 18.0f : 3.5f, 0, 1);
        } else {
            const float farPt[3] = {
                bladePt[0] + reflectDir[0] * 900.0f,
                bladePt[1] + reflectDir[1] * 900.0f,
                bladePt[2] + reflectDir[2] * 900.0f
            };
            const unsigned int boltCol = gevrJnBlastersActive()
                ? (perfect ? 0xFFF480FFu : 0x58E8FFFFu)
                : glow;
            jnAddTracer(bladePt, farPt, boltCol, 0.16f);
        }
    }
    return 1;
}

/* -------------------------------------------------------------------------- */
/* Fast3D Shaded Geometry Helpers                                             */
/* -------------------------------------------------------------------------- */

static void jnSetVtx(Vtx *v, f32 x, f32 y, f32 z, u32 rgba)
{
    v->v.ob[0] = (s16)x;
    v->v.ob[1] = (s16)y;
    v->v.ob[2] = (s16)z;
    v->v.flag = 0;
    v->v.tc[0] = v->v.tc[1] = 0;
    v->v.cn[0] = (rgba >> 24) & 0xff;
    v->v.cn[1] = (rgba >> 16) & 0xff;
    v->v.cn[2] = (rgba >> 8) & 0xff;
    v->v.cn[3] = rgba & 0xff;
}

static u32 jnShadeRgba(u32 rgba, float mul)
{
    if (mul < 0.40f) mul = 0.40f;
    if (mul > 1.0f) mul = 1.0f;
    const unsigned int r = (unsigned int)((float)((rgba >> 24) & 0xff) * mul);
    const unsigned int g = (unsigned int)((float)((rgba >> 16) & 0xff) * mul);
    const unsigned int b = (unsigned int)((float)((rgba >> 8) & 0xff) * mul);
    return (r << 24) | (g << 16) | (b << 8) | (rgba & 0xffu);
}

static Gfx *jnBeginGeom(Gfx *gdl, const float originView[3], float scale, int translucent)
{
    Mtxf mf;
    Mtx *mv = dynAllocateMatrix();
    if (!mv) return NULL;
    matrix_4x4_set_identity(&mf);
    mf.m[0][0] = mf.m[1][1] = mf.m[2][2] = 0.1f * scale; /* vertices in mm */
    mf.m[3][0] = originView[0];
    mf.m[3][1] = originView[1];
    mf.m[3][2] = originView[2];
    guMtxF2L(mf.m, mv);
    gSPMatrix(gdl++, osVirtualToPhysical((void *)currentPlayerGetProjectionMatrix()), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(gdl++, osVirtualToPhysical(mv), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gDPPipeSync(gdl++);
    gSPClearGeometryMode(gdl++, G_LIGHTING | G_FOG | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(gdl++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    if (translucent) {
        gDPSetRenderMode(gdl++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
    } else {
        gDPSetRenderMode(gdl++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    }
    gSPTexture(gdl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    return gdl;
}

static void jnOrthoFrame(const float dir[3], float uOut[3], float vOut[3])
{
    const float ref[3] = { fabsf(dir[1]) < 0.9f ? 0.0f : 1.0f, fabsf(dir[1]) < 0.9f ? 1.0f : 0.0f, 0.0f };
    jnCross3(dir, ref, uOut);
    jnNorm3(uOut, uOut);
    jnCross3(dir, uOut, vOut);
    jnNorm3(vOut, vOut);
}

/* 6-sided smoothly shaded cylindrical prism from origin along dir*lenMm (mm local) */
static Gfx *jnEmitPrism(Gfx *gdl, const float aMm[3], const float dir[3], float lenMm,
                        float r0Mm, float r1Mm, u32 rgbaBase, u32 rgbaTip, int emissive)
{
    Vtx *v = dynAllocateVertices(12);
    if (!v) return gdl;
    float u[3], w[3];
    jnOrthoFrame(dir, u, w);
    const float bMm[3] = {
        aMm[0] + dir[0] * lenMm,
        aMm[1] + dir[1] * lenMm,
        aMm[2] + dir[2] * lenMm
    };
    for (int i = 0; i < 6; i++) {
        const float theta = (float)i * (6.2831853f / 6.0f);
        const float ca = cosf(theta), sa = sinf(theta);
        const float nx = u[0] * ca + w[0] * sa;
        const float ny = u[1] * ca + w[1] * sa;
        const float nz = u[2] * ca + w[2] * sa;
        const float ldot = emissive ? 1.0f : (0.72f + 0.28f * (ny * 0.75f + nx * 0.45f - nz * 0.25f));
        const u32 c0 = emissive ? rgbaBase : jnShadeRgba(rgbaBase, ldot);
        const u32 c1 = emissive ? rgbaTip : jnShadeRgba(rgbaTip, ldot);
        jnSetVtx(&v[i], aMm[0] + nx * r0Mm, aMm[1] + ny * r0Mm, aMm[2] + nz * r0Mm, c0);
        jnSetVtx(&v[6 + i], bMm[0] + nx * r1Mm, bMm[1] + ny * r1Mm, bMm[2] + nz * r1Mm, c1);
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), 12, 0);
    for (int i = 0; i < 6; i++) {
        int j = (i + 1) % 6;
        gSP2Triangles(gdl++, i, j, 6 + j, 0, i, 6 + j, 6 + i, 0);
    }
    return gdl;
}

static Gfx *jnDrawLightsaberExView(Gfx *gdl, const float hiltView[3], const float dirView[3],
                                   float lenCm, float extend01, int colorIdx, float scale, int doubleEnded)
{
    unsigned int core = 0xFFFFFFFFu, glow = 0x2890FFE8u;
    gevrJnSaberPalette(colorIdx, &core, &glow);
    const float hiltMm = doubleEnded ? 320.0f : 240.0f;
    const float backOff = doubleEnded ? -160.0f : 0.0f;
    const float fwdOff = doubleEnded ? 160.0f : 240.0f;
    const float hiltStart[3] = { dirView[0] * backOff, dirView[1] * backOff, dirView[2] * backOff };
    const float emitter[3] = { dirView[0] * fwdOff, dirView[1] * fwdOff, dirView[2] * fwdOff };
    const float backEmitter[3] = { -dirView[0] * 160.0f, -dirView[1] * 160.0f, -dirView[2] * 160.0f };
    const float negDir[3] = { -dirView[0], -dirView[1], -dirView[2] };

    Gfx *beg = jnBeginGeom(gdl, hiltView, scale, 0);
    if (!beg) return gdl;
    gdl = beg;
    gdl = jnEmitPrism(gdl, hiltStart, dirView, hiltMm, 17.0f, 15.0f, 0x282A30FFu, 0x808894FFu, 0);
    if (extend01 > 0.02f) {
        const float bladeMm = (lenCm * 10.0f - 240.0f) * extend01;
        gdl = jnEmitPrism(gdl, emitter, dirView, bladeMm, 11.0f, 6.0f, core, core, 1);
        if (doubleEnded) {
            gdl = jnEmitPrism(gdl, backEmitter, negDir, bladeMm * 0.92f, 11.0f, 6.0f, core, core, 1);
        }
        beg = jnBeginGeom(gdl, hiltView, scale, 1);
        if (beg) {
            gdl = beg;
            const float pulse = 1.0f + 0.08f * sinf(s_time * 38.0f);
            gdl = jnEmitPrism(gdl, emitter, dirView, bladeMm + 25.0f, 25.0f * pulse, 14.0f * pulse, glow, glow & 0xFFFFFF90u, 1);
            if (doubleEnded) {
                gdl = jnEmitPrism(gdl, backEmitter, negDir, bladeMm * 0.92f + 25.0f,
                                  25.0f * pulse, 14.0f * pulse, glow, glow & 0xFFFFFF90u, 1);
            }
        }
    }
    return gdl;
}

static Gfx *jnDrawLightsaberView(Gfx *gdl, const float hiltView[3], const float dirView[3],
                                 float lenCm, float extend01, int colorIdx, float scale)
{
    return jnDrawLightsaberExView(gdl, hiltView, dirView, lenCm, extend01, colorIdx, scale, 0);
}

static Gfx *jnDrawKatanaView(Gfx *gdl, const float hiltView[3], const float dirView[3],
                             float lenCm, int isGold, float scale)
{
    const float hiltMm = 220.0f;
    const float bladeMm = fmaxf(280.0f, lenCm * 10.0f - hiltMm);
    const float zero[3] = { 0, 0, 0 };
    Gfx *beg = jnBeginGeom(gdl, hiltView, scale, 0);
    if (!beg) return gdl;
    gdl = beg;
    gdl = jnEmitPrism(gdl, zero, dirView, hiltMm, 15.0f, 14.0f, 0x1E2028FFu, 0x363A44FFu, 0);
    const float tsuba[3] = { dirView[0] * hiltMm, dirView[1] * hiltMm, dirView[2] * hiltMm };
    gdl = jnEmitPrism(gdl, tsuba, dirView, 18.0f, 36.0f, 34.0f, isGold ? 0xFFD230FFu : 0xC0A040FFu, 0x705820FFu, 0);
    const float bladeBase[3] = { dirView[0] * (hiltMm + 18.0f), dirView[1] * (hiltMm + 18.0f), dirView[2] * (hiltMm + 18.0f) };
    const u32 c0 = isGold ? 0xFFE252FFu : 0xD4DCE6FFu;
    const u32 c1 = isGold ? 0xFFF4A8FFu : 0xF8FCFFFFu;
    return jnEmitPrism(gdl, bladeBase, dirView, bladeMm, 13.0f, 2.5f, c0, c1, 0);
}

static Gfx *jnDrawShieldView(Gfx *gdl, const float centerView[3], const float rightView[3],
                             const float upView[3], float scale)
{
    Gfx *beg = jnBeginGeom(gdl, centerView, scale, 0);
    if (!beg) return gdl;
    gdl = beg;
    Vtx *v = dynAllocateVertices(17);
    if (!v) return gdl;
    jnSetVtx(&v[0], 0, 0, 0, 0x485260FFu);
    for (int i = 0; i < 8; i++) {
        const float a = (float)i * (6.2831853f / 8.0f);
        const float ca = cosf(a), sa = sinf(a);
        const float sh = 0.72f + 0.28f * sa;
        jnSetVtx(&v[1 + i], (rightView[0] * ca + upView[0] * sa) * 145.0f,
                 (rightView[1] * ca + upView[1] * sa) * 145.0f,
                 (rightView[2] * ca + upView[2] * sa) * 145.0f, jnShadeRgba(0x363E4AFFu, sh));
        jnSetVtx(&v[9 + i], (rightView[0] * ca + upView[0] * sa) * 180.0f,
                 (rightView[1] * ca + upView[1] * sa) * 180.0f,
                 (rightView[2] * ca + upView[2] * sa) * 180.0f, jnShadeRgba(0xE6B830FFu, sh));
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), 17, 0);
    for (int i = 0; i < 8; i++) {
        int j = (i + 1) & 7;
        gSP1Triangle(gdl++, 0, 1 + i, 1 + j, 0);
        gSP2Triangles(gdl++, 1 + i, 9 + i, 9 + j, 0, 1 + i, 9 + j, 1 + j, 0);
    }
    return gdl;
}

static Gfx *jnDrawBoomerangView(Gfx *gdl, const float centerView[3], float spinAngle,
                                int variant, float scale)
{
    Gfx *beg = jnBeginGeom(gdl, centerView, scale, 0);
    if (!beg) return gdl;
    gdl = beg;
    const int isGold = (variant == GEVR_JN_BOOM_GOLD_SHURIKEN || variant == GEVR_JN_BOOM_GOLD_WING);
    const int isWing = (variant == GEVR_JN_BOOM_WING || variant == GEVR_JN_BOOM_GOLD_WING);
    const u32 cCenter = isGold ? 0x20242CFFu : 0x3A424EFFu;
    const u32 cTip = isGold ? 0xFFD432FFu : 0xE8F0F8FFu;

    if (isWing) {
        Vtx *v = dynAllocateVertices(6);
        if (!v) return gdl;
        const float ca = cosf(spinAngle), sa = sinf(spinAngle);
        const float p[6][2] = {
            { 0, 0 }, { -170, 110 }, { -120, 145 },
            { 35, 35 }, { 170, 110 }, { 120, 145 }
        };
        for (int i = 0; i < 6; i++) {
            float x = p[i][0] * ca - p[i][1] * sa;
            float z = p[i][0] * sa + p[i][1] * ca;
            jnSetVtx(&v[i], x, 0.0f, z, (i == 0 || i == 3) ? cCenter : cTip);
        }
        gSPVertex(gdl++, osVirtualToPhysical(v), 6, 0);
        gSP2Triangles(gdl++, 0, 1, 2, 0, 0, 2, 3, 0);
        gSP2Triangles(gdl++, 0, 3, 5, 0, 0, 5, 4, 0);
        return gdl;
    }

    Vtx *v = dynAllocateVertices(9);
    if (!v) return gdl;
    jnSetVtx(&v[0], 0, 0, 0, cCenter);
    for (int i = 0; i < 4; i++) {
        const float a = spinAngle + (float)i * 1.5707963f;
        const float ca = cosf(a), sa = sinf(a);
        jnSetVtx(&v[1 + i * 2], ca * 175.0f, 0.0f, sa * 175.0f, cTip);
        jnSetVtx(&v[2 + i * 2], cosf(a + 0.55f) * 55.0f, 0.0f, sinf(a + 0.55f) * 55.0f, cCenter);
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), 9, 0);
    for (int i = 0; i < 4; i++) {
        gSP1Triangle(gdl++, 0, 1 + i * 2, 2 + i * 2, 0);
    }
    return gdl;
}

static Gfx *jnDrawSparkView(Gfx *gdl, const float centerView[3], float radiusCm, unsigned int rgba, float scale)
{
    Gfx *beg = jnBeginGeom(gdl, centerView, scale, 1);
    if (!beg) return gdl;
    gdl = beg;
    Vtx *v = dynAllocateVertices(11);
    if (!v) return gdl;
    jnSetVtx(&v[0], 0, 0, 0, 0xFFFFFFFFu);
    for (int i = 0; i < 10; i++) {
        const float a = (float)i * (6.2831853f / 10.0f) + s_time * 8.0f;
        const float r = (i & 1) ? radiusCm * 10.0f : radiusCm * 4.2f;
        jnSetVtx(&v[1 + i], cosf(a) * r, sinf(a) * r, 0.0f, rgba);
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), 11, 0);
    for (int i = 0; i < 10; i++) {
        gSP1Triangle(gdl++, 0, 1 + i, 1 + ((i + 1) % 10), 0);
    }
    return gdl;
}

static Gfx *jnDrawLightningView(Gfx *gdl, const float aView[3], const float bView[3],
                                unsigned int outerRgba, float scale, int seed)
{
    Gfx *beg = jnBeginGeom(gdl, aView, scale, 1);
    if (!beg) return gdl;
    gdl = beg;
    const int segs = 7;
    Vtx *v = dynAllocateVertices((segs + 1) * 2);
    if (!v) return gdl;
    const float d[3] = {
        (bView[0] - aView[0]) * 10.0f / scale,
        (bView[1] - aView[1]) * 10.0f / scale,
        (bView[2] - aView[2]) * 10.0f / scale
    };
    for (int i = 0; i <= segs; i++) {
        const float u = (float)i / (float)segs;
        const float jitter = (i == 0 || i == segs) ? 0.0f : 55.0f * sinf(s_time * 48.0f + (float)(i * 19 + seed));
        const float jv = (i == 0 || i == segs) ? 0.0f : 35.0f * cosf(s_time * 53.0f + (float)(i * 13 + seed));
        const float px = d[0] * u + jitter, py = d[1] * u + jv, pz = d[2] * u;
        jnSetVtx(&v[i * 2], px + 9.0f, py + 9.0f, pz, outerRgba);
        jnSetVtx(&v[i * 2 + 1], px - 9.0f, py - 9.0f, pz, 0xFFFFFFFFu);
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), (segs + 1) * 2, 0);
    for (int i = 0; i < segs; i++) {
        int o = i * 2;
        gSP2Triangles(gdl++, o, o + 1, o + 2, 0, o + 1, o + 3, o + 2, 0);
    }
    return gdl;
}

static void jnWorldToView(const Mtxf *w2s, const float w[3], float s, float out[3])
{
    coord3d c;
    c.x = w[0]; c.y = w[1]; c.z = w[2];
    mtx4TransformVecInPlace((Mtxf *)w2s, &c);
    out[0] = c.x * s; out[1] = c.y * s; out[2] = c.z * s;
}

/* Read-only per-eye transform of world-space motion ribbons (never mutates s_trails[]) */
static Gfx *jnDrawTrailWorld(Gfx *gdl, const Mtxf *w2s, const JnBladeTrail *tr, unsigned int rgba, float scale)
{
    if (!tr || tr->count < 2 || !w2s) return gdl;
    float origV[3];
    jnWorldToView(w2s, tr->hilt[0], scale, origV);
    Gfx *beg = jnBeginGeom(gdl, origV, scale, 1);
    if (!beg) return gdl;
    gdl = beg;
    Vtx *v = dynAllocateVertices(tr->count * 2);
    if (!v) return gdl;
    const float invS = 10.0f / scale;
    const unsigned int rgb = rgba & 0xFFFFFF00u;
    const unsigned int maxA = rgba & 0xFFu;
    for (int i = 0; i < tr->count; i++) {
        float hv[3], tv[3];
        jnWorldToView(w2s, tr->hilt[i], scale, hv);
        jnWorldToView(w2s, tr->tip[i], scale, tv);
        const float fade = 1.0f - (float)i / (float)tr->count;
        const unsigned int aTip = (unsigned int)((float)maxA * fade * 0.72f);
        const unsigned int aBase = (unsigned int)((float)maxA * fade * 0.16f);
        jnSetVtx(&v[i * 2 + 0],
                 (hv[0] - origV[0]) * invS,
                 (hv[1] - origV[1]) * invS,
                 (hv[2] - origV[2]) * invS, rgb | aBase);
        jnSetVtx(&v[i * 2 + 1],
                 (tv[0] - origV[0]) * invS,
                 (tv[1] - origV[1]) * invS,
                 (tv[2] - origV[2]) * invS, rgb | aTip);
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), tr->count * 2, 0);
    for (int i = 0; i < tr->count - 1; i++) {
        const int o = i * 2;
        gSP2Triangles(gdl++, o, o + 1, o + 3, 0, o, o + 3, o + 2, 0);
    }
    return gdl;
}

static Gfx *jnDrawSmokeView(Gfx *gdl, const float centerView[3], float radiusCm, float alpha01, float scale)
{
    if (alpha01 <= 0.02f) return gdl;
    Gfx *beg = jnBeginGeom(gdl, centerView, scale, 1);
    if (!beg) return gdl;
    gdl = beg;
    Vtx *v = dynAllocateVertices(9);
    if (!v) return gdl;
    const unsigned int a0 = (unsigned int)(alpha01 * 175.0f);
    const unsigned int a1 = (unsigned int)(alpha01 * 38.0f);
    jnSetVtx(&v[0], 0.0f, 0.0f, 0.0f, 0x38445400u | a0);
    const float r = radiusCm * 10.0f;
    for (int i = 0; i < 8; i++) {
        const float a = (float)i * (6.2831853f / 8.0f) + s_time * 1.4f;
        jnSetVtx(&v[1 + i], cosf(a) * r, sinf(a) * r, 0.0f, 0x60708400u | a1);
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), 9, 0);
    for (int i = 0; i < 8; i++) {
        gSP1Triangle(gdl++, 0, 1 + i, 1 + ((i + 1) & 7), 0);
    }
    return gdl;
}

/* -------------------------------------------------------------------------- */
/* World Render (Thrown Weapons, Sith/Ninja/Ally Blades, VFX)                 */
/* -------------------------------------------------------------------------- */

Gfx *gevrJnDrawWorld(Gfx *gdl)
{
    if (!g_CurrentPlayer || !jnInGameplay()) return gdl;
    const Mtxf *w2s = camGetWorldToScreenMtxf();
    if (!w2s) return gdl;
    const float s = jnViewScale();
    Gfx *const startGdl = gdl;

    /* Player's thrown Lightsaber or Boomerang */
    if (s_thrown.active) {
        float pv[3];
        jnWorldToView(w2s, s_thrown.pos, s, pv);
        if (pv[2] < -10.0f) {
            if (!s_thrown.isBoomerang) {
                const float spin[3] = { cosf(s_thrown.spinAngle), 0.0f, sinf(s_thrown.spinAngle) };
                gdl = jnDrawLightsaberExView(gdl, pv, spin, 105.0f, 1.0f, VrSaberColor, s,
                                             VrSaberStyle == GEVR_JN_STYLE_DOUBLE);
            } else {
                gdl = jnDrawBoomerangView(gdl, pv, s_thrown.spinAngle, VrNinjaBoomerang, s);
            }
        }
    }

    /* Special NPCs (Sith / Evil Ninjas / Jedi Ally) & their thrown boomerangs */
    for (int i = 0; i < JN_MAX_SPECIAL; i++) {
        JnSpecialNpc *sp = &s_specials[i];
        if (!sp->used) continue;
        if (sp->boomerang.active) {
            float pv[3];
            jnWorldToView(w2s, sp->boomerang.pos, s, pv);
            if (pv[2] < -10.0f) {
                gdl = jnDrawBoomerangView(gdl, pv, sp->boomerang.spinAngle, GEVR_JN_BOOM_SHURIKEN, s);
            }
        }
        if (!jnChrAliveValid(sp->chr, sp->chrnum)) continue;
        ChrRecord *c = sp->chr;

        float hiltW[3], dirW[3];
        if (sp->handValid && (g_GlobalTimer - sp->handTick) <= 8) {
            memcpy(hiltW, sp->handPosW, sizeof(hiltW));
            memcpy(dirW, sp->handDirW, sizeof(dirW));
        } else {
            const float yaw = c->model ? getsubroty(c->model) : 0.0f;
            const float cy = cosf(yaw), sy = sinf(yaw);
            hiltW[0] = c->prop->pos.x + cy * 18.0f * sp->scale + sy * 16.0f * sp->scale;
            hiltW[1] = c->prop->pos.y + 106.0f * sp->scale;
            hiltW[2] = c->prop->pos.z - sy * 18.0f * sp->scale + cy * 16.0f * sp->scale;
            dirW[0] = sy * 0.60f;
            dirW[1] = 0.72f;
            dirW[2] = cy * 0.60f;
            jnNorm3(dirW, dirW);
        }
        if (sp->swingTimer > 0.0f) {
            const float swingDrop = sinf((sp->swingTimer / 0.28f) * 3.14159f) * 0.65f;
            dirW[1] -= swingDrop;
            jnNorm3(dirW, dirW);
        }

        const float tipWorld[3] = {
            hiltW[0] + dirW[0] * 100.0f * sp->scale,
            hiltW[1] + dirW[1] * 100.0f * sp->scale,
            hiltW[2] + dirW[2] * 100.0f * sp->scale
        };
        float hv[3], tv[3];
        jnWorldToView(w2s, hiltW, s, hv);
        if (hv[2] >= -10.0f) continue;
        jnWorldToView(w2s, tipWorld, s, tv);
        float d[3] = { tv[0] - hv[0], tv[1] - hv[1], tv[2] - hv[2] };
        jnNorm3(d, d);
        if (sp->kind == JN_NPC_SITH || sp->kind == JN_NPC_ALLY_JEDI) {
            gdl = jnDrawLightsaberExView(gdl, hv, d, 110.0f * sp->scale, sp->ignite.extend, sp->color, s,
                                         sp->kind == JN_NPC_SITH && sp->doubleBladed);
        } else {
            gdl = jnDrawKatanaView(gdl, hv, d, 105.0f, 0, s);
        }
    }

    /* Ninja Shadow-Step smoke clouds */
    for (int i = 0; i < JN_MAX_SMOKE; i++) {
        if (!s_smoke[i].used) continue;
        float pv[3];
        jnWorldToView(w2s, s_smoke[i].pos, s, pv);
        if (pv[2] < -10.0f) {
            const float u = s_smoke[i].age / s_smoke[i].maxAge;
            gdl = jnDrawSmokeView(gdl, pv, s_smoke[i].radius * (0.6f + 0.9f * u), 1.0f - u, s);
        }
    }

    /* Clash / ricochet starbursts */
    for (int i = 0; i < JN_MAX_SPARKS; i++) {
        if (!s_sparks[i].used) continue;
        float pv[3];
        jnWorldToView(w2s, s_sparks[i].pos, s, pv);
        if (pv[2] < -10.0f) {
            const float k = 1.0f - s_sparks[i].age / s_sparks[i].maxAge;
            gdl = jnDrawSparkView(gdl, pv, s_sparks[i].radius * (0.4f + 0.8f * k), s_sparks[i].rgba, s);
        }
    }

    /* Deflected laser bolts */
    for (int i = 0; i < JN_MAX_TRACERS; i++) {
        if (!s_tracers[i].used) continue;
        float av[3], bv[3];
        jnWorldToView(w2s, s_tracers[i].from, s, av);
        jnWorldToView(w2s, s_tracers[i].to, s, bv);
        gdl = jnDrawLightningView(gdl, av, bv, s_tracers[i].rgba, s, i * 7);
    }

    /* Sith NPC Force Lightning */
    if (s_sithLightningShow > 0.0f) {
        float av[3], bv[3];
        jnWorldToView(w2s, s_sithLightningFrom, s, av);
        jnWorldToView(w2s, s_sithLightningTo, s, bv);
        gdl = jnDrawLightningView(gdl, av, bv, 0xFF3838FFu, s, 42);
    }

    if (gdl != startGdl) {
        gDPPipeSync(gdl++);
        gSPTexture(gdl++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
        gdl = combiner_bayer_lod_perspective(gdl);
    }
    return gdl;
}

/* -------------------------------------------------------------------------- */
/* First-Person Viewmodel Render (Lightsaber, Swords, Shield, Force FX)       */
/* -------------------------------------------------------------------------- */

Gfx *gevrJnDrawFirstPerson(Gfx *gdl)
{
    if (!jnModeActive() || jnUsingGunsOverride() || !jnInGameplay()) return gdl;
    const Mtxf *w2s = camGetWorldToScreenMtxf();
    const float s = jnViewScale();
    const float sideSign = VrJediHand ? -1.0f : 1.0f;
    const int bCtrl = VrJediHand ? 0 : 1;
    const int fCtrl = 1 - bCtrl;
    Gfx *const startGdl = gdl;

    float offHilt[3] = { -16.0f * sideSign * s, -10.0f * s, -30.0f * s };
    float offDir[3] = { 0.22f * sideSign, 0.72f, -0.66f };
    float offRight[3] = { 1.0f, 0.0f, 0.0f };
    float offUp[3] = { 0.0f, 1.0f, 0.0f };
    int haveVrOff = 0;
    if (g_gevrStereo) {
        float ob[3];
        if (gevrJnGetGripAxes(fCtrl, offHilt, offRight, offUp, ob)) {
            offDir[0] = -ob[0] * 0.35f + offUp[0] * 0.94f;
            offDir[1] = -ob[1] * 0.35f + offUp[1] * 0.94f;
            offDir[2] = -ob[2] * 0.35f + offUp[2] * 0.94f;
            jnNorm3(offDir, offDir);
            haveVrOff = 1;
        }
    }

    /* Weapon hand pose (VR tracked hand or Flat animated swing/block) */
    if (!s_thrown.active) {
        float hilt[3], tipDir[3];
        int haveVr = 0;
        if (g_gevrStereo) {
            float r[3], u[3], b[3];
            if (gevrJnGetGripAxes(bCtrl, hilt, r, u, b)) {
                tipDir[0] = -b[0] * 0.35f + u[0] * 0.94f;
                tipDir[1] = -b[1] * 0.35f + u[1] * 0.94f;
                tipDir[2] = -b[2] * 0.35f + u[2] * 0.94f;
                jnNorm3(tipDir, tipDir);
                haveVr = 1;
            }
        }
        if (!haveVr) {
            if (s_blocking) {
                hilt[0] = -14.0f * sideSign * s; hilt[1] = -6.0f * s; hilt[2] = -34.0f * s;
                tipDir[0] = 0.65f * sideSign; tipDir[1] = 0.72f; tipDir[2] = -0.22f;
            } else if (s_slash.active) {
                const float u = s_slash.duration > 0.0f ? s_slash.t / s_slash.duration : 0.0f;
                const float swing = (s_slash.dir == 0 ? 1.0f - 2.0f * u : -1.0f + 2.0f * u) * sideSign;
                hilt[0] = (14.0f * swing) * s; hilt[1] = (-10.0f - 4.0f * sinf(u * 3.14159f)) * s; hilt[2] = -36.0f * s;
                tipDir[0] = -0.55f * swing; tipDir[1] = 0.35f - 0.5f * u; tipDir[2] = -0.75f;
            } else {
                hilt[0] = 17.0f * sideSign * s; hilt[1] = -14.0f * s; hilt[2] = -32.0f * s;
                tipDir[0] = -0.22f * sideSign; tipDir[1] = 0.72f; tipDir[2] = -0.66f;
            }
            jnNorm3(tipDir, tipDir);
        }

        if (VrJediNinjaMode == GEVR_JN_JEDI) {
            const int dbl = (VrSaberStyle == GEVR_JN_STYLE_DOUBLE);
            unsigned int core = 0xFFFFFFFFu, glow = 0x2890FFE8u;
            gevrJnSaberPalette(VrSaberColor, &core, &glow);
            gdl = jnDrawLightsaberExView(gdl, hilt, tipDir, 115.0f, s_fpIgnite.extend, VrSaberColor, s, dbl);
            if (VrSaberStyle == GEVR_JN_STYLE_DUAL) {
                float ohilt[3] = {
                    haveVrOff ? offHilt[0] : -17.0f * sideSign * s,
                    haveVrOff ? offHilt[1] : -14.0f * s,
                    haveVrOff ? offHilt[2] : -32.0f * s
                };
                float odir[3] = { offDir[0], offDir[1], offDir[2] };
                if (!haveVrOff && s_offSlash.active) {
                    const float u = s_offSlash.t / fmaxf(0.01f, s_offSlash.duration);
                    odir[0] = -0.55f * sideSign * (1.0f - 2.0f * u);
                    odir[2] = -0.78f;
                }
                jnNorm3(odir, odir);
                gdl = jnDrawLightsaberView(gdl, ohilt, odir, 105.0f, s_fpIgnite.extend, VrSaberColor, s);
            }
            if (VrJnBladeTrails && w2s) {
                gdl = jnDrawTrailWorld(gdl, w2s, &s_trails[0], glow, s);
                if (dbl) gdl = jnDrawTrailWorld(gdl, w2s, &s_trails[1], glow, s);
                if (VrSaberStyle == GEVR_JN_STYLE_DUAL) gdl = jnDrawTrailWorld(gdl, w2s, &s_trails[2], glow, s);
            }
        } else if (VrJediNinjaMode == GEVR_JN_NINJA) {
            const int sw = jnEffectiveNinjaSword();
            const unsigned int nTrailRgba = gevrJnIsGoldSword(sw) ? 0xFFE058D8u : 0xD8ECFFD8u;
            if (gevrJnSelectedNinjaSlot == GEVR_JN_NINJA_SLOT_BOOMERANG) {
                const float bp[3] = { 16.0f * sideSign * s, -11.0f * s, -34.0f * s };
                gdl = jnDrawBoomerangView(gdl, haveVr ? hilt : bp, s_time * 3.0f, VrNinjaBoomerang, s);
            } else {
                const float reachCm = gevrJnWeaponReachCm(GEVR_JN_NINJA, sw);
                gdl = jnDrawKatanaView(gdl, hilt, tipDir, reachCm, gevrJnIsGoldSword(sw), s);
            }
            /* Off-hand dual short sword or Ninja Shield */
            if (gevrJnIsDualSword(GEVR_JN_NINJA, sw)) {
                float ohilt[3] = {
                    haveVrOff ? offHilt[0] : -17.0f * sideSign * s,
                    haveVrOff ? offHilt[1] : -14.0f * s,
                    haveVrOff ? offHilt[2] : -32.0f * s
                };
                float odir[3] = { offDir[0], offDir[1], offDir[2] };
                if (!haveVrOff && s_offSlash.active) {
                    const float u = s_offSlash.t / fmaxf(0.01f, s_offSlash.duration);
                    odir[0] = -0.5f * sideSign * (1.0f - 2.0f * u);
                    odir[2] = -0.8f;
                }
                jnNorm3(odir, odir);
                gdl = jnDrawKatanaView(gdl, ohilt, odir, 70.0f, gevrJnIsGoldSword(sw), s);
            } else if (jnHasNinjaShield()) {
                float sc[3] = {
                    haveVrOff ? offHilt[0] : (s_ninjaShieldHeld ? -4.0f * sideSign * s : -18.0f * sideSign * s),
                    haveVrOff ? offHilt[1] : (s_ninjaShieldHeld ? -2.0f * s : -11.0f * s),
                    haveVrOff ? offHilt[2] : (s_ninjaShieldHeld ? -36.0f * s : -30.0f * s)
                };
                gdl = jnDrawShieldView(gdl, sc, offRight, offUp, s);
            }
            if (VrJnBladeTrails && w2s) {
                gdl = jnDrawTrailWorld(gdl, w2s, &s_trails[0], nTrailRgba, s);
                if (gevrJnIsDualSword(GEVR_JN_NINJA, sw)) gdl = jnDrawTrailWorld(gdl, w2s, &s_trails[2], nTrailRgba, s);
            }
        }
    }

    /* Off-hand Force Lightning arcs */
    if (s_lightningActive && w2s) {
        for (int i = 0; i < s_lightningCount; i++) {
            if (!jnChrAliveValid(s_lightningChrs[i], s_lightningChrnums[i])) continue;
            const float tgtW[3] = {
                s_lightningChrs[i]->prop->pos.x,
                s_lightningChrs[i]->prop->pos.y + 115.0f,
                s_lightningChrs[i]->prop->pos.z
            };
            float tv[3];
            jnWorldToView(w2s, tgtW, s, tv);
            gdl = jnDrawLightningView(gdl, offHilt, tv, 0x50B8FFFFu, s, i * 11);
        }
    }

    /* Force Choke tether line */
    if (s_chokeActive && w2s && jnChrAliveValid(s_chokeChr, s_chokeChrnum)) {
        const float tgtW[3] = {
            s_chokeChr->prop->pos.x,
            s_chokeChr->prop->pos.y + 125.0f,
            s_chokeChr->prop->pos.z
        };
        float tv[3];
        jnWorldToView(w2s, tgtW, s, tv);
        gdl = jnDrawLightningView(gdl, offHilt, tv, 0xFF5050C0u, s, 99);
    }

    if (gdl != startGdl) {
        gDPPipeSync(gdl++);
        gSPTexture(gdl++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
        gdl = combiner_bayer_lod_perspective(gdl);
    }
    return gdl;
}
