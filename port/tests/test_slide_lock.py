"""Compile the GE-X slide (gun.c) with GoldenEye's own slide clock and check it
cycles on a shot, locks open on the last round and closes on a loaded magazine."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "bondconstants.h"
#include "gevr_gexweapon.h"
#define TRUE 1
#define FALSE 0
#define MAX_PLAYER_COUNT 4
float VrReloadGrab[2][3], VrGexForeHold[3], VrGexPp7Grab[3], VrGexPp7Support[3], VrGexPp7SupportRot[3];
float VrGexGunOff[3], VrGexPp7GunOff[3], VrGexKf7MagOff[3], VrGexPp7MagOff[3], VrGexKf7WellOff[3], VrGexPp7WellOff[3];
float VrGexWeaponFits[64][10][3], VrGexBatFit[5][3];
typedef struct { f32 m[4][4]; } Mtxf;
typedef struct { s32 numMatrices; } ModelFileHeader;
typedef struct { f32 BoltRecoilBack; } WeaponStats;
struct hand { f32 field_A88; s32 field_A8C, weapon_ammo_in_magazine; };
static struct { struct hand hands[2]; } player;
static typeof(player) *g_CurrentPlayer = &player;
static WeaponStats stats = { 30.0f };   /* GoldenEye's PP7: BoltRecoilBack 29.999998 */
static f32 g_GlobalTimerDelta = 1.0f;
static s32 item = ITEM_WPPK;
static f32 s_gevrGexReadyFrame[MAX_PLAYER_COUNT][2];
static ITEM_IDS get_item_in_hand_or_watch_menu(GUNHAND hand) { (void)hand; return item; }
static WeaponStats *get_ptr_item_statistics(ITEM_IDS i) { (void)i; return &stats; }
static s32 get_cur_playernum(void) { return 0; }
const GexWeaponDef *gevrGexWeaponForHand(s32 hand) { (void)hand; return gevrGexWeaponGet(item); }
/* PRODUCTION */
static Mtxf rw[64], rest;
static void pose(GUNHAND hand, s32 clip)
{
    ModelFileHeader hdr = { 43 };
    rw[37] = rest;
    gevrGexSlideTo(gevrGexWeaponForHand(hand), &hdr, rw, hand, clip);
}
/* how far back the slide is drawn, along its own z (model units) */
static f32 drawnBack(void) { return (rest.m[3][2] - rw[37].m[3][2]) / rest.m[2][2]; }
static void fire(GUNHAND hand, s32 left)
{
    player.hands[hand].weapon_ammo_in_magazine = left;
    sub_GAME_7F05E808(hand);
}
int main(void)
{
    memset(&rest, 0, sizeof(rest));
    rest.m[0][0] = rest.m[1][1] = rest.m[2][2] = 0.5f; rest.m[3][3] = 1;
    rest.m[3][0] = 3; rest.m[3][1] = 4; rest.m[3][2] = -60;
    s_gevrGexSlide[ITEM_WPPK] = 37 + 1;   /* GwppkZ: MODELPART_GUN_SLIDE is matrix 37 */
    s_gevrGexReadyFrame[0][GUNRIGHT] = -1;

    /* a shot with rounds left: back over four ticks, home over six */
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 3;
    pose(GUNRIGHT, 0); assert(drawnBack() == 0.0f);
    fire(GUNRIGHT, 2);
    for (int t = 0; t < 4; t++) pose(GUNRIGHT, 0);
    assert(fabsf(drawnBack() - 30.0f) < 0.01f);
    for (int t = 0; t < 6; t++) pose(GUNRIGHT, 0);
    assert(fabsf(drawnBack()) < 0.01f);
    assert(rw[37].m[3][0] == rest.m[3][0] && rw[37].m[3][1] == rest.m[3][1]);   /* along its own z only */

    /* the last round: it stays back */
    fire(GUNRIGHT, 0);
    for (int t = 0; t < 120; t++) pose(GUNRIGHT, 0);
    assert(fabsf(drawnBack() - 30.0f) < 0.01f);
    /* a magazine out and an empty one in keep it open */
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 0;
    for (int t = 0; t < 10; t++) pose(GUNRIGHT, 0);
    assert(fabsf(drawnBack() - 30.0f) < 0.01f);

    /* a loaded magazine seated: no rack (the ready motion), the slide closes */
    player.hands[GUNRIGHT].weapon_ammo_in_magazine = 7;
    gevrGexMagazineReady(GUNRIGHT);
    assert(s_gevrGexReadyFrame[0][GUNRIGHT] < 0);
    for (int t = 0; t < 6; t++) pose(GUNRIGHT, 0);
    assert(fabsf(drawnBack()) < 0.01f);
    /* with the slide home, a magazine seated is racked as before */
    gevrGexMagazineReady(GUNRIGHT);
    assert(s_gevrGexReadyFrame[0][GUNRIGHT] == gevrGexWeaponGet(ITEM_WPPK)->reload.ammoFrame);
    s_gevrGexReadyFrame[0][GUNRIGHT] = -1;

    /* while a clip poses the slide the clock still runs, the offset stays off */
    fire(GUNRIGHT, 0);
    for (int t = 0; t < 4; t++) pose(GUNRIGHT, 1);
    assert(drawnBack() == 0.0f && player.hands[GUNRIGHT].field_A88 > 29.9f);

    /* a rig with no slide part, or a gun with no bolt stat, never moves */
    player.hands[GUNLEFT].weapon_ammo_in_magazine = 3;
    item = ITEM_AK47;
    fire(GUNLEFT, 0);
    for (int t = 0; t < 10; t++) pose(GUNLEFT, 0);
    assert(drawnBack() == 0.0f);
    item = ITEM_WPPK; stats.BoltRecoilBack = 0; player.hands[GUNLEFT].field_A88 = 0;
    fire(GUNLEFT, 0);
    for (int t = 0; t < 10; t++) pose(GUNLEFT, 0);
    assert(drawnBack() == 0.0f);
    puts("PASS: GE-X slide cycles per shot, locks open on the last round, closes on a loaded magazine");
    return 0;
}
'''

gun = (ROOT / "src/game/gun.c").read_text(encoding="utf-8")
production = [line for line in gun.splitlines()
              if line.startswith(("#define GEVR_PD_PART_GUN_SLIDE", "static s8 s_gevrGexSlide["))]
production += [function(gun, s) for s in (
    "void sub_GAME_7F05E808(GUNHAND hand) {", "void sub_GAME_7F05E83C(GUNHAND hand)\n{",
    "static s32 gevrGexSlideMatrix(", "static s32 gevrGexSlideLocked(", "static void gevrGexSlideTo(",
    "void gevrGexMagazineReady(")]
# the rig's own pose moves its slide, and the slide's matrix is looked up from the model's parts
assert "gevrGexSlideTo(def, hdr, rwmtx, hand," in function(gun, "void gevrGexPoseGun(")
assert "gevrGexPartMatrix(def->model, GEVR_PD_PART_GUN_SLIDE)" in function(gun, "static void gevrGexGunPrepare(")
with tempfile.TemporaryDirectory(prefix="gevr-slide-lock-") as temp:
    source, exe = Path(temp) / "test.c", Path(temp) / "test.exe"
    source.write_text(HARNESS.replace("/* PRODUCTION */", "\n".join(production)), encoding="utf-8")
    subprocess.run([shutil.which("gcc") or "gcc", "-std=gnu11", "-O2", "-D_LANGUAGE_C",
                    "-I" + str(ROOT), "-I" + str(ROOT / "include"), "-I" + str(ROOT / "src"),
                    "-I" + str(ROOT / "port/include"),
                    str(source), str(ROOT / "port/src/gevr_gexweapon.c"), "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
