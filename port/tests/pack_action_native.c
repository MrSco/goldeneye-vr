/* A pack gun's moving parts against the game's own code (test_pack_action.py
 * pastes it in): GoldenEye's slide (gun.c sub_GAME_7F05E808 and
 * sub_GAME_7F05E83C) beside gevr_modaction.h on the same shots and ticks, then
 * gun.c's per-hand action and the offsets it gives a pack body's groups. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "gevr_modaction.h"

typedef int s32;
typedef unsigned char u8;
typedef float f32;
typedef int GUNHAND;
typedef union { float f[3]; } coord3d;
typedef struct { float m[4][4]; } Mtxf;
typedef struct { int unused; } GevrModMesh;
enum { FALSE, TRUE, ITEM_UNARMED, ITEM_TEST, ITEM_OTHER, ITEM_IDS_MAX, MAX_PLAYER_COUNT = 4 };
enum { GEVR_MODPART_ACTION, GEVR_MODPART_HANDLE, GEVR_MODPART_GROUPS };
enum { GEVR_MODHAND_READY, GEVR_MODHAND_TRIGGER, GEVR_MODHAND_SUPPORT, GEVR_MODHAND_POSES };
typedef struct { unsigned pictures[GEVR_MODPART_GROUPS]; float travel[GEVR_MODPART_GROUPS][3]; int holdOpen;
                 GevrModActionTiming timing; } GevrModGunParts;
typedef struct { f32 BoltRecoilBack; } WeaponStats;
struct hand { f32 field_A88; s32 field_A8C; s32 weapon_ammo_in_magazine; };
static struct TestPlayer { int hand_item[2]; struct hand hands[2]; } player;
static void *owner = &player;
static struct TestPlayer *g_CurrentPlayer = &player;
static f32 g_GlobalTimerDelta = 1.0f;
static WeaponStats s_stats = { 7.0f };   /* a PP7-like stroke for GoldenEye's own slide */
static f32 s_rack[2];
static int netIsActive(void) { return FALSE; }
static int get_cur_playernum(void) { return 0; }
static int netGetLocalSlot(void) { return 0; }
static void gevrModMeshReset(void) {}
static int get_item_in_hand_or_watch_menu(GUNHAND hand) { return g_CurrentPlayer->hand_item[hand]; }
static const WeaponStats *get_ptr_item_statistics(int item) { (void)item; return &s_stats; }
f32 gevrRackPullShare(s32 hand) { return s_rack[hand]; }
static void gevrModMeshPoint(const Mtxf *fit, const float point[3], float out[3]) { (void)fit; memcpy(out, point, 3 * sizeof(float)); }
static void gevrModGunActionShot(s32 hand);
static s32 s_interval;   /* gevr_modgame.c: an automatic's ticks between shots */
s32 gevrModGameShotInterval(s32 item) { (void)item; return s_interval; }
static int s_partTicks, s_partShots, s_partInterval;   /* gevr_modattach.c: the timed parts' tick */
void gevrModAttachActionTick(s32 hand, s32 item, s32 shot, f32 ticks, s32 loaded, s32 holdOpen, s32 interval)
{
    (void)hand, (void)item, (void)ticks, (void)loaded, (void)holdOpen;
    ++s_partTicks;
    s_partShots += shot != 0;
    s_partInterval = interval;
}

/* PRODUCTION */

static void near(float got, float want, const char *what)
{
    if (fabsf(got - want) > 1e-5f) {
        printf("FAIL %s: %f, wanted %f\n", what, got, want);
        assert(0);
    }
}

/* one game tick: the shot (gunfire.c) then GoldenEye's slide and the pack's
 * action, each by its own code */
static void tick(int shoot, float ticks)
{
    struct hand *h = &g_CurrentPlayer->hands[0];
    g_GlobalTimerDelta = ticks;
    if (shoot) {
        sub_GAME_7F05E808(0);
        if (h->weapon_ammo_in_magazine > 0) --h->weapon_ammo_in_magazine;
    }
    sub_GAME_7F05E83C(0);
    gevrModGunActionTick(0);
}

