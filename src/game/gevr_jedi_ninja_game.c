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
extern int VrJediBlasters, VrJediPickupGuns, VrNinjaPickupGuns, VrLeftHandedMode;
extern int VrRagdolls, VrDismemberment, VrBodiesStay;
extern s32 g_gevrStereo, g_StageNum, g_ClockTimer, g_GlobalTimer;
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
extern int bondinvAddInvItem(ITEM_IDS item);
extern s32 currentPlayerEquipWeaponWrapper(GUNHAND hand, s32 next_weapon);
extern s32 getCurrentPlayerWeaponId(GUNHAND hand);
extern Gfx *sub_GAME_7F061E18(Gfx *gdl, BeamRecord *flash, s32 arg2);
extern float get_analog_value(int hand_index, const char *input_name);
extern float vr_ctrl_velocity_play[2][3];
extern int vr_haptics_ready(void);
extern s32 trigger_haptic_vibration_c(int hand_index, float amplitude, float duration);
extern s32 plot_course_for_actor(ChrRecord *chr, coord3d *pos, StandTile *tile, SPEED speed);
extern s32 chrIsNotDeadOrShot(ChrRecord *chr);
extern Gfx *combiner_bayer_lod_perspective(Gfx *gdl);
extern Gfx *microcode_constructor(Gfx *gdl);

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

enum {
    JN_NPC_SITH = 1,
    JN_NPC_NINJA = 2,
    JN_NPC_ALLY_JEDI = 3
};

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
static float s_vrSlashCd[2];
static float s_vrPowerCd;
static int s_vrTrigWas[2];
static float s_vrThrowGuard;

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
static float s_blasterRecoil;
static int s_lastStage = -1;
static float s_sithLightningShow;
static float s_sithLightningFrom[3], s_sithLightningTo[3];

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

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

static void jnTrailPush(int idx, const float hiltView[3], const float tipView[3], float scale)
{
    if (idx < 0 || idx >= 3 || !VrJnBladeTrails) return;
    JnBladeTrail *tr = &s_trails[idx];
    if (tr->count > 0) {
        const float dx = tipView[0] - tr->tip[0][0];
        const float dy = tipView[1] - tr->tip[0][1];
        const float dz = tipView[2] - tr->tip[0][2];
        const float minD = 1.6f * scale;
        if (dx * dx + dy * dy + dz * dz < minD * minD) return;
    }
    const int n = tr->count < JN_TRAIL_SAMPLES ? tr->count : JN_TRAIL_SAMPLES - 1;
    for (int i = n; i > 0; i--) {
        memcpy(tr->hilt[i], tr->hilt[i - 1], sizeof(tr->hilt[0]));
        memcpy(tr->tip[i], tr->tip[i - 1], sizeof(tr->tip[0]));
        tr->age[i] = tr->age[i - 1];
    }
    memcpy(tr->hilt[0], hiltView, sizeof(tr->hilt[0]));
    memcpy(tr->tip[0], tipView, sizeof(tr->tip[0]));
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

static void jnHitGuardWithBlade(ChrRecord *c, const float dir[3], float dmgAmount, int forceCutCount)
{
    if (!jnChrAliveValid(c, -1)) return;
    coord3d d;
    d.x = dir[0]; d.y = dir[1]; d.z = dir[2];
    JnSpecialNpc *sp = jnFindSpecial(c);
    if (sp && (sp->kind == JN_NPC_SITH || sp->kind == JN_NPC_NINJA)) {
        c->chrflags |= CHRFLAG_WAS_DAMAGED;
        c->damage += dmgAmount;
        if (c->damage >= c->maxdamage) {
            handles_shot_actors(c, HIT_CHEST, &d, ITEM_GOLDENGUN, TRUE);
        }
    } else {
        handles_shot_actors(c, HIT_CHEST, &d, ITEM_GOLDENGUN, TRUE);
        if (!chrIsDead(c)) {
            c->chrflags |= CHRFLAG_WAS_DAMAGED;
            c->damage += dmgAmount;
            if (c->damage >= c->maxdamage) {
                handles_shot_actors(c, HIT_CHEST, &d, ITEM_GOLDENGUN, TRUE);
            }
        }
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
    const float reach2 = (reachCm + 55.0f) * (reachCm + 55.0f);
    const s32 n = get_numguards();
    for (s32 i = 0; i < n; i++) {
        ChrRecord *c = &g_ChrSlots[i];
        if (!jnIsEnemyGuard(c)) continue;
        const float dx = c->prop->pos.x - eye[0];
        const float dy = (c->prop->pos.y + 100.0f) - eye[1];
        const float dz = c->prop->pos.z - eye[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > reach2 || d2 < 1.0f) continue;
        const float d = sqrtf(d2);
        const float to[3] = { dx / d, dy / d, dz / d };
        if (jnDot3(fwd, to) < 0.35f) continue;
        jnHitGuardWithBlade(c, to, dmg, 2);
        jnHaptic(VrJediHand ? 0 : 1, 0.8f, 0.08f);
    }
}

/* -------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
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
    s_vrSlashCd[0] = s_vrSlashCd[1] = 0.0f;
    s_vrPowerCd = 0.0f;
    s_vrTrigWas[0] = s_vrTrigWas[1] = 0;
    s_vrThrowGuard = 0.0f;
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
    s_blasterRecoil = 0.0f;
    s_sithLightningShow = 0.0f;
    s_lastStage = g_StageNum;
    if (VrJediNinjaMode != 0) {
        VrRagdolls = 1;
        VrDismemberment = 1;
        if (VrBodiesStay == 0) VrBodiesStay = GEVR_JN_DEFAULT_BODIES_STAY;
    }
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

s32 gevrJnDrawBlasterActive(void)
{
    return gevrJnBlastersActive()
        && jnUsingGunsOverride()
        && jnInGameplay()
        && getCurrentPlayerWeaponId(GUNRIGHT) == ITEM_LASER;
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
                const float vx = c->prop->pos.x - playerPos[0];
                const float vz = c->prop->pos.z - playerPos[2];
                if (!gevrJnInterceptPlayerDamage(1.2f, vx, vz)) {
                    record_damage_kills(1.2f, vx, vz, -1, 1);
                }
                sp->boomerang.returning = 1;
            }
            gevrJnThrownStep(&sp->boomerang, handPos, hitWall, dt);
        }

        if (sp->kind == JN_NPC_ALLY_JEDI) continue;
        if (s_chokeActive && s_chokeChr == c) continue; /* suspended & helpless */

        const float dx = playerPos[0] - c->prop->pos.x;
        const float dy = playerPos[1] - (c->prop->pos.y + 100.0f * sp->scale);
        const float dz = playerPos[2] - c->prop->pos.z;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 2200.0f * 2200.0f) continue;

        /* Only engage once within visual range or alerted */
        if (c->alertness < 50 && d2 > 900.0f * 900.0f) continue;

        const float d = sqrtf(fmaxf(1.0f, d2));
        if (d > 110.0f && g_CurrentPlayer && g_CurrentPlayer->prop && g_CurrentPlayer->prop->stan) {
            if (c->actiontype != ACT_GOPOS) {
                plot_course_for_actor(c, &g_CurrentPlayer->prop->pos, g_CurrentPlayer->prop->stan, SPEED_RUN);
            }
            /* Super speed lunge step toward player */
            if (d > 140.0f && d < 1200.0f) {
                const float spd = (sp->kind == JN_NPC_SITH ? 220.0f : 185.0f) * dt;
                float nxt[3] = {
                    c->prop->pos.x + (dx / d) * spd,
                    c->prop->pos.y,
                    c->prop->pos.z + (dz / d) * spd
                };
                if (!pdphysIsReady() || !pdphysRaycastAnyRoom(c->prop->pos.f, nxt, NULL, NULL, NULL)) {
                    jnMoveChrProp(c, nxt);
                }
            }
        }

        /* Melee saber / katana swing */
        const float meleeRange = 145.0f * (sp->scale > 1.0f ? sp->scale : 1.0f);
        if (d <= meleeRange && sp->meleeCd <= 0.0f) {
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
                const float vx = c->prop->pos.x - playerPos[0];
                const float vz = c->prop->pos.z - playerPos[2];
                if (!gevrJnInterceptPlayerDamage(1.5f, vx, vz)) {
                    record_damage_kills(1.5f, vx, vz, -1, 1);
                }
            }
        }

        /* Special attack: Sith Force Lightning / Push, Evil Ninja Boomerang */
        if (sp->powerCd <= 0.0f && d > 180.0f && d < 950.0f) {
            if (sp->kind == JN_NPC_SITH) {
                sp->powerCd = 4.5f;
                const float vx = c->prop->pos.x - playerPos[0];
                const float vz = c->prop->pos.z - playerPos[2];
                s_sithLightningShow = 0.45f;
                s_sithLightningFrom[0] = c->prop->pos.x;
                s_sithLightningFrom[1] = c->prop->pos.y + 115.0f * sp->scale;
                s_sithLightningFrom[2] = c->prop->pos.z;
                memcpy(s_sithLightningTo, playerPos, sizeof(s_sithLightningTo));
                if (!gevrJnInterceptPlayerDamage(0.9f, vx, vz)) {
                    record_damage_kills(0.9f, vx, vz, -1, 1);
                }
            } else if (sp->kind == JN_NPC_NINJA && !sp->boomerang.active) {
                sp->powerCd = 4.2f;
                const float origin[3] = { c->prop->pos.x, c->prop->pos.y + 115.0f, c->prop->pos.z };
                const float aim[3] = { dx / d, dy / d, dz / d };
                gevrJnThrownLaunch(&sp->boomerang, 1, origin, aim);
            }
        }
    }
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
        /* Melee strike against closest enemy guard */
        const s32 n = get_numguards();
        ChrRecord *best = NULL;
        float bestD2 = 175.0f * 175.0f;
        for (s32 i = 0; i < n; i++) {
            ChrRecord *e = &g_ChrSlots[i];
            if (!jnIsEnemyGuard(e)) continue;
            const float dx = e->prop->pos.x - ally->prop->pos.x;
            const float dy = e->prop->pos.y - ally->prop->pos.y;
            const float dz = e->prop->pos.z - ally->prop->pos.z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < bestD2) { bestD2 = d2; best = e; }
        }
        if (best) {
            const float dir[3] = {
                best->prop->pos.x - ally->prop->pos.x,
                0.0f,
                best->prop->pos.z - ally->prop->pos.z
            };
            float nd[3];
            jnNorm3(dir, nd);
            jnHitGuardWithBlade(best, nd, 10.0f, 1);
            sp->meleeCd = 0.7f;
        }
    }
    return 0; /* let gevrAllyThink run normal follow/target pathing too */
}

