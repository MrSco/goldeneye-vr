/* A pack gun's gloves against the game's own code (test_pack_hands.py pastes
 * gun.c's table and lookups and gevr_modgame.c's check in): which glove a
 * hand draws in each pose, with whose fit, only on the pack body it was given
 * for; the support glove only while that gun is held two-handed; refusals. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gevr_modaction.h"

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
static int netIsActive(void) { return FALSE; }
static int get_cur_playernum(void) { return 0; }
static int netGetLocalSlot(void) { return 0; }
static void gevrModMeshReset(void) {}

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
    gevrModGunModelSet(GUNRIGHT, ITEM_TEST, "pack-body.bin", bodyFit, NULL, NULL, NULL);
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
    gevrModGunModelReset();
    assert(gevrModGunHand(GUNRIGHT, GEVR_MODHAND_READY, &fit, &fitted) == NULL);
    puts("pack hands: gloves per pose and fit on their own pack body, the trigger, two-handed support, refusals and resets");
    return 0;
}
