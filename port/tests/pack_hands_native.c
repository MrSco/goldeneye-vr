/* A pack gun's gloves against the game's own code (test_pack_hands.py pastes
 * gun.c's table and lookups and gevr_modgame.c's check in): which glove a
 * hand draws in each pose, with whose fit, only on the pack body it was given
 * for; the support glove only while that gun is held two-handed; refusals. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gevr_modaction.h"
#include "gevr_modgrip.h"
#include <math.h>

typedef int s32;
typedef unsigned char u8;
typedef float f32;
typedef union { float f[3]; } coord3d;
typedef struct { float m[4][4]; } Mtxf;
typedef struct { int unused; } GevrModMesh;
enum { FALSE, TRUE };
enum { ITEM_UNARMED, ITEM_TEST, ITEM_OTHER, ITEM_THROWN, ITEM_IDS_MAX, MAX_PLAYER_COUNT = 4 };
enum { GUNRIGHT, GUNLEFT };
enum { GEVR_MODPART_ACTION, GEVR_MODPART_HANDLE, GEVR_MODPART_GROUPS };
enum { GEVR_MODHAND_READY, GEVR_MODHAND_TRIGGER, GEVR_MODHAND_SUPPORT, GEVR_MODHAND_POSES };
typedef struct { unsigned pictures[GEVR_MODPART_GROUPS]; float travel[GEVR_MODPART_GROUPS][3]; int holdOpen;
                 GevrModActionTiming timing; } GevrModGunParts;
static struct TestPlayer { int hand_item[2]; } player;
static void *owner = &player;
static struct TestPlayer *g_CurrentPlayer = &player;
static int s_net;   /* a co-op session in which the current player is another's copy */
static int netIsActive(void) { return s_net; }
static int get_cur_playernum(void) { return s_net ? 1 : 0; }
static int netGetLocalSlot(void) { return 0; }
static void gevrModMeshReset(void) {}
void gevrModMeshPoint(const Mtxf *fit, const float point[3], float out[3]);   /* gevr_modmesh.c, pasted in */

/* the mission's meshes: glove files are pack-*.bin; bad-*.bin won't load */
static const GevrModMesh s_meshes[8];
static const char *s_names[8];
static int s_loads;
static int gevrModAssetHasModel(const char *name) { return strncmp(name, "none", 4) != 0; }
static int gevrModMeshLoad(const char *name, const GevrModMesh **out)
{
    ++s_loads;
    if (strncmp(name, "pack-", 5)) return strncmp(name, "bad-", 4) ? 0 : -1;
    for (int i = 0; i < 8; ++i) {
        if (s_names[i] == NULL) s_names[i] = name;
        if (!strcmp(s_names[i], name)) { *out = &s_meshes[i]; return 1; }
    }
    return -1;
}
static int gevrIsThrowable(int item) { return item == ITEM_THROWN; }
static int s_twoHand, s_twoHandGun;
s32 gevrStereoTwoHandGrip(void) { return s_twoHand; }
s32 gevrStereoTwoHandGun(void) { return s_twoHandGun; }
int gevrVrTriggerDown[2];
s32 g_gevrStereo = TRUE;

/* PRODUCTION */

static void sameFit(const Mtxf *a, const float b[4][4]) { assert(!memcmp(a->m, b, sizeof(a->m))); }

