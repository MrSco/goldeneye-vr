/* Exercise the real model set, fitted hold lookup and reset functions. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef int s32;
typedef unsigned char u8;
typedef float f32;
typedef union { float f[3]; } coord3d;
typedef struct { float m[4][4]; } Mtxf;
typedef struct { int unused; } GevrModMesh;
typedef struct { unsigned pictures[2]; float travel[2][3]; int holdOpen; } GevrModGunParts;
enum { GEVR_MODHAND_READY, GEVR_MODHAND_TRIGGER, GEVR_MODHAND_SUPPORT, GEVR_MODHAND_POSES };
enum { FALSE, TRUE, ITEM_UNARMED, ITEM_TEST, ITEM_IDS_MAX, MAX_PLAYER_COUNT = 4 };
static struct TestPlayer { int hand_item[2]; } player;
static void *owner = &player;
static struct TestPlayer *g_CurrentPlayer = &player;
static int netIsActive(void) { return FALSE; }
static int get_cur_playernum(void) { return 0; }
static int netGetLocalSlot(void) { return 0; }
static void gevrModMeshReset(void) {}

/* PRODUCTION */

int main(void)
{
    static const GevrModMesh mesh;
    static const float support[3] = {4, 5, 60};
    static const float fit[4][4] = {{-1,0,0,0}, {0,1,0,0}, {0,0,2,0}, {2,10,20,1}};
    coord3d point = {{10, 20, 30}};
    player.hand_item[0] = ITEM_TEST;
    assert(!gevrModGunSupport(0, &point)); /* stock: caller keeps its hold */
    assert(point.f[0] == 10 && point.f[1] == 20 && point.f[2] == 30);
    gevrModGunModelSet(0, ITEM_TEST, "pack.bin", fit, NULL, support, NULL, NULL);
    s_gevrModGunShown[0][0].owner = owner;
    s_gevrModGunShown[0][0].mesh = &mesh;
    strcpy(s_gevrModGunShown[0][0].name, "previous.bin");
    assert(!gevrModGunSupport(0, &point)); /* pending replacement */
    strcpy(s_gevrModGunShown[0][0].name, "pack.bin");
    assert(gevrModGunSupport(0, &point));
    assert(point.f[0] == -2 && point.f[1] == 15 && point.f[2] == 140);
    gevrModGunModelSet(0, ITEM_TEST, "pack.bin", fit, NULL, NULL, NULL, NULL);
    assert(!gevrModGunSupport(0, &point)); /* the same body drops its override */
    gevrModGunModelSet(0, ITEM_TEST, "pack.bin", fit, NULL, support, NULL, NULL);
    assert(gevrModGunSupport(0, &point));
    gevrModGunModelReset();
    assert(!gevrModGunSupport(0, &point)); /* stage reset */
    puts("gun model: the pack hold uses its fit, and stock hold resumes on clear or reset");
    return 0;
}
