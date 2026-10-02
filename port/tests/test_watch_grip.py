"""Native regression checks for the original right watch hand and separate left arm."""
from pathlib import Path
import json
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
view = (ROOT / "src/game/bondview2.c").read_text(encoding="utf-8")
math = (ROOT / "src/game/matrixmath.c").read_text(encoding="utf-8")
gun = (ROOT / "src/game/gunfire.c").read_text(encoding="utf-8")
state = (ROOT / "src/game/gun.c").read_text(encoding="utf-8")
model = (ROOT / "src/game/model.c").read_text(encoding="utf-8")
convert = (ROOT / "port/src/gevr_model.c").read_text(encoding="utf-8")
handpatch = (ROOT / "port/src/gevr_handpatch.c").read_text(encoding="utf-8")


def gu_file(name):
    return (ROOT / "src/libultra/gu" / name).read_text(encoding="utf-8")


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


definitions = "\n".join(re.search(r"^#define " + name + r"\s+[^\n]+", view, re.M).group()
                        for name in ("GEVR_UNITS_PER_METRE", "GEVR_VIEWMODEL_CM",
                                     "GEVR_GRIP_TO_ORIGIN_CM", "GEVR_WRIST_BEHIND_CM"))
definitions += "\n" + "\n".join(re.search(r"^static (?:const )?f32 " + name + r"\[[37]\] = [^;]+;", view, re.M).group()
                                      for name in ("s_gevrLaserFace", "s_gevrLaserNormal", "s_gevrLaserForearm", "s_gevrWatchHandTrim"))
definitions += "\n" + re.search(r"^#define GEVR_WATCHHAND_DLS[^\n]+", gun, re.M).group()
definitions += "\n" + "\n".join(re.search(r"^#define " + name + r"\s+[^\n]+", view, re.M).group()
                                    for name in ("GEVR_WATCH_PRESS_CM", "GEVR_WATCH_KEEP_CM", "GEVR_WATCH_REACH_CM"))
definitions += "\n" + re.search(r"static const struct \{[^}]+\} s_hpDrop\[\] = \{.*?\n\};", handpatch, re.S).group()
production = "\n".join((
    function(math, "void matrix_4x4_set_rotation_around_xyz("),
    function(math, "void matrix_4x4_set_rotation_around_y("),
    function(math, "void matrix_4x4_set_position_and_rotation_around_y("),
    function(math, "void matrix_4x4_set_position("),
    function(math, "void matrix_4x4_multiply_homogeneous("),
    function(gu_file("mtxutil.c"), "void guMtxIdentF("),
    function(gu_file("normalize.c"), "void guNormalize("),
    function(gu_file("rotate.c"), "void guRotateF("),
    function(model, "s32 modelFindNodeMtxIndex("),
    function(state, "f32 get_value_if_watch_is_on_hand_or_not("),
    function(state, "void sub_GAME_7F05E6B4("),
    function(gun, "static void gevrWatchHandTickHiddenFinger("),
    function(view, "static s32 gevrWatchFaceFrame("),
    function(view, "s32 gevrStereoWatchHandMatrix("),
    function(gun, "static s32 gevrWatchHandHideLeft("),
    function(gun, "static void gevrWatchHandAnimateFinger("),
    function(view, "s32 gevrStereoWatchGripUpdate("),
    function(handpatch, "s32 gevrHandPatchDropsTexture("),
    function(convert, "static s32 gevrModelDropsWatchTexture("),
    function(convert, "static void gevrModelFilterDisplayList("),
))
# Each side's patches must live on its own DL, so the hidden left model leaves no faces behind.
patch = json.loads((ROOT / "tools/handpatch/GwatchlaserZ.patch.json").read_text(encoding="utf-8"))
assert len(patch["parts"]) == 9
for part in patch["parts"]:
    assert set(part["nodes"]) == {part["host"]}
    for group in part["groups"]:
        for vertex in group["verts"]:
            refs = {mix[0] for mix in vertex["mix"]} if "mix" in vertex else {vertex["node"]}
            assert refs == {part["host"]}