int main(void)
{
    static const GevrModMesh body;
    static const float bodyFit[4][4] = {{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,7,1}};
    static const float gloveFit[4][4] = {{-1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,12,1}};
    static const float supportFit[4][4] = {{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,30,200,1}};
    const char *all[GEVR_MODHAND_POSES] = { "pack-ready.bin", "pack-trigger.bin", "pack-support.bin" };
    const float (*fits[GEVR_MODHAND_POSES])[4] = { gloveFit, gloveFit, supportFit };
    const float (*none[GEVR_MODHAND_POSES])[4] = { NULL, NULL, NULL };
    Mtxf fit;
    s32 fitted;

    player.hand_item[GUNRIGHT] = ITEM_TEST;
    /* the game side refuses a thrown item and a file that isn't a pack mesh, changing nothing */
    assert(!gevrModGameGunHands(GUNRIGHT, ITEM_THROWN, all, fits));
    const char *legacy[GEVR_MODHAND_POSES] = { "GlegacyZ", NULL, NULL };
    assert(!gevrModGameGunHands(GUNRIGHT, ITEM_TEST, legacy, none));
    const char *broken[GEVR_MODHAND_POSES] = { "pack-ready.bin", "bad-trigger.bin", NULL };
    assert(!gevrModGameGunHands(GUNRIGHT, ITEM_TEST, broken, none));
    const char *missing[GEVR_MODHAND_POSES] = { "none.bin", NULL, NULL };
    assert(!gevrModGameGunHands(GUNRIGHT, ITEM_TEST, missing, none));
    assert(gevrModGameGunHands(GUNRIGHT, ITEM_TEST, all, fits));

    /* no pack body drawn yet: the stock hand */
    assert(gevrModGunHand(GUNRIGHT, GEVR_MODHAND_READY, &fit, &fitted) == NULL);
    gevrModGunModelSet(GUNRIGHT, ITEM_TEST, "pack-body.bin", bodyFit, NULL, NULL, NULL, NULL);
    s_gevrModGunShown[0][GUNRIGHT].owner = owner;
    s_gevrModGunShown[0][GUNRIGHT].mesh = &body;
    strcpy(s_gevrModGunShown[0][GUNRIGHT].name, "pack-body.bin");
    const GevrModMesh *ready = gevrModGunHand(GUNRIGHT, GEVR_MODHAND_READY, &fit, &fitted);
    assert(ready != NULL && fitted);
    sameFit(&fit, gloveFit);   /* its own frame */
    assert(gevrModGunHand(GUNRIGHT, GEVR_MODHAND_TRIGGER, &fit, &fitted) != ready);
    assert(gevrModGunHand(GUNLEFT, GEVR_MODHAND_READY, &fit, &fitted) == NULL);   /* only the hand it was given for */

    /* without frames each glove takes the body's fit */
    assert(gevrModGameGunHands(GUNRIGHT, ITEM_TEST, all, none));
    assert(gevrModGunHand(GUNRIGHT, GEVR_MODHAND_SUPPORT, &fit, &fitted) != NULL && fitted);
    sameFit(&fit, bodyFit);
    assert(gevrModGameGunHands(GUNRIGHT, ITEM_TEST, all, fits));

    /* the trigger pose follows the VR trigger; the flat game shows ready */
    gevrVrTriggerDown[GUNRIGHT] = TRUE;
    assert(gevrModGunTriggerHeld(GUNRIGHT) && !gevrModGunTriggerHeld(GUNLEFT));
    g_gevrStereo = FALSE;
    assert(!gevrModGunTriggerHeld(GUNRIGHT));
    g_gevrStereo = TRUE;

    /* the support glove shows only while the other hand holds this gun */
    assert(!gevrModGunSupportGloveShown(GUNRIGHT));
    s_twoHand = TRUE;
    s_twoHandGun = GUNLEFT;
    assert(!gevrModGunSupportGloveShown(GUNRIGHT));
    s_twoHandGun = GUNRIGHT;
    assert(gevrModGunSupportGloveShown(GUNRIGHT));
    assert(gevrModGunHand(GUNRIGHT, GEVR_MODHAND_SUPPORT, &fit, &fitted) != NULL);
    sameFit(&fit, supportFit);
    const char *readyOnly[GEVR_MODHAND_POSES] = { "pack-ready.bin", NULL, NULL };
    assert(gevrModGameGunHands(GUNRIGHT, ITEM_TEST, readyOnly, none));
    assert(!gevrModGunSupportGloveShown(GUNRIGHT));   /* the off hand's own arm again */
    assert(gevrModGunHand(GUNRIGHT, GEVR_MODHAND_TRIGGER, &fit, &fitted) == NULL);   /* the caller falls back to ready */

    /* another gun, a cleared hand, a pending body and a new mission: the stock hand */
    player.hand_item[GUNRIGHT] = ITEM_OTHER;
    assert(gevrModGunHand(GUNRIGHT, GEVR_MODHAND_READY, &fit, &fitted) == NULL);
    player.hand_item[GUNRIGHT] = ITEM_TEST;
    assert(gevrModGameGunHands(GUNRIGHT, ITEM_TEST, NULL, none));
    assert(gevrModGunHand(GUNRIGHT, GEVR_MODHAND_READY, &fit, &fitted) == NULL);
    assert(gevrModGameGunHands(GUNRIGHT, ITEM_TEST, all, fits));
    strcpy(s_gevrModGunShown[0][GUNRIGHT].name, "previous.bin");
    assert(gevrModGunHand(GUNRIGHT, GEVR_MODHAND_READY, &fit, &fitted) == NULL);
    strcpy(s_gevrModGunShown[0][GUNRIGHT].name, "pack-body.bin");
    assert(gevrModGunHand(GUNRIGHT, GEVR_MODHAND_READY, &fit, &fitted) != NULL);
    /* the grip point: through the body's fit, only on its own body, none after a reset */
    {
        static const float grip[3] = {0, -40, 90};
        coord3d at;
        gevrModGunModelSet(GUNRIGHT, ITEM_TEST, "pack-body.bin", bodyFit, NULL, NULL, grip, NULL);
        assert(gevrModGunGrip(GUNRIGHT, &at) && at.f[0] == 0 && at.f[1] == -40 && at.f[2] == 97);
        assert(!gevrModGunGrip(GUNLEFT, &at));
        gevrModGunModelSet(GUNRIGHT, ITEM_TEST, "pack-body.bin", bodyFit, NULL, NULL, NULL, NULL);
        assert(!gevrModGunGrip(GUNRIGHT, &at));   /* a later look without it clears it */
        gevrModGunModelSet(GUNRIGHT, ITEM_TEST, "pack-body.bin", bodyFit, NULL, NULL, grip, NULL);
        strcpy(s_gevrModGunShown[0][GUNRIGHT].name, "previous.bin");
        assert(!gevrModGunGrip(GUNRIGHT, &at));   /* pending */
        strcpy(s_gevrModGunShown[0][GUNRIGHT].name, "pack-body.bin");
    }
    /* the placement (gevr_modgrip.h) through bondview2's root formula: the grip
     * point lands in the controller's grip at the default fit, either hand
     * mode, and the player's trim moves it by exactly the trim */
    for (int lefty = 0; lefty < 2; ++lefty) {
        static const float game[3] = {2.74f, 1.94f, -12.35f};
        const float right[3] = {0.6f, 0.0f, 0.8f}, up[3] = {0, 1, 0}, back[3] = {-0.8f, 0.0f, 0.6f}, pos[3] = {5, 6, 7};
        const float cm = 3.0f, size = 1.1f, viewCm = 0.85f, toOrigin = 12.0f, k = viewCm * cm * size;
        const float point[3] = {3, -40, 97};
        for (int trimmed = 0; trimmed < 2; ++trimmed) {
            const float player[3] = { game[0] + trimmed * 1.0f, game[1] - trimmed * 2.0f, game[2] + trimmed * 0.5f };
            float off[3], root[3], at[3], ax[3][3];
            gevrModGripOff(point, player, game, viewCm, toOrigin, off);
            for (int i = 0; i < 3; ++i) {
                /* bondview2.c gevrStereoGunMatrix: the axes and the root */
                ax[0][i] = (lefty ? right[i] : -right[i]) * k;
                ax[1][i] = up[i] * k;
                ax[2][i] = -back[i] * k;
                root[i] = pos[i] + ((lefty ? -off[0] : off[0]) * right[i] + off[1] * up[i] + (toOrigin + off[2]) * back[i]) * cm * size;
            }
            for (int i = 0; i < 3; ++i) at[i] = root[i] + point[0] * ax[0][i] + point[1] * ax[1][i] + point[2] * ax[2][i];
            for (int i = 0; i < 3; ++i) {
                const float trim = ((lefty ? -1.0f : 1.0f) * (player[0] - game[0]) * right[i] + (player[1] - game[1]) * up[i]
                                    + (player[2] - game[2]) * back[i]) * cm * size;
                assert(fabsf(at[i] - (pos[i] + trim)) < 1e-3f);
            }
        }
    }
    /* the empty off hand's gloves: open, pinch while gripped (else open), its
     * own frame, refusals that change nothing, another player's copy, a reset */
    {
        static const float offFit[4][4] = {{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,5,1}};
        assert(gevrModOffGlove(0, &fit, &fitted) == NULL);
        assert(gevrModGameOffGloves("pack-open.bin", "pack-pinch.bin", offFit));
        const GevrModMesh *open = gevrModOffGlove(0, &fit, &fitted);
        assert(open != NULL && fitted);
        sameFit(&fit, offFit);
        assert(gevrModOffGlove(1, &fit, &fitted) != open && gevrModOffGlove(1, &fit, &fitted) != NULL);
        assert(!gevrModGameOffGloves("pack-open.bin", "bad-pinch.bin", NULL));
        assert(!gevrModGameOffGloves("GlegacyZ", NULL, NULL));
        assert(gevrModOffGlove(1, &fit, &fitted) != open);   /* unchanged by the refusals */
        assert(gevrModGameOffGloves("pack-open.bin", NULL, NULL));
        assert(gevrModOffGlove(1, &fit, &fitted) == open && !fitted);   /* no pinch: open throughout */
        s_net = TRUE;
        assert(gevrModOffGlove(0, &fit, &fitted) == NULL);   /* another player's copy */
        s_net = FALSE;
        assert(gevrModGameOffGloves(NULL, NULL, NULL));
        assert(gevrModOffGlove(0, &fit, &fitted) == NULL);
        assert(gevrModGameOffGloves("pack-open.bin", NULL, NULL));
        gevrModGunModelReset();
        assert(gevrModOffGlove(0, &fit, &fitted) == NULL);
    }
    gevrModGunModelReset();
    assert(gevrModGunHand(GUNRIGHT, GEVR_MODHAND_READY, &fit, &fitted) == NULL);
    puts("pack hands: gloves per pose and fit on their own pack body, the trigger, two-handed support, refusals and resets");
    return 0;
}