int main(void)
{
    static const GevrModMesh mesh;
    static const GevrModGunParts parts = {{2, 1}, {{0, 0, -44}, {0, 3, -55}}, TRUE};
    GevrModGunParts noHold = parts;
    f32 offset[GEVR_MODPART_GROUPS][3];
    noHold.holdOpen = FALSE;

    player.hand_item[0] = ITEM_TEST;
    player.hands[0].weapon_ammo_in_magazine = 3;
    gevrModGunModelSet(0, ITEM_TEST, "pack.bin", NULL, NULL, NULL, NULL, &parts);
    s_gevrModGunShown[0][0].owner = owner;
    s_gevrModGunShown[0][0].mesh = &mesh;
    strcpy(s_gevrModGunShown[0][0].name, "pack.bin");

    /* Shots, the last one emptying it, a frame of two ticks, then a reload:
     * the pack's share is GoldenEye's slide over its stroke at every step. */
    const int shots[] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (unsigned i = 0; i < sizeof(shots) / sizeof(shots[0]); ++i) {
        tick(shots[i], i == 5 ? 2.0f : 1.0f);
        if (i == 24) player.hands[0].weapon_ammo_in_magazine = 3;   /* a fresh magazine */
        near(gevrModGunActionShare(0, FALSE), player.hands[0].field_A88 / s_stats.BoltRecoilBack, "the share follows GoldenEye's slide");
        near(gevrModGunActionShare(0, TRUE), 0.0f, "a handle doesn't move with a shot");
    }
    near(gevrModGunActionShare(0, FALSE), 0.0f, "closed again after the reload");

    /* The share is in ticks, not in the stroke: a carrier without one (the
     * sniper rifle's BoltRecoilBack is 0) still cycles. */
    s_stats.BoltRecoilBack = 0.0f;
    tick(1, 1.0f);
    near(gevrModGunActionShare(0, FALSE), 0.25f, "a quarter open after one tick on a stroke-less carrier");
    assert(gevrModGunParts(0, offset) == &s_gevrModGunPartsFor[0][ITEM_TEST]);
    near(offset[GEVR_MODPART_ACTION][2], -11.0f, "the action group a quarter along its travel");
    near(offset[GEVR_MODPART_HANDLE][1], 0.0f, "the handle group stays home");

    /* the rack's pull moves both, the action as far as it's pulled or the shot holds it */
    s_rack[0] = 0.5f;
    assert(gevrModGunParts(0, offset) != NULL);
    near(offset[GEVR_MODPART_ACTION][2], -22.0f, "the action follows the pull past the shot");
    near(offset[GEVR_MODPART_HANDLE][1], 1.5f, "the handle follows the pull");
    near(offset[GEVR_MODPART_HANDLE][2], -27.5f, "the handle's full vector scales with it");
    s_rack[0] = 0.0f;

    /* empty: GoldenEye's slide stays back; a body whose action doesn't hold open closes */
    player.hands[0].weapon_ammo_in_magazine = 0;
    for (int i = 0; i < 10; ++i) tick(i == 0, 1.0f);
    near(gevrModGunActionShare(0, FALSE), 1.0f, "held open on an empty magazine by default");
    gevrModGunModelSet(0, ITEM_TEST, "pack.bin", NULL, NULL, NULL, NULL, &noHold);
    for (int i = 0; i < 6; ++i) tick(0, 1.0f);
    near(gevrModGunActionShare(0, FALSE), 0.0f, "a body that doesn't hold open closes on its own");

    /* another gun in the hand starts closed; a pending or cleared look has no parts */
    tick(1, 1.0f);
    player.hand_item[0] = ITEM_OTHER;
    near(gevrModGunActionShare(0, FALSE), 0.0f, "another item doesn't take the last one's open action");
    assert(gevrModGunParts(0, offset) == NULL);
    player.hand_item[0] = ITEM_TEST;
    gevrModGunModelSet(0, ITEM_TEST, "pack.bin", NULL, NULL, NULL, NULL, NULL);
    assert(gevrModGunParts(0, offset) == NULL);
    gevrModGunModelSet(0, ITEM_TEST, "pack.bin", NULL, NULL, NULL, NULL, &parts);
    assert(gevrModGunParts(0, offset) != NULL);
    /* A pack's timing (MrSco on #353): open 1, close 5, ease 0.4 is native
     * Tactical's rifle stroke, open * (0.4 + 0.6 * open) from the shot's own
     * tick back over five more (port/include's formula, no Tactical code). */
    {
        GevrModGunParts timed = parts;
        timed.holdOpen = FALSE;
        timed.timing = (GevrModActionTiming){ 1, 5, 0.4f };
        gevrModGunModelSet(0, ITEM_TEST, "pack.bin", NULL, NULL, NULL, NULL, &timed);
        player.hands[0].weapon_ammo_in_magazine = 5;
        for (int i = 0; i < 8; ++i) tick(0, 1.0f);   /* closed */
        s_partShots = 0;
        for (int age = 0; age < 8; ++age) {
            tick(age == 0, 1.0f);
            const float o = age < 6 ? 1.0f - age / 5.0f : 0.0f;
            near(gevrModGunActionShare(0, FALSE), o * (0.4f + 0.6f * o), "the pack's own timing and ease, tick by tick");
        }
        assert(s_partShots == 1 && s_partInterval == 0);   /* the parts were told the shot once */
        /* GoldenEye's default doesn't take the ease; the pull isn't eased */
        s_rack[0] = 0.3f;
        near(gevrModGunActionShare(0, FALSE), 0.3f, "the rack's pull as pulled");
        s_rack[0] = 0.0f;
        /* an automatic firing every 4 ticks: the close is cut to 3 so a cycle fits */
        s_interval = 4;
        for (int age = 0; age < 5; ++age) {
            tick(age == 0, 1.0f);
            const float o = age < 4 ? 1.0f - age / 3.0f : 0.0f;
            near(gevrModGunActionShare(0, FALSE), o * (0.4f + 0.6f * o), "a fast gun's close fits between its shots");
        }
        assert(s_partInterval == 4);
        s_interval = 0;
    }
    {
        int open, close;
        const GevrModActionTiming none = { 0, 0, 1.0f };
        gevrModActionTicks(&none, 3, &open, &close);
        assert(open == 4 && close == 6);   /* GoldenEye's own is never shortened */
        gevrModActionTicks(NULL, 0, &open, &close);
        assert(open == 4 && close == 6);
        const GevrModActionTiming slow = { 10, 30, 1.0f };
        gevrModActionTicks(&slow, 5, &open, &close);
        assert(open == 10 && close == 1);   /* never below one tick */
    }
    gevrModGunModelReset();
    assert(gevrModGunParts(0, offset) == NULL);
    puts("pack action: GoldenEye's slide rule on the pack's stroke, the rack's pull, hold open, the groups' offsets, and a pack's own timing, ease and fast-gun close");
    return 0;
}