fixture = r'''
#include <ultra64.h>
#undef GAME_TICKRATE
#include <bondtypes.h>
#include <bondconstants.h>
#include <bondgame.h>
#include "game/bondview.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "game/matrixmath.h"
#define sysLogPrintf(...) ((void)0)
#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif
static int g_gevrStereo = 1, VrLeftHandedMode, havePose[2] = {1, 1};
float D_800364CC = 1;
static float size = 1;
static float VrGunOffX = 2.74f, VrGunOffY = 1.94f, VrGunOffZ = -12.35f;
static int s_gevrWatchFaceKnown = 1;
static float s_gevrWatchFaceCm[3] = {1.5f, 0.7f, -0.2f};
static Mtxf poses[2];
static struct player player;
struct player *g_CurrentPlayer = &player;
static int g_GlobalTimerDelta = 1;
static ITEM_IDS getCurrentPlayerWeaponId(GUNHAND hand) { return player.hands[hand].weaponnum; }
static ModelFileHeader *gevrModelPendingHeader, *gevrModelWatchGripHeader;
static int s_gevrWatchGrip, online, localSlot, playerSlot, haveWatchPoint = 1;
static float watchPoint[3];
static int netIsActive(void) { return online; }
static int get_cur_playernum(void) { return playerSlot; }
static int netGetLocalSlot(void) { return localSlot; }
static int gevrStereoWatchPoint(float out[3]) { memcpy(out, watchPoint, sizeof(watchPoint)); return haveWatchPoint; }
static void gevrHandPatchNoteMarker(u32 tex, u32 word) { (void)tex; (void)word; }
static float gevrGunSizeFactor(void) { return size; }
static int gevrGripAxes(int ctrl, float pos[3], float right[3], float up[3], float back[3])
{
    memcpy(pos, poses[ctrl].m[3], 3 * sizeof(float));
    memcpy(right, poses[ctrl].m[0], 3 * sizeof(float));
    memcpy(up, poses[ctrl].m[1], 3 * sizeof(float));
    memcpy(back, poses[ctrl].m[2], 3 * sizeof(float));
    return havePose[ctrl];
}
/* DEFINITIONS */
/* PRODUCTION */
static void closeEnough(float actual, float expected)
{
    assert(fabsf(actual - expected) <= 0.0001f * fmaxf(1, fabsf(expected)));
}
static float dot(const float *a, const float *b)
{
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
static float determinant(const Mtxf *m)
{
    const float *a = m->m[0], *b = m->m[1], *c = m->m[2];
    return a[0]*(b[1]*c[2]-b[2]*c[1]) - a[1]*(b[0]*c[2]-b[2]*c[0])
         + a[2]*(b[0]*c[1]-b[1]*c[0]);
}
static Mtxf snap(void)
{
    Mtxf m;
    assert(gevrStereoWatchHandMatrix(&m));
    return m;
}
static void checkFinger(const Mtxf *base)
{
    union ModelRoData fingerData = {0};
    float hinge[6] = {0, 0, 1, 0, 0, 0};
    ModelNode finger = {.Opcode = MODELNODE_OPCODE_GROUP, .Data = &fingerData};
    ModelNode axis = {.Data = (union ModelRoData *)hinge};
    ModelNode *switches[29] = {0};
    switches[6] = &finger; switches[28] = &axis;
    ModelFileHeader header = {.Switches = switches, .numSwitches = 29, .numMatrices = 4};
    fingerData.Group.MatrixID0 = 3;
    fingerData.Group.Origin = (coord3d){.x = -3, .y = 7, .z = 2};
    Mtxf rest[4], pressed[4];
    for (int i = 0; i < 4; i++) rest[i] = pressed[i] = *base;
    player.hands[GUNRIGHT].weaponnum = ITEM_WATCHLASER;
    float limit = get_value_if_watch_is_on_hand_or_not(GUNRIGHT);
    gevrWatchHandAnimateFinger(&header, (Mtxf *)base, rest, 0);
    gevrWatchHandAnimateFinger(&header, (Mtxf *)base, pressed, limit);
    for (int m = 0; m < 3; m++) {
        assert(memcmp(&rest[m], base, sizeof(*base)) == 0);
        assert(memcmp(&pressed[m], base, sizeof(*base)) == 0);
    }
    for (int j = 0; j < 3; j++) {
        float origin = base->m[3][j];
        for (int k = 0; k < 3; k++) origin += fingerData.Group.Origin.f[k]*base->m[k][j];
        closeEnough(rest[3].m[3][j], origin);
        closeEnough(pressed[3].m[3][j], origin);
        /* A point along the finger rotates five degrees round the hinge,
         * independent of the attached frame's handedness and size. */
        closeEnough(pressed[3].m[0][j], base->m[0][j]);
        closeEnough(rest[3].m[0][j], cosf(limit)*base->m[0][j] - sinf(limit)*base->m[1][j]);
    }
    assert(memcmp(&rest[3], &pressed[3], sizeof(Mtxf)) != 0);
    /* Repeated rendering reuses the tick's state; it never advances it. */
    float before = player.hands[GUNRIGHT].field_A84;
    Mtxf again[4]; memcpy(again, rest, sizeof(rest));
    gevrWatchHandAnimateFinger(&header, (Mtxf *)base, again, 0);
    assert(memcmp(again, rest, sizeof(rest)) == 0);
    assert(player.hands[GUNRIGHT].field_A84 == before);
    switches[6] = NULL;
    gevrWatchHandAnimateFinger(&header, (Mtxf *)base, again, limit);
    assert(memcmp(again, rest, sizeof(rest)) == 0);
    switches[6] = &finger; fingerData.Group.MatrixID0 = 4;
    gevrWatchHandAnimateFinger(&header, (Mtxf *)base, again, limit);
    assert(memcmp(again, rest, sizeof(rest)) == 0);
}
static void checkWatchFaces(void)
{
    const u32 watch[] = {0x5dd, 0x5de, 0x5df, 0x5e0, 0x5e1, 0x5e3, 0x648, 0x809};
    const u32 keep[] = {0x701, 0x702, 0x703, 0x704, 0x705, 0x706,
                       0x63d, 0x63e, 0x645, 0x660, 0x667, 0x66c, 0x69f};
    ModelFileHeader privateHeader = {0}, sharedHeader = {0};
    for (int mode = 0; mode < 4; mode++) {
        gevrModelWatchGripHeader = mode ? &privateHeader : NULL;
        gevrModelPendingHeader = mode == 2 ? &sharedHeader : &privateHeader;
        for (int tex = 0; tex < 21; tex++) {
            Gfx commands[8] = {0}, original[8];
            commands[0].words.w0 = 0xc0000000; commands[0].words.w1 = tex < 8 ? watch[tex] : keep[tex-8];
            commands[1].words.w0 = 0x04000030; commands[1].words.w1 = 0x05000100; /* vertices */
            commands[2].words.w0 = 0xbf000000; commands[2].words.w1 = 0x000a141e;
            commands[3].words.w0 = 0xb1123456; commands[3].words.w1 = 0x12345678;
            commands[4].words.w0 = 0xc0000000; commands[4].words.w1 = 0x702; /* back to skin */
            commands[5].words.w0 = 0x01000040; commands[5].words.w1 = 0x030000c0; /* finger matrix */
            commands[6].words.w0 = 0xbf000000; commands[6].words.w1 = 0x000a141e;
            commands[7].words.w0 = 0xb8000000;
            memcpy(original, commands, sizeof(commands));
            gevrModelFilterDisplayList(commands, 8, mode == 3 ? "Csuit_lf_handZ" : "GwatchlaserZ");
            for (int i = 0; i < 8; i++) {
                if (mode == 1 && tex < 8 && (i == 2 || i == 3)) {
                    assert(commands[i].words.w0 == 0xb1000000 && commands[i].words.w1 == 0);
                } else assert(memcmp(&commands[i], &original[i], sizeof(Gfx)) == 0);
            }
        }
    }
    assert(gevrHandPatchDropsTexture("GfistZ", 0x5ea)); /* existing fist correction */
    gevrModelWatchGripHeader = gevrModelPendingHeader = NULL;
    Gfx fist[3] = {0};
    fist[0].words.w0 = 0xc0000000; fist[0].words.w1 = 0x5ea;
    fist[1].words.w0 = 0xbf000000; fist[1].words.w1 = 0x000a141e;
    fist[2].words.w0 = 0xb8000000;
    gevrModelFilterDisplayList(fist, 3, "GfistZ");
    assert(fist[1].words.w0 == 0xb1000000 && fist[1].words.w1 == 0);
    assert(fist[0].words.w1 == 0x5ea && fist[2].words.w0 == 0xb8000000);
}
static void checkSnapReach(void)
{
    const float levels[] = {0.2f, 1};
    const float sizes[] = {0.5f, 1, 2};
    for (int level = 0; level < 2; level++)
    for (int cheat = 0; cheat < 3; cheat++)
    for (int turn = 0; turn < 3; turn++) {
        D_800364CC = levels[level]; size = sizes[cheat];
        float cm = GEVR_UNITS_PER_METRE * D_800364CC / 100;
        coord3d angles = {.x = turn * 0.4f, .y = turn * -0.7f, .z = turn * 1.1f};
        matrix_4x4_set_rotation_around_xyz(&angles, &poses[1]);
        float distances[] = {16.1f, 15.9f, 18, 21.9f, 22.1f, 18, 15.9f};
        int expected[] = {0, 1, 1, 1, 0, 0, 1};
        s_gevrWatchGrip = 0;
        for (int step = 0; step < 7; step++) {
            for (int i = 0; i < 3; i++) poses[1].m[3][i] = watchPoint[i] + poses[1].m[0][i] * cm * distances[step];
            float distance;
            assert(gevrStereoWatchGripUpdate(1, &distance) == expected[step]);
            closeEnough(distance, distances[step]);
        }
        assert(!gevrStereoWatchGripUpdate(0, NULL));
        havePose[1] = 0; assert(!gevrStereoWatchGripUpdate(1, NULL)); havePose[1] = 1;
        haveWatchPoint = 0; assert(!gevrStereoWatchGripUpdate(1, NULL)); haveWatchPoint = 1;
    }
    online = 1; playerSlot = 1; localSlot = 0;
    s_gevrWatchGrip = 0;
    float distance = -1;
    assert(gevrStereoWatchGripUpdate(1, &distance)); /* remote firing uses its owner's input */
    assert(distance == 0 && s_gevrWatchGrip == 0);
    online = playerSlot = 0;
}
int main(void)
{
    const float scales[] = {0.5f, 1, 2};
    const float levels[] = {0.2f, 1};
    const float trims[][7] = {{0, 0, 0, 0, 0, 0, 1}, {2, -4, 3, 22, -31, 83, 1}};
    /* The shipped primary hand's canonical orientation, before #60.
     * Locking its position must not give it the taser hand's 90-degree turn. */
    const float original[3][3] = {{-0.0062f, -0.89360036f, 0.4488f},
                                {-0.865f, -0.22037904f, -0.4508f},
                                {0.5017f, -0.39100696f, -0.7716f}};
    assert(memcmp(s_gevrWatchHandTrim, trims[0], sizeof(s_gevrWatchHandTrim)) == 0);
    coord3d identity = {.x = 0, .y = 0, .z = 0};
    matrix_4x4_set_rotation_around_xyz(&identity, &poses[0]);
    Mtxf baseline = snap();
    for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) closeEnough(baseline.m[i][j], original[i][j] * GEVR_VIEWMODEL_CM * 0.1f);
    for (int mirrored = 0; mirrored < 2; mirrored++)
    for (int scale = 0; scale < 3; scale++)
    for (int level = 0; level < 2; level++)
    for (int turn = 0; turn < 3; turn++)
    for (int trim = 0; trim < 2; trim++) {
        VrLeftHandedMode = mirrored;
        size = scales[scale]; D_800364CC = levels[level];
        memcpy(s_gevrWatchHandTrim, trims[trim], sizeof(s_gevrWatchHandTrim));
        coord3d angles = {.x = turn*0.41f, .y = turn*-0.72f, .z = turn*1.17f};
        matrix_4x4_set_rotation_around_xyz(&angles, &poses[0]);
        poses[0].m[3][0] = turn*23; poses[0].m[3][1] = -17; poses[0].m[3][2] = 36;
        poses[1] = poses[0];
        Mtxf attached = snap();
        checkFinger(&attached);
        float multiplier = size * D_800364CC;
        for (int i = 0; i < 3; i++) {
            float length2 = dot(baseline.m[i], baseline.m[i]) * multiplier * multiplier;
            assert(fabsf(dot(attached.m[i], attached.m[i]) - length2) < length2 * 0.00001f);
        }
        assert(determinant(&attached) * determinant(&baseline) * (mirrored ? -1 : 1) > 0);
        float o[3], x[3], y[3], z[3];
        assert(gevrWatchFaceFrame(o, x, y, z));
        float cm = GEVR_UNITS_PER_METRE * D_800364CC / 100 * size;
        for (int i = 0; i < 3; i++) {
            float anchor = attached.m[3][i];
            for (int j = 0; j < 3; j++) anchor += s_gevrLaserFace[j]*attached.m[j][i];
            closeEnough(anchor, o[i] + cm*(s_gevrWatchHandTrim[0]*x[i] + s_gevrWatchHandTrim[1]*y[i] + s_gevrWatchHandTrim[2]*z[i]));
        }
        angles.x += 1.4f;
        matrix_4x4_set_rotation_around_xyz(&angles, &poses[1]);
        poses[1].m[3][0] = 500;
        Mtxf stillAttached = snap();
        assert(memcmp(&attached, &stillAttached, sizeof(attached)) == 0);
    }
    Mtxf before = snap(), failed = before;
    havePose[0] = 0;
    assert(!gevrStereoWatchHandMatrix(&failed));
    assert(memcmp(&before, &failed, sizeof(before)) == 0);
    havePose[0] = 1; g_gevrStereo = 0;
    assert(!gevrStereoWatchHandMatrix(&failed));
    assert(memcmp(&before, &failed, sizeof(before)) == 0);
    ModelNode nodes[GEVR_WATCHHAND_DLS], *lists[GEVR_WATCHHAND_DLS];
    union ModelRoData data[GEVR_WATCHHAND_DLS];
    Gfx displayLists[2 * GEVR_WATCHHAND_DLS];
    memset(nodes, 0, sizeof(nodes)); memset(data, 0, sizeof(data));
    /* Distinct source identities: left=566, right=886, finger=104. The six
     * sleeves belong to the right arm too. Rotate traversal order each time. */
    const int counts[GEVR_WATCHHAND_DLS] = {566, 24, 73, 28, 24, 52, 52, 886, 104};
    for (int order = 0; order < GEVR_WATCHHAND_DLS; order++) {
    for (int i = 0; i < GEVR_WATCHHAND_DLS; i++) {
        lists[i] = &nodes[i]; nodes[i].Data = &data[i];
        data[i].DisplayList.numVertices = counts[(i + order) % GEVR_WATCHHAND_DLS];
        data[i].DisplayList.Primary = &displayLists[2*i];
        data[i].DisplayList.Secondary = &displayLists[2*i+1];
    }
    assert(gevrWatchHandHideLeft(lists));
    for (int i = 0; i < GEVR_WATCHHAND_DLS; i++) {
        if (data[i].DisplayList.numVertices != 566) {
            assert(data[i].DisplayList.Primary == &displayLists[2*i]);
            assert(data[i].DisplayList.Secondary == &displayLists[2*i+1]);
        } else {
            assert(data[i].DisplayList.Primary == NULL);
            assert(data[i].DisplayList.Secondary == NULL);
        }
    }
    }
    /* An unexpected model must fail before changing any display lists. */
    for (int i = 0; i < GEVR_WATCHHAND_DLS; i++) {
        data[i].DisplayList.numVertices = 1;
        data[i].DisplayList.Primary = &displayLists[2*i];
    }
    assert(!gevrWatchHandHideLeft(lists));
    data[0].DisplayList.numVertices = data[1].DisplayList.numVertices = 566;
    assert(!gevrWatchHandHideLeft(lists));
    for (int i = 0; i < GEVR_WATCHHAND_DLS; i++) assert(data[i].DisplayList.Primary == &displayLists[2*i]);
    g_gevrStereo = 1;
    for (int item = 0; item < 2; item++) {
        int weapon = item ? ITEM_TRIGGER : ITEM_WATCHLASER;
        player.hands[GUNRIGHT].weaponnum = weapon;
        player.hands[GUNRIGHT].field_A84 = 0;
        player.hands[GUNRIGHT].weapon_hold_time = 1;
        float limit = get_value_if_watch_is_on_hand_or_not(GUNRIGHT);
        for (int tick = 0; tick < 8; tick++) {
            float before = player.hands[GUNRIGHT].field_A84;
            gevrWatchHandTickHiddenFinger(GUNRIGHT, weapon, 0);
            closeEnough(player.hands[GUNRIGHT].field_A84, fminf(limit, before + 0.029088823f));
        }
        closeEnough(player.hands[GUNRIGHT].field_A84, limit);
        player.hands[GUNRIGHT].weapon_hold_time = 0;
        for (int tick = 0; tick < 8; tick++) {
            float before = player.hands[GUNRIGHT].field_A84;
            gevrWatchHandTickHiddenFinger(GUNRIGHT, weapon, 0);
            closeEnough(player.hands[GUNRIGHT].field_A84, fmaxf(0, before - 0.017453294f));
        }
        assert(player.hands[GUNRIGHT].field_A84 == 0);
        player.hands[GUNRIGHT].weapon_hold_time = 1;
        gevrWatchHandTickHiddenFinger(GUNRIGHT, weapon, 1); /* normal model already advances it */
        gevrWatchHandTickHiddenFinger(GUNRIGHT, ITEM_WPPK, 0);
        gevrWatchHandTickHiddenFinger(GUNLEFT, weapon, 0);
        g_gevrStereo = 0; gevrWatchHandTickHiddenFinger(GUNRIGHT, weapon, 0); g_gevrStereo = 1;
        assert(player.hands[GUNRIGHT].field_A84 == 0);
    }
    checkWatchFaces();
    checkSnapReach();
    puts("watch grip: wrist/finger poses, palm/sleeves, press/release, private watch-face filtering and 16/22 cm snap hysteresis passed");
    return 0;
}
'''
fixture = fixture.replace("/* DEFINITIONS */", definitions).replace("/* PRODUCTION */", production)
with tempfile.TemporaryDirectory(prefix="gevr-watch-grip-") as temp:
    source = Path(temp) / "watch_grip.c"
    exe = Path(temp) / "watch_grip.exe"
    source.write_text(fixture, encoding="utf-8")
    args = [shutil.which("gcc") or "gcc", "-std=gnu11", "-O2", "-D_LANGUAGE_C=1",
            "-Wno-builtin-declaration-mismatch"]
    args += ["-I" + str(ROOT / folder) for folder in (".", "include", "src", "port/include")]
    subprocess.run(args + [str(source), "-o", str(exe), "-lm"], check=True)
    subprocess.run([str(exe)], check=True)