static ChrRecord *jnFindForceTarget(const float eye[3], const float fwd[3], float maxRangeCm, float minDot)
{
    ChrRecord *best = NULL;
    float bestScore = 1e30f;
    const s32 n = get_numguards();
    for (s32 i = 0; i < n; i++) {
        ChrRecord *c = &g_ChrSlots[i];
        if (!jnIsEnemyGuard(c)) continue;
        const float dx = c->prop->pos.x - eye[0];
        const float dy = (c->prop->pos.y + 100.0f) - eye[1];
        const float dz = c->prop->pos.z - eye[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < 100.0f || d2 > maxRangeCm * maxRangeCm) continue;
        const float d = sqrtf(d2);
        const float dot = (dx * fwd[0] + dy * fwd[1] + dz * fwd[2]) / d;
        if (dot < minDot) continue;
        const float score = d * (1.4f - dot);
        if (score < bestScore) { bestScore = score; best = c; }
    }
    return best;
}

static void jnTriggerForcePush(const float eye[3], const float fwd[3])
{
    const s32 n = get_numguards();
    for (s32 i = 0; i < n; i++) {
        ChrRecord *c = &g_ChrSlots[i];
        if (!jnIsEnemyGuard(c)) continue;
        const float dx = c->prop->pos.x - eye[0];
        const float dy = (c->prop->pos.y + 90.0f) - eye[1];
        const float dz = c->prop->pos.z - eye[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 650.0f * 650.0f || d2 < 1.0f) continue;
        const float d = sqrtf(d2);
        const float dir[3] = { dx / d, dy / d, dz / d };
        if (jnDot3(fwd, dir) < 0.55f) continue;
        jnHitGuardWithBlade(c, dir, 14.0f, 0);
        const float spark[3] = { c->prop->pos.x, c->prop->pos.y + 105.0f, c->prop->pos.z };
        jnAddSpark(spark, 0xA0D8FFE0u, 24.0f, 0.25f);
    }
    for (int slot = 0; slot < 24; slot++) {
        float pos[3];
        if (!gevrRagdollGetBodyPos(slot, pos)) continue;
        const float dx = pos[0] - eye[0], dy = pos[1] - eye[1], dz = pos[2] - eye[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 750.0f * 750.0f || d2 < 1.0f) continue;
        const float d = sqrtf(d2);
        const float dir[3] = { dx / d, dy / d, dz / d };
        if (jnDot3(fwd, dir) < 0.50f) continue;
        const float vel[3] = { fwd[0] * 1350.0f, fwd[1] * 1350.0f + 260.0f, fwd[2] * 1350.0f };
        gevrRagdollThrowBody(slot, vel);
    }
    if (VrJediForcePowers && VrJediCatchProjectiles && g_CurrentPlayer && g_CurrentPlayer->prop) {
        for (int i = 0; i < PROJECTILES_ARR_MAX; i++) {
            if (!jnProjValidAirborne(i)) continue;
            Projectile *pr = &g_Projectiles[i];
            const float dx = pr->obj->prop->pos.x - eye[0];
            const float dy = pr->obj->prop->pos.y - eye[1];
            const float dz = pr->obj->prop->pos.z - eye[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (i != s_caughtProjIdx) {
                if (d2 > 750.0f * 750.0f || d2 < 1.0f) continue;
                const float d = sqrtf(d2);
                const float dir[3] = { dx / d, dy / d, dz / d };
                if (jnDot3(fwd, dir) < 0.55f) continue;
            }
            float spd = sqrtf(pr->speed.x * pr->speed.x + pr->speed.y * pr->speed.y + pr->speed.z * pr->speed.z);
            if (spd < 18.0f) spd = 22.0f;
            pr->speed.x = fwd[0] * spd;
            pr->speed.y = fwd[1] * spd;
            pr->speed.z = fwd[2] * spd;
            pr->ownerprop = g_CurrentPlayer->prop;
            jnAddSpark(pr->obj->prop->pos.f, 0x88E0FFFFu, 26.0f, 0.25f);
        }
        s_caughtProjIdx = -1;
    }
    jnSfx(GEVR_JN_SFX_FORCE_PUSH, 1.15f);
    jnHaptic(VrJediHand ? 1 : 0, 0.7f, 0.08f);
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

    if (s_blasterRecoil > 0.0f) {
        s_blasterRecoil = fmaxf(0.0f, s_blasterRecoil - dt * 6.0f);
    }
    if (gevrJnDrawBlasterActive() && g_CurrentPlayer->hands[GUNRIGHT].weapon_beam.unk00 >= 0
        && g_CurrentPlayer->hands[GUNRIGHT].weapon_beam.unk00 <= 1) {
        s_blasterRecoil = 1.0f;
    }

    if (!jnModeActive() || jnUsingGunsOverride()) {
        s_blocking = 0;
        s_blockHoldSec = 0.0f;
        s_ninjaShieldHeld = 0;
        s_lightningActive = 0;
        s_saberLockTimer = 0.0f;
        s_caughtProjIdx = -1;
        jnRestoreShadowSpeed();
        gevrJnAudioSetState(0, 0, 0.0f, 0.0f, 0, 0, 0, 0);
        return;
    }

    VrRagdolls = 1;
    VrDismemberment = 1;
    if (VrBodiesStay == 0) VrBodiesStay = GEVR_JN_DEFAULT_BODIES_STAY;

    /* Determine RT / LT actions (Flat vs VR) */
    int bladeTap = 0, bladeBlock = 0, bladeThrow = 0;
    int offHold = 0, offRelease = 0, offDouble = 0;
    float bVelCm[3] = { 0.0f, 0.0f, 0.0f };
    float fVelCm[3] = { 0.0f, 0.0f, 0.0f };
    float vrSwing01 = 0.0f;
    float bladeHandW[3], forceHandW[3];
    memcpy(bladeHandW, eye, sizeof(bladeHandW));
    memcpy(forceHandW, eye, sizeof(forceHandW));
    for (int k = 0; k < 3; k++) {
        bladeHandW[k] += right[k] * (VrJediHand ? -18.0f : 18.0f) - up[k] * 12.0f + fwd[k] * 32.0f;
        forceHandW[k] += right[k] * (VrJediHand ? 18.0f : -18.0f) - up[k] * 12.0f + fwd[k] * 32.0f;
    }

    if (g_gevrStereo) {
        const int bCtrl = VrJediHand ? 0 : 1;
        const int fCtrl = 1 - bCtrl;
        const int bDown = get_analog_value(bCtrl, "trigger") > 0.5f;
        const int fDown = get_analog_value(fCtrl, "trigger") > 0.5f;
        if (s_vrSlashCd[0] > 0.0f) s_vrSlashCd[0] -= dt;
        if (s_vrSlashCd[1] > 0.0f) s_vrSlashCd[1] -= dt;
        if (s_vrPowerCd > 0.0f) s_vrPowerCd -= dt;
        if (s_vrThrowGuard > 0.0f) s_vrThrowGuard -= dt;

        bladeBlock = bDown;
        bVelCm[0] = vr_ctrl_velocity_play[bCtrl][0] * 100.0f;
        bVelCm[1] = vr_ctrl_velocity_play[bCtrl][1] * 100.0f;
        bVelCm[2] = vr_ctrl_velocity_play[bCtrl][2] * 100.0f;
        fVelCm[0] = vr_ctrl_velocity_play[fCtrl][0] * 100.0f;
        fVelCm[1] = vr_ctrl_velocity_play[fCtrl][1] * 100.0f;
        fVelCm[2] = vr_ctrl_velocity_play[fCtrl][2] * 100.0f;

        if (!bDown && s_vrTrigWas[bCtrl] && gevrJnVrWristThrow(bVelCm, fwd, 1)) {
            bladeThrow = 1;
        }
        const float bSpeed = sqrtf(jnDot3(bVelCm, bVelCm));
        const float fSpeed = sqrtf(jnDot3(fVelCm, fVelCm));
        vrSwing01 = fminf(1.0f, fmaxf(bSpeed, fSpeed) / 220.0f);
        if (!bDown && bSpeed >= 135.0f && s_vrSlashCd[bCtrl] <= 0.0f) {
            bladeTap = 1;
            s_vrSlashCd[bCtrl] = 0.28f;
        }
        const int dualOffhand = gevrJnIsDualSword(VrJediNinjaMode, jnEffectiveNinjaSword())
            || (VrJediNinjaMode == GEVR_JN_JEDI && VrSaberStyle == GEVR_JN_STYLE_DUAL);
        if (dualOffhand && !fDown && fSpeed >= 135.0f && s_vrSlashCd[fCtrl] <= 0.0f) {
            gevrJnSlashStart(&s_offSlash, 0);
            jnPerformMeleeSweep(eye, fwd, gevrJnWeaponReachCm(VrJediNinjaMode, jnEffectiveNinjaSword()), 12.0f);
            jnSfx(VrJediNinjaMode == GEVR_JN_JEDI ? GEVR_JN_SFX_SLASH_SABER : GEVR_JN_SFX_SLASH_STEEL, 0.9f);
            s_vrSlashCd[fCtrl] = 0.28f;
        }

        offHold = fDown;
        if (!fDown && s_vrTrigWas[fCtrl]) {
            offRelease = 1;
            if (gevrJnVrWristThrow(fVelCm, fwd, 1)) offDouble = 1;
        }
        if (fDown && s_vrPowerCd <= 0.0f && gevrJnVrWristPull(fVelCm, fwd, 1)) {
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
                jnHaptic(VrJediHand ? 0 : 1, 0.65f, 0.04f);
            }
            const float pushIn = g_gevrStereo ? jnDot3(bVelCm, fwd) : 0.0f;
            if (bladeTap || pushIn > 150.0f) {
                float dir[3] = { s_saberLockChr->prop->pos.x - eye[0], 0.0f, s_saberLockChr->prop->pos.z - eye[2] };
                jnNorm3(dir, dir);
                jnHitGuardWithBlade(s_saberLockChr, dir, 18.0f, 0);
                JnSpecialNpc *sp = jnFindSpecial(s_saberLockChr);
                if (sp) sp->meleeCd = 1.6f;
                jnSfx(GEVR_JN_SFX_CLASH_SABER, 1.5f);
                jnHaptic(VrJediHand ? 0 : 1, 1.0f, 0.12f);
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

    /* Melee swing (cannot attack while blocking or while weapon is thrown) */
    const int sword = jnEffectiveNinjaSword();
    if (bladeTap && !s_blocking && !s_thrown.active
        && !(VrJediNinjaMode == GEVR_JN_NINJA && gevrJnSelectedNinjaSlot == GEVR_JN_NINJA_SLOT_SHADOW)
        && !(VrJediNinjaMode == GEVR_JN_NINJA && gevrJnSelectedNinjaSlot == GEVR_JN_NINJA_SLOT_BOOMERANG)) {
        if (gevrJnSlashStart(&s_slash, s_blocking)) {
            const float reachMul = (VrJediNinjaMode == GEVR_JN_JEDI && VrSaberStyle == GEVR_JN_STYLE_DOUBLE) ? 1.12f : 1.0f;
            jnPerformMeleeSweep(eye, fwd, gevrJnWeaponReachCm(VrJediNinjaMode, sword) * reachMul, 14.0f);
            jnSfx(VrJediNinjaMode == GEVR_JN_JEDI ? GEVR_JN_SFX_SLASH_SABER : GEVR_JN_SFX_SLASH_STEEL, 1.0f);
            if (!g_gevrStereo && VrJediNinjaMode == GEVR_JN_JEDI && VrSaberStyle == GEVR_JN_STYLE_DUAL && !s_offSlash.active) {
                gevrJnSlashStart(&s_offSlash, 0);
            }
        }
    }
    gevrJnSlashStep(&s_slash, dt);
    gevrJnSlashStep(&s_offSlash, dt);

    /* Wall / floor plasma scorch sparks while saber blade scrapes geometry */
    if (VrJediNinjaMode == GEVR_JN_JEDI && VrJnBladeTrails && !s_thrown.active
        && s_fpIgnite.extend > 0.45f && pdphysIsReady()) {
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
        float hitPos[3];
        if (pdphysRaycastAnyRoom(bladeHandW, tipW, hitPos, NULL, NULL) && s_wallSparkCd <= 0.0f) {
            jnAddSpark(hitPos, 0xFF9838FFu, 15.0f, 0.14f);
            jnHaptic(VrJediHand ? 0 : 1, 0.25f, 0.02f);
            s_wallSparkCd = 0.055f;
        }
    }

    /* Throw Lightsaber / Ninja Boomerang */
    const int wantThrow = bladeThrow || (VrJediNinjaMode == GEVR_JN_JEDI && gevrJnSelectedPower == GEVR_JN_FORCE_THROW && offDouble)
        || (VrJediNinjaMode == GEVR_JN_NINJA && gevrJnSelectedNinjaSlot == GEVR_JN_NINJA_SLOT_BOOMERANG && bladeTap);
    if (wantThrow && !s_thrown.active && !s_blocking) {
        const int isBoom = (VrJediNinjaMode == GEVR_JN_NINJA);
        gevrJnThrownLaunch(&s_thrown, isBoom, bladeHandW, fwd);
        s_thrownBounces = 0;
        s_throwHitCount = 0;
        jnTrailClear();
        jnSfx(isBoom ? GEVR_JN_SFX_SLASH_STEEL : GEVR_JN_SFX_SLASH_SABER, 1.2f);
        jnHaptic(VrJediHand ? 0 : 1, 0.7f, 0.08f);
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
            jnHitGuardWithBlade(c, dir, 18.0f, 2);
        }
    }

    /* Force Lightning vs Telekinesis (Force Grab/Choke, Pull, Push, Limb Ball) */
    const int allowForce = (VrJediNinjaMode == GEVR_JN_JEDI && VrJediForcePowers != 0)
        || (VrJediNinjaMode == GEVR_JN_NINJA && !jnHasNinjaShield());
    const int useLightning = (VrJediNinjaMode == GEVR_JN_JEDI && gevrJnSelectedPower == GEVR_JN_FORCE_LIGHTNING);
    s_lightningActive = 0;
    s_lightningCount = 0;

    if (!allowForce) {
        s_chokeActive = 0;
        s_chokeChr = NULL;
        s_grabBodySlot = -1;
        s_caughtProjIdx = -1;
    } else if (useLightning && offHold) {
        s_lightningActive = 1;
        s_lightningTickAccum += dt;
        const int doDmg = (s_lightningTickAccum >= 0.12f);
        if (doDmg) s_lightningTickAccum = 0.0f;

        const s32 n = get_numguards();
        for (s32 i = 0; i < n && s_lightningCount < JN_MAX_LIGHTNING; i++) {
            ChrRecord *c = &g_ChrSlots[i];
            if (!jnIsEnemyGuard(c)) continue;
            const float dx = c->prop->pos.x - eye[0];
            const float dy = (c->prop->pos.y + 100.0f) - eye[1];
            const float dz = c->prop->pos.z - eye[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > 900.0f * 900.0f || d2 < 1.0f) continue;
            const float d = sqrtf(d2);
            const float dir[3] = { dx / d, dy / d, dz / d };
            if (jnDot3(fwd, dir) < 0.65f) continue;
            s_lightningChrs[s_lightningCount] = c;
            s_lightningChrnums[s_lightningCount] = c->chrnum;
            s_lightningCount++;
            if (doDmg) {
                jnHitGuardWithBlade(c, dir, 2.2f, 0);
            }
        }
    } else if (allowForce) {
        float chokeTarget[3];
        gevrJnChokeTargetPos(forceHandW, fwd, chokeTarget);
        float ballCenter[3] = {
            eye[0] + fwd[0] * 115.0f,
            eye[1] + fwd[1] * 115.0f - 10.0f,
            eye[2] + fwd[2] * 115.0f
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
                        jnHitGuardWithBlade(s_chokeChr, slamDir, 45.0f, 0);
                        int bslot = gevrRagdollFindNearestBody(s_thrownBodyStart, slamDir, 260.0f, -1.0f, NULL);
                        if (bslot >= 0) gevrRagdollThrowBody(bslot, slamVel);
                        jnAddSpark(s_thrownBodyStart, 0x88C0FFFFu, 32.0f, 0.25f);
                        jnSfx(GEVR_JN_SFX_FORCE_PUSH, 1.35f);
                        jnHaptic(VrJediHand ? 1 : 0, 1.0f, 0.12f);
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
                    pr->speed.x = (forceHandW[0] - pr->obj->prop->pos.x) * 0.14f;
                    pr->speed.y = (forceHandW[1] - pr->obj->prop->pos.y) * 0.14f;
                    pr->speed.z = (forceHandW[2] - pr->obj->prop->pos.z) * 0.14f;
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
                        const float dx = pr->obj->prop->pos.x - eye[0];
                        const float dy = pr->obj->prop->pos.y - eye[1];
                        const float dz = pr->obj->prop->pos.z - eye[2];
                        const float d2 = dx * dx + dy * dy + dz * dz;
                        if (d2 >= bestD2 || d2 < 4.0f) continue;
                        const float d = sqrtf(d2);
                        if ((dx * fwd[0] + dy * fwd[1] + dz * fwd[2]) / d < 0.72f) continue;
                        bestD2 = d2;
                        bestIdx = pi;
                    }
                    if (bestIdx >= 0) {
                        s_caughtProjIdx = bestIdx;
                        caughtProj = 1;
                        jnHaptic(VrJediHand ? 1 : 0, 0.5f, 0.05f);
                    }
                }
                ChrRecord *closeNpc = caughtProj ? NULL : jnFindForceTarget(eye, fwd, GEVR_JN_GRAB_RANGE_CM, 0.72f);
                if (caughtProj) {
                    /* held by off-hand */
                } else if (closeNpc) {
                    s_chokeChr = closeNpc;
                    s_chokeChrnum = closeNpc->chrnum;
                    s_chokeActive = 1;
                    s_chokeHoldSec = 0.0f;
                    jnHaptic(VrJediHand ? 1 : 0, 0.5f, 0.06f);
                } else {
                    float bodyPos[3];
                    int bslot = gevrRagdollFindNearestBody(eye, fwd, 900.0f, 0.75f, bodyPos);
                    if (bslot >= 0) {
                        s_grabBodySlot = bslot;
                        gevrRagdollPullBody(bslot, chokeTarget, 1600.0f, 7.5f);
                    } else if (VrJediNinjaMode == GEVR_JN_JEDI) {
                        int pulled = gevrRagdollPullLooseLimbs(eye, ballCenter, GEVR_JN_PULL_RANGE_CM, s_time, GEVR_JN_MAX_LIMBS);
                        if (pulled > 0) {
                            s_limbBallSuspended = 1;
                            memcpy(s_limbBallPos, ballCenter, sizeof(s_limbBallPos));
                        }
                        ChrRecord *farNpc = jnFindForceTarget(eye, fwd, GEVR_JN_PULL_RANGE_CM, 0.84f);
                        if (farNpc) {
                            float p[3] = { farNpc->prop->pos.x, farNpc->prop->pos.y, farNpc->prop->pos.z };
                            const float toEye[3] = { eye[0] - p[0], eye[1] - 100.0f - p[1], eye[2] - p[2] };
                            float nd[3];
                            const float d = jnNorm3(toEye, nd);
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
                float launchDir[3] = { fwd[0], fwd[1], fwd[2] };
                if (g_gevrStereo && sqrtf(jnDot3(fVelCm, fVelCm)) > 240.0f) {
                    jnNorm3(fVelCm, launchDir);
                }
                const float throwVel[3] = { launchDir[0] * 1600.0f, launchDir[1] * 1600.0f + 160.0f, launchDir[2] * 1600.0f };
                memcpy(s_thrownBodyStart, s_chokeChr->prop->pos.f, sizeof(s_thrownBodyStart));
                jnHitGuardWithBlade(s_chokeChr, launchDir, 40.0f, 0);
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
                jnHaptic(VrJediHand ? 1 : 0, 0.9f, 0.10f);
            } else if (s_grabBodySlot >= 0) {
                const float throwVel[3] = { fwd[0] * 1600.0f, fwd[1] * 1600.0f + 180.0f, fwd[2] * 1600.0f };
                gevrRagdollGetBodyPos(s_grabBodySlot, s_thrownBodyStart);
                gevrRagdollThrowBody(s_grabBodySlot, throwVel);
                s_thrownBodySlot = s_grabBodySlot;
                s_thrownBodyAge = 0.0f;
                s_thrownBodyHitCount = 0;
                gevrJnThrownBodyInit(&s_thrownBody, 80.0f, 1600.0f);
                jnSfx(GEVR_JN_SFX_FORCE_PUSH, 0.95f);
                s_grabBodySlot = -1;
                jnHaptic(VrJediHand ? 1 : 0, 0.9f, 0.10f);
            } else if (s_caughtProjIdx >= 0) {
                if (jnProjValidAirborne(s_caughtProjIdx) && g_CurrentPlayer && g_CurrentPlayer->prop) {
                    Projectile *pr = &g_Projectiles[s_caughtProjIdx];
                    pr->speed.x = fwd[0] * 28.0f;
                    pr->speed.y = fwd[1] * 28.0f;
                    pr->speed.z = fwd[2] * 28.0f;
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
            int kept = gevrRagdollPullLooseLimbs(eye, ballCenter, 360.0f, s_time, GEVR_JN_MAX_LIMBS);
            if (kept <= 0 && !offHold) {
                s_limbBallSuspended = 0;
            } else {
                memcpy(s_limbBallPos, ballCenter, sizeof(s_limbBallPos));
            }
        }

        if (offDouble && VrJediNinjaMode == GEVR_JN_JEDI) {
            if (s_limbBallSuspended) {
                gevrRagdollLaunchLooseLimbs(s_limbBallPos, fwd, 1800.0f, 190.0f);
                const s32 n = get_numguards();
                for (s32 i = 0; i < n; i++) {
                    ChrRecord *c = &g_ChrSlots[i];
                    if (!jnIsEnemyGuard(c)) continue;
                    const float dx = c->prop->pos.x - eye[0];
                    const float dy = (c->prop->pos.y + 100.0f) - eye[1];
                    const float dz = c->prop->pos.z - eye[2];
                    const float d = sqrtf(dx * dx + dy * dy + dz * dz);
                    if (d < 1.0f || d > 950.0f) continue;
                    const float dir[3] = { dx / d, dy / d, dz / d };
                    if (jnDot3(fwd, dir) < 0.65f) continue;
                    const float dmg = gevrJnLimbBlastDamage(d);
                    if (dmg >= 80.0f) {
                        jnHitGuardWithBlade(c, dir, 24.0f, 1);
                    } else if (dmg > 0.0f) {
                        jnHitGuardWithBlade(c, dir, dmg * 0.15f, 0);
                    }
                }
                s_limbBallSuspended = 0;
                jnHaptic(VrJediHand ? 1 : 0, 1.0f, 0.12f);
            } else {
                jnTriggerForcePush(eye, fwd);
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
                    jnHitGuardWithBlade(c, fwd, 40.0f, 3);
                } else if (impact == GEVR_JN_IMPACT_RAGDOLL_KILL) {
                    jnHitGuardWithBlade(c, fwd, 25.0f, 0);
                } else if (impact == GEVR_JN_IMPACT_KNOCKDOWN) {
                    jnHitGuardWithBlade(c, fwd, 5.0f, 0);
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

    /* Ninja bullet-slice mid-swing */
    if (VrJediNinjaMode == GEVR_JN_NINJA && VrNinjaBulletSlice && (s_slash.active || s_offSlash.active)) {
        const float cutPt[3] = {
            eye[0] + fwd[0] * 55.0f,
            eye[1] + fwd[1] * 55.0f,
            eye[2] + fwd[2] * 55.0f
        };
        jnAddSpark(cutPt, 0xF0F8FFFFu, 26.0f, 0.18f);
        jnSfx(GEVR_JN_SFX_CLASH_METAL, 1.15f);
        jnHaptic(VrJediHand ? 0 : 1, 0.7f, 0.06f);
        return 1;
    }

    float toShooter[3] = { vecX, 0.0f, vecZ };
    /* If caller passed zero vector, look for nearest threatening guard in front */
    if (toShooter[0] * toShooter[0] + toShooter[2] * toShooter[2] < 1e-4f) {
        toShooter[0] = fwd[0];
        toShooter[1] = fwd[1];
        toShooter[2] = fwd[2];
    }
    float reflectDir[3] = { 0.0f, 0.0f, 0.0f };
    const int res = gevrJnCheckBlock(VrJediNinjaMode, s_blocking || s_ninjaShieldHeld,
                                     s_ninjaShieldHeld, fwd, toShooter, reflectDir);
    if (res == GEVR_JN_BLOCK_NONE) return 0;

    const int perfect = (VrJnPerfectParry != 0 && s_blockHoldSec <= 0.22f && res == GEVR_JN_BLOCK_DEFLECT);
    float bladePt[3] = {
        eye[0] + fwd[0] * 48.0f + right[0] * (s_ninjaShieldHeld ? -10.0f : 6.0f),
        eye[1] + fwd[1] * 48.0f - up[1] * 6.0f,
        eye[2] + fwd[2] * 48.0f + right[2] * (s_ninjaShieldHeld ? -10.0f : 6.0f)
    };
    unsigned int core = 0xFFFFFFFFu, glow = 0xFFD860FFu;
    if (VrJediNinjaMode == GEVR_JN_JEDI) {
        gevrJnSaberPalette(VrSaberColor, &core, &glow);
    }
    jnAddSpark(bladePt, perfect ? 0xFFF480FFu : (res == GEVR_JN_BLOCK_DEFLECT && VrJediNinjaMode == GEVR_JN_JEDI ? glow : 0xFFE860FFu),
               perfect ? 38.0f : (VrJediNinjaMode == GEVR_JN_JEDI ? 28.0f : 20.0f), 0.22f);
    jnSfx(res == GEVR_JN_BLOCK_DEFLECT ? GEVR_JN_SFX_DEFLECT : GEVR_JN_SFX_CLASH_METAL, perfect ? 1.45f : 1.0f);
    jnHaptic(VrJediHand ? 0 : 1, perfect ? 1.0f : 0.85f, perfect ? 0.10f : 0.07f);

    if (res == GEVR_JN_BLOCK_DEFLECT) {
        ChrRecord *shooter = g_gevrStereo ? jnFindForceTarget(eye, fwd, 2200.0f, 0.70f) : NULL;
        if (!shooter) shooter = jnFindForceTarget(eye, reflectDir, 2200.0f, 0.70f);
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
            jnHitGuardWithBlade(shooter, nd, perfect ? 38.0f : 16.0f, 0);
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
/* Fast3D Ultra-Performant 3D Rendering Helpers                               */
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

/* 4-sided tapered prism from origin along dir*lenMm (in mm local about originView) */
static Gfx *jnEmitPrism(Gfx *gdl, const float aMm[3], const float dir[3], float lenMm,
                        float r0Mm, float r1Mm, u32 rgbaBase, u32 rgbaTip)
{
    Vtx *v = dynAllocateVertices(8);
    if (!v) return gdl;
    float u[3], w[3];
    jnOrthoFrame(dir, u, w);
    const float bMm[3] = {
        aMm[0] + dir[0] * lenMm,
        aMm[1] + dir[1] * lenMm,
        aMm[2] + dir[2] * lenMm
    };
    const float ox[4] = { 1, 0, -1, 0 };
    const float oy[4] = { 0, 1, 0, -1 };
    for (int i = 0; i < 4; i++) {
        jnSetVtx(&v[i],
                 aMm[0] + (u[0] * ox[i] + w[0] * oy[i]) * r0Mm,
                 aMm[1] + (u[1] * ox[i] + w[1] * oy[i]) * r0Mm,
                 aMm[2] + (u[2] * ox[i] + w[2] * oy[i]) * r0Mm, rgbaBase);
        jnSetVtx(&v[4 + i],
                 bMm[0] + (u[0] * ox[i] + w[0] * oy[i]) * r1Mm,
                 bMm[1] + (u[1] * ox[i] + w[1] * oy[i]) * r1Mm,
                 bMm[2] + (u[2] * ox[i] + w[2] * oy[i]) * r1Mm, rgbaTip);
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), 8, 0);
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        gSP2Triangles(gdl++, i, j, 4 + j, 0, i, 4 + j, 4 + i, 0);
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
    gdl = jnEmitPrism(gdl, hiltStart, dirView, hiltMm, 17.0f, 15.0f, 0x222428FFu, 0x707882FFu);
    if (extend01 > 0.02f) {
        const float bladeMm = (lenCm * 10.0f - 240.0f) * extend01;
        gdl = jnEmitPrism(gdl, emitter, dirView, bladeMm, 11.0f, 6.0f, core, core);
        if (doubleEnded) {
            gdl = jnEmitPrism(gdl, backEmitter, negDir, bladeMm * 0.92f, 11.0f, 6.0f, core, core);
        }
        beg = jnBeginGeom(gdl, hiltView, scale, 1);
        if (beg) {
            gdl = beg;
            const float pulse = 1.0f + 0.08f * sinf(s_time * 38.0f);
            gdl = jnEmitPrism(gdl, emitter, dirView, bladeMm + 25.0f, 25.0f * pulse, 14.0f * pulse, glow, glow & 0xFFFFFF90u);
            if (doubleEnded) {
                gdl = jnEmitPrism(gdl, backEmitter, negDir, bladeMm * 0.92f + 25.0f,
                                  25.0f * pulse, 14.0f * pulse, glow, glow & 0xFFFFFF90u);
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

/* Axis-aligned in grip basis (r/u/f) solid 8-vtx box in mm */
static Gfx *jnEmitBox(Gfx *gdl, const float cMm[3], const float r[3], const float u[3], const float f[3],
                      float hr, float hu, float hf, u32 rgbaTop, u32 rgbaBot)
{
    Vtx *v = dynAllocateVertices(8);
    if (!v) return gdl;
    static const float s_corners[8][3] = {
        { -1, -1, -1 }, {  1, -1, -1 }, {  1,  1, -1 }, { -1,  1, -1 },
        { -1, -1,  1 }, {  1, -1,  1 }, {  1,  1,  1 }, { -1,  1,  1 }
    };
    static const int s_faces[6][4] = {
        { 0, 1, 2, 3 }, { 5, 4, 7, 6 }, { 4, 0, 3, 7 },
        { 1, 5, 6, 2 }, { 3, 2, 6, 7 }, { 4, 5, 1, 0 }
    };
    for (int i = 0; i < 8; i++) {
        const float sr = s_corners[i][0] * hr;
        const float su = s_corners[i][1] * hu;
        const float sf = s_corners[i][2] * hf;
        jnSetVtx(&v[i],
                 cMm[0] + r[0] * sr + u[0] * su + f[0] * sf,
                 cMm[1] + r[1] * sr + u[1] * su + f[1] * sf,
                 cMm[2] + r[2] * sr + u[2] * su + f[2] * sf,
                 s_corners[i][1] > 0.0f ? rgbaTop : rgbaBot);
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), 8, 0);
    for (int q = 0; q < 6; q++) {
        gSP2Triangles(gdl++, s_faces[q][0], s_faces[q][1], s_faces[q][2], 0,
                      s_faces[q][0], s_faces[q][2], s_faces[q][3], 0);
    }
    return gdl;
}

/* Han Solo's BlasTech DL-44 Heavy Blaster Pistol (Mauser C96 + offset scope + MG42 flash hider) */
static Gfx *jnDrawDl44BlasterView(Gfx *gdl, const float gripView[3], const float r[3], const float u[3],
                                  const float fwd[3], float scale, float scopeSide, int firingFlash)
{
    Gfx *beg = jnBeginGeom(gdl, gripView, scale, 0);
    if (!beg) return gdl;
    gdl = beg;

    /* 1. Walnut wood "broomhandle" Mauser grip + darker grooved bands + lanyard cap */
    float gDir[3] = { -0.22f * fwd[0] + 0.975f * u[0], -0.22f * fwd[1] + 0.975f * u[1], -0.22f * fwd[2] + 0.975f * u[2] };
    jnNorm3(gDir, gDir);
    const float gBot[3] = { -16.0f * fwd[0] - 88.0f * u[0], -16.0f * fwd[1] - 88.0f * u[1], -16.0f * fwd[2] - 88.0f * u[2] };
    gdl = jnEmitPrism(gdl, gBot, gDir, 96.0f, 16.5f, 13.5f, 0x4A2612FFu, 0x6A3A1EFFu);
    const float butt[3] = { -20.0f * fwd[0] - 92.0f * u[0], -20.0f * fwd[1] - 92.0f * u[1], -20.0f * fwd[2] - 92.0f * u[2] };
    gdl = jnEmitBox(gdl, butt, r, u, fwd, 11.0f, 5.0f, 12.0f, 0x98A2ACFFu, 0x626A74FFu);

    /* 2. Boxy Mauser C96 gunmetal receiver, top strap & exposed rear ring hammer */
    const float recC[3] = { 30.0f * fwd[0] + 32.0f * u[0], 30.0f * fwd[1] + 32.0f * u[1], 30.0f * fwd[2] + 32.0f * u[2] };
    gdl = jnEmitBox(gdl, recC, r, u, fwd, 13.5f, 18.0f, 44.0f, 0x3C424CFFu, 0x22262EFFu);
    const float topStrap[3] = { 36.0f * fwd[0] + 54.0f * u[0], 36.0f * fwd[1] + 54.0f * u[1], 36.0f * fwd[2] + 54.0f * u[2] };
    gdl = jnEmitBox(gdl, topStrap, r, u, fwd, 11.0f, 5.5f, 40.0f, 0x525A66FFu, 0x363C44FFu);
    const float hammer[3] = { -22.0f * fwd[0] + 52.0f * u[0], -22.0f * fwd[1] + 52.0f * u[1], -22.0f * fwd[2] + 52.0f * u[2] };
    gdl = jnEmitBox(gdl, hammer, r, u, fwd, 4.5f, 9.5f, 9.5f, 0x707884FFu, 0x404650FFu);

    /* 3. Forward box magazine well + trigger guard + brass side greeble */
    const float magWell[3] = { 76.0f * fwd[0] + 16.0f * u[0], 76.0f * fwd[1] + 16.0f * u[1], 76.0f * fwd[2] + 16.0f * u[2] };
    gdl = jnEmitBox(gdl, magWell, r, u, fwd, 12.0f, 20.0f, 22.0f, 0x30363EFFu, 0x1C2026FFu);
    const float tGuard[3] = { 32.0f * fwd[0] + 6.0f * u[0], 32.0f * fwd[1] + 6.0f * u[1], 32.0f * fwd[2] + 6.0f * u[2] };
    gdl = jnEmitBox(gdl, tGuard, r, u, fwd, 5.0f, 3.0f, 18.0f, 0x3A4048FFu, 0x262A30FFu);
    const float greeble[3] = { -14.5f * scopeSide * r[0] + 68.0f * fwd[0] + 24.0f * u[0],
                             -14.5f * scopeSide * r[1] + 68.0f * fwd[1] + 24.0f * u[1],
                             -14.5f * scopeSide * r[2] + 68.0f * fwd[2] + 24.0f * u[2] };
    gdl = jnEmitBox(gdl, greeble, r, u, fwd, 3.5f, 7.0f, 11.0f, 0xD2A048FFu, 0x8A6424FFu);

    /* 4. Tapered barrel + charcoal cooling/heat-sink fins */
    const float barS[3] = { 74.0f * fwd[0] + 44.0f * u[0], 74.0f * fwd[1] + 44.0f * u[1], 74.0f * fwd[2] + 44.0f * u[2] };
    gdl = jnEmitPrism(gdl, barS, fwd, 118.0f, 10.5f, 8.2f, 0x343A44FFu, 0x242830FFu);
    for (int f = 0; f < 3; f++) {
        const float ff = 104.0f + (float)f * 20.0f;
        const float finC[3] = { ff * fwd[0] + 44.0f * u[0], ff * fwd[1] + 44.0f * u[1], ff * fwd[2] + 44.0f * u[2] };
        gdl = jnEmitBox(gdl, finC, r, u, fwd, 12.5f, 13.0f, 4.0f, 0x282C34FFu, 0x181A20FFu);
    }

    /* 5. Flared MG42 slotted-cone flash hider muzzle */
    const float muzS[3] = { 186.0f * fwd[0] + 44.0f * u[0], 186.0f * fwd[1] + 44.0f * u[1], 186.0f * fwd[2] + 44.0f * u[2] };
    gdl = jnEmitPrism(gdl, muzS, fwd, 52.0f, 11.5f, 18.0f, 0x20242CFFu, 0x16181EFFu);
    if (firingFlash) {
        const float tipS[3] = { 234.0f * fwd[0] + 44.0f * u[0], 234.0f * fwd[1] + 44.0f * u[1], 234.0f * fwd[2] + 44.0f * u[2] };
        gdl = jnEmitPrism(gdl, tipS, fwd, 26.0f, 12.0f, 3.0f, 0xFF6028FFu, 0xFFF088FFu);
    }

    /* 6. Offset brushed-aluminum mounts + high right-side Hensoldt-Wetzlar sniper scope */
    const float sOff = 24.0f * scopeSide;
    const float mnt1[3] = { sOff * 0.55f * r[0] + 18.0f * fwd[0] + 64.0f * u[0],
                          sOff * 0.55f * r[1] + 18.0f * fwd[1] + 64.0f * u[1],
                          sOff * 0.55f * r[2] + 18.0f * fwd[2] + 64.0f * u[2] };
    const float mnt2[3] = { sOff * 0.55f * r[0] + 72.0f * fwd[0] + 64.0f * u[0],
                          sOff * 0.55f * r[1] + 72.0f * fwd[1] + 64.0f * u[1],
                          sOff * 0.55f * r[2] + 72.0f * fwd[2] + 64.0f * u[2] };
    gdl = jnEmitBox(gdl, mnt1, r, u, fwd, 11.0f, 11.0f, 4.5f, 0xC8D0DCFFu, 0x808894FFu);
    gdl = jnEmitBox(gdl, mnt2, r, u, fwd, 11.0f, 11.0f, 4.5f, 0xC8D0DCFFu, 0x808894FFu);
    const float scRear[3] = { sOff * r[0] - 10.0f * fwd[0] + 76.0f * u[0],
                            sOff * r[1] - 10.0f * fwd[1] + 76.0f * u[1],
                            sOff * r[2] - 10.0f * fwd[2] + 76.0f * u[2] };
    const float scBody[3] = { sOff * r[0] + 14.0f * fwd[0] + 76.0f * u[0],
                            sOff * r[1] + 14.0f * fwd[1] + 76.0f * u[1],
                            sOff * r[2] + 14.0f * fwd[2] + 76.0f * u[2] };
    const float scFront[3] = { sOff * r[0] + 84.0f * fwd[0] + 76.0f * u[0],
                             sOff * r[1] + 84.0f * fwd[1] + 76.0f * u[1],
                             sOff * r[2] + 84.0f * fwd[2] + 76.0f * u[2] };
    gdl = jnEmitPrism(gdl, scRear, fwd, 26.0f, 11.5f, 8.5f, 0x1C2028FFu, 0x2C323CFFu);
    gdl = jnEmitPrism(gdl, scBody, fwd, 72.0f, 8.5f, 8.5f, 0x282E38FFu, 0x282E38FFu);
    gdl = jnEmitPrism(gdl, scFront, fwd, 28.0f, 8.5f, 12.5f, 0x282E38FFu, 0x1A2028FFu);
    const float scLens[3] = { sOff * r[0] - 11.0f * fwd[0] + 76.0f * u[0],
                            sOff * r[1] - 11.0f * fwd[1] + 76.0f * u[1],
                            sOff * r[2] - 11.0f * fwd[2] + 76.0f * u[2] };
    gdl = jnEmitBox(gdl, scLens, r, u, fwd, 7.5f, 7.5f, 2.0f, 0x2868A8FFu, 0x103060FFu);
    const float scKnob[3] = { sOff * r[0] + 44.0f * fwd[0] + 88.0f * u[0],
                            sOff * r[1] + 44.0f * fwd[1] + 88.0f * u[1],
                            sOff * r[2] + 44.0f * fwd[2] + 88.0f * u[2] };
    gdl = jnEmitBox(gdl, scKnob, r, u, fwd, 4.5f, 4.5f, 4.5f, 0xB8C0CCFFu, 0x707884FFu);
    return gdl;
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
    gdl = jnEmitPrism(gdl, zero, dirView, hiltMm, 15.0f, 14.0f, 0x1A1A20FFu, 0x2C2E34FFu);
    const float tsuba[3] = { dirView[0] * hiltMm, dirView[1] * hiltMm, dirView[2] * hiltMm };
    gdl = jnEmitPrism(gdl, tsuba, dirView, 18.0f, 36.0f, 34.0f, isGold ? 0xFFD230FFu : 0xB89838FFu, 0x705820FFu);
    const float bladeBase[3] = { dirView[0] * (hiltMm + 18.0f), dirView[1] * (hiltMm + 18.0f), dirView[2] * (hiltMm + 18.0f) };
    const u32 c0 = isGold ? 0xFFE252FFu : 0xDCE4ECFFu;
    const u32 c1 = isGold ? 0xFFF4A8FFu : 0xF8FCFFFFu;
    return jnEmitPrism(gdl, bladeBase, dirView, bladeMm, 14.0f, 3.0f, c0, c1);
}

static Gfx *jnDrawShieldView(Gfx *gdl, const float centerView[3], const float rightView[3],
                             const float upView[3], float scale)
{
    Gfx *beg = jnBeginGeom(gdl, centerView, scale, 0);
    if (!beg) return gdl;
    gdl = beg;
    Vtx *v = dynAllocateVertices(17);
    if (!v) return gdl;
    jnSetVtx(&v[0], 0, 0, 0, 0x20242CFFu);
    for (int i = 0; i < 8; i++) {
        const float a = (float)i * (6.2831853f / 8.0f);
        const float ca = cosf(a), sa = sinf(a);
        jnSetVtx(&v[1 + i], (rightView[0] * ca + upView[0] * sa) * 145.0f,
                 (rightView[1] * ca + upView[1] * sa) * 145.0f,
                 (rightView[2] * ca + upView[2] * sa) * 145.0f, 0x3A424EFFu);
        jnSetVtx(&v[9 + i], (rightView[0] * ca + upView[0] * sa) * 180.0f,
                 (rightView[1] * ca + upView[1] * sa) * 180.0f,
                 (rightView[2] * ca + upView[2] * sa) * 180.0f, 0xE6B830FFu);
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
    const u32 cCenter = isGold ? 0x16181EFFu : 0x303640FFu;
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

static Gfx *jnDrawTrailView(Gfx *gdl, const JnBladeTrail *tr, unsigned int rgba, float scale)
{
    if (!tr || tr->count < 2) return gdl;
    const float *orig = tr->hilt[0];
    Gfx *beg = jnBeginGeom(gdl, orig, scale, 1);
    if (!beg) return gdl;
    gdl = beg;
    Vtx *v = dynAllocateVertices(tr->count * 2);
    if (!v) return gdl;
    const float invS = 10.0f / scale;
    const unsigned int rgb = rgba & 0xFFFFFF00u;
    const unsigned int maxA = rgba & 0xFFu;
    for (int i = 0; i < tr->count; i++) {
        const float fade = 1.0f - (float)i / (float)tr->count;
        const unsigned int aTip = (unsigned int)((float)maxA * fade * 0.72f);
        const unsigned int aBase = (unsigned int)((float)maxA * fade * 0.16f);
        jnSetVtx(&v[i * 2 + 0],
                 (tr->hilt[i][0] - orig[0]) * invS,
                 (tr->hilt[i][1] - orig[1]) * invS,
                 (tr->hilt[i][2] - orig[2]) * invS, rgb | aBase);
        jnSetVtx(&v[i * 2 + 1],
                 (tr->tip[i][0] - orig[0]) * invS,
                 (tr->tip[i][1] - orig[1]) * invS,
                 (tr->tip[i][2] - orig[2]) * invS, rgb | aTip);
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

static void jnWorldToView(const Mtxf *w2s, const float w[3], float s, float out[3])
{
    coord3d c;
    c.x = w[0]; c.y = w[1]; c.z = w[2];
    mtx4TransformVecInPlace((Mtxf *)w2s, &c);
    out[0] = c.x * s; out[1] = c.y * s; out[2] = c.z * s;
}

/* -------------------------------------------------------------------------- */
/* World Render (Thrown Weapons, Sith/Ninja/Ally Blades, VFX)                 */
/* -------------------------------------------------------------------------- */

Gfx *gevrJnDrawWorld(Gfx *gdl)
{
    if (!g_CurrentPlayer || !jnInGameplay()) return gdl;
    const Mtxf *w2s = camGetWorldToScreenMtxf();
    if (!w2s) return gdl;
    const float s = g_gevrStereo && D_800364CC > 1e-6f ? D_800364CC : 1.0f;
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
        const float hiltW[3] = { c->prop->pos.x, c->prop->pos.y + 108.0f * sp->scale, c->prop->pos.z };
        float hv[3];
        jnWorldToView(w2s, hiltW, s, hv);
        if (hv[2] >= -10.0f) continue;

        float tipWorld[3] = {
            hiltW[0] + sinf(c->chrwidth + s_time * 3.0f) * 35.0f,
            hiltW[1] + 55.0f * sp->scale,
            hiltW[2] + cosf(c->chrwidth + s_time * 3.0f) * 35.0f
        };
        float tv[3];
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
    if (gevrJnDrawBlasterActive()) {
        const float s = g_gevrStereo && D_800364CC > 1e-6f ? D_800364CC : 1.0f;
        const float scopeSide = VrLeftHandedMode ? -1.0f : 1.0f;
        const int firingFlash = (g_CurrentPlayer->hands[GUNRIGHT].weapon_beam.unk00 >= 0
                                 && g_CurrentPlayer->hands[GUNRIGHT].weapon_beam.unk00 <= 2) ? 1 : 0;
        float base[3], r[3], u[3], fwd[3];
        int haveVr = 0;

        gDPPipeSync(gdl++);
        gSPMatrix(gdl++, osVirtualToPhysical((void *)currentPlayerGetProjectionMatrix()), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
        gdl = combiner_bayer_lod_perspective(gdl);
        gdl = sub_GAME_7F061E18(gdl, &g_CurrentPlayer->hands[GUNRIGHT].weapon_beam, 0);

        if (g_gevrStereo) {
            float b[3];
            if (gevrJnGetGripAxes(1, base, r, u, b)) {
                fwd[0] = -b[0]; fwd[1] = -b[1]; fwd[2] = -b[2];
                jnNorm3(fwd, fwd);
                jnNorm3(r, r);
                jnCross3(r, fwd, u);
                jnNorm3(u, u);
                haveVr = 1;
            }
        }
        if (!haveVr) {
            base[0] = 14.5f * scopeSide * s;
            base[1] = -12.5f * s;
            base[2] = -28.0f * s;
            fwd[0] = -0.10f * scopeSide; fwd[1] = 0.04f; fwd[2] = -0.994f;
            jnNorm3(fwd, fwd);
            r[0] = 0.994f; r[1] = 0.0f; r[2] = -0.10f * scopeSide;
            jnNorm3(r, r);
            jnCross3(r, fwd, u);
            jnNorm3(u, u);
        }
        if (s_blasterRecoil > 0.001f) {
            const float kick = s_blasterRecoil * 3.0f * s;
            base[0] -= fwd[0] * kick;
            base[1] -= fwd[1] * kick;
            base[2] -= fwd[2] * kick;
            fwd[0] += u[0] * (s_blasterRecoil * 0.14f);
            fwd[1] += u[1] * (s_blasterRecoil * 0.14f);
            fwd[2] += u[2] * (s_blasterRecoil * 0.14f);
            jnNorm3(fwd, fwd);
            jnCross3(r, fwd, u);
            jnNorm3(u, u);
        }
        gdl = jnDrawDl44BlasterView(gdl, base, r, u, fwd, s, scopeSide, firingFlash);
        gDPPipeSync(gdl++);
        gSPTexture(gdl++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
        gdl = combiner_bayer_lod_perspective(gdl);
        return gdl;
    }

    if (!jnModeActive() || jnUsingGunsOverride() || !jnInGameplay()) return gdl;
    const Mtxf *w2s = camGetWorldToScreenMtxf();
    const float s = g_gevrStereo && D_800364CC > 1e-6f ? D_800364CC : 1.0f;
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
            if (s_fpIgnite.extend > 0.25f) {
                const float reachView = (91.0f * s_fpIgnite.extend + 24.0f) * s;
                const float emitV[3] = {
                    hilt[0] + tipDir[0] * (dbl ? 16.0f : 24.0f) * s,
                    hilt[1] + tipDir[1] * (dbl ? 16.0f : 24.0f) * s,
                    hilt[2] + tipDir[2] * (dbl ? 16.0f : 24.0f) * s
                };
                const float tipV[3] = {
                    hilt[0] + tipDir[0] * reachView,
                    hilt[1] + tipDir[1] * reachView,
                    hilt[2] + tipDir[2] * reachView
                };
                jnTrailPush(0, emitV, tipV, s);
                if (dbl) {
                    const float emitB[3] = {
                        hilt[0] - tipDir[0] * 16.0f * s,
                        hilt[1] - tipDir[1] * 16.0f * s,
                        hilt[2] - tipDir[2] * 16.0f * s
                    };
                    const float tipB[3] = {
                        hilt[0] - tipDir[0] * (reachView * 0.92f),
                        hilt[1] - tipDir[1] * (reachView * 0.92f),
                        hilt[2] - tipDir[2] * (reachView * 0.92f)
                    };
                    jnTrailPush(1, emitB, tipB, s);
                }
            }
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
                if (s_fpIgnite.extend > 0.25f) {
                    const float oReach = (81.0f * s_fpIgnite.extend + 24.0f) * s;
                    const float oEmit[3] = { ohilt[0] + odir[0] * 24.0f * s, ohilt[1] + odir[1] * 24.0f * s, ohilt[2] + odir[2] * 24.0f * s };
                    const float oTip[3] = { ohilt[0] + odir[0] * oReach, ohilt[1] + odir[1] * oReach, ohilt[2] + odir[2] * oReach };
                    jnTrailPush(2, oEmit, oTip, s);
                }
            }
            if (VrJnBladeTrails) {
                gdl = jnDrawTrailView(gdl, &s_trails[0], glow, s);
                if (dbl) gdl = jnDrawTrailView(gdl, &s_trails[1], glow, s);
                if (VrSaberStyle == GEVR_JN_STYLE_DUAL) gdl = jnDrawTrailView(gdl, &s_trails[2], glow, s);
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
                const float emitV[3] = { hilt[0] + tipDir[0] * 24.0f * s, hilt[1] + tipDir[1] * 24.0f * s, hilt[2] + tipDir[2] * 24.0f * s };
                const float tipV[3] = { hilt[0] + tipDir[0] * reachCm * s, hilt[1] + tipDir[1] * reachCm * s, hilt[2] + tipDir[2] * reachCm * s };
                jnTrailPush(0, emitV, tipV, s);
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
                const float oEmit[3] = { ohilt[0] + odir[0] * 24.0f * s, ohilt[1] + odir[1] * 24.0f * s, ohilt[2] + odir[2] * 24.0f * s };
                const float oTip[3] = { ohilt[0] + odir[0] * 70.0f * s, ohilt[1] + odir[1] * 70.0f * s, ohilt[2] + odir[2] * 70.0f * s };
                jnTrailPush(2, oEmit, oTip, s);
            } else if (jnHasNinjaShield()) {
                float sc[3] = {
                    haveVrOff ? offHilt[0] : (s_ninjaShieldHeld ? -4.0f * sideSign * s : -18.0f * sideSign * s),
                    haveVrOff ? offHilt[1] : (s_ninjaShieldHeld ? -2.0f * s : -11.0f * s),
                    haveVrOff ? offHilt[2] : (s_ninjaShieldHeld ? -36.0f * s : -30.0f * s)
                };
                gdl = jnDrawShieldView(gdl, sc, offRight, offUp, s);
            }
            if (VrJnBladeTrails) {
                gdl = jnDrawTrailView(gdl, &s_trails[0], nTrailRgba, s);
                if (gevrJnIsDualSword(GEVR_JN_NINJA, sw)) gdl = jnDrawTrailView(gdl, &s_trails[2], nTrailRgba, s);
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
