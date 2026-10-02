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
production = "\n".join((
    function(math, "void matrix_4x4_set_rotation_around_xyz("),
    function(view, "static s32 gevrWatchFaceFrame("),
    function(view, "s32 gevrStereoWatchHandMatrix("),
    function(gun, "static void gevrWatchHandHideLeft("),
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
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define sysLogPrintf(...) ((void)0)
#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif
static int g_gevrStereo = 1, VrLeftHandedMode, havePose[2] = {1, 1};
static float D_800364CC = 1, size = 1;
static float VrGunOffX = 2.74f, VrGunOffY = 1.94f, VrGunOffZ = -12.35f;
static int s_gevrWatchFaceKnown = 1;
static float s_gevrWatchFaceCm[3] = {1.5f, 0.7f, -0.2f};
static Mtxf poses[2];
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
    for (int i = 0; i < GEVR_WATCHHAND_DLS; i++) {
        lists[i] = &nodes[i]; nodes[i].Data = &data[i];
        data[i].DisplayList.Primary = &displayLists[2*i];
        data[i].DisplayList.Secondary = &displayLists[2*i+1];
    }
    gevrWatchHandHideLeft(lists);
    for (int i = 0; i < GEVR_WATCHHAND_DLS; i++) {
        if (i == 0 || i == 8) {
            assert(data[i].DisplayList.Primary == &displayLists[2*i]);
            assert(data[i].DisplayList.Secondary == &displayLists[2*i+1]);
        } else {
            assert(data[i].DisplayList.Primary == NULL);
            assert(data[i].DisplayList.Secondary == NULL);
        }
    }
    puts("watch grip: original right-hand pose, 72 wrist attachments, scale, handedness, tracking loss and left-model hiding passed");
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
