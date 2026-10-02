"""Native checks of production watch attachment math and GripWatch persistence."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
view = (ROOT / "src/game/bondview2.c").read_text(encoding="utf-8")
math = (ROOT / "src/game/matrixmath.c").read_text(encoding="utf-8")
settings = (ROOT / "port/vr/vr_settings.cpp").read_text(encoding="utf-8")
defaults = (ROOT / "port/vr/vr_settings_defaults.c").read_text(encoding="utf-8")


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
definitions += "\n" + re.search(r"static const f32 s_gevrTwoHandPalm\[3\] = [^;]+;", view).group()
definitions += "\n" + re.search(r"float VrWatchGripTrim\[6\] = [^;]+;", defaults).group()
production = "\n".join((
    function(math, "void matrix_4x4_set_rotation_around_xyz("),
    function(math, "void matrix_scalar_multiply("),
    function(view, "s32 gevrStereoGunMatrix("),
    function(view, "static s32 gevrWatchFaceFrame("),
    function(view, "s32 gevrStereoWatchGripMatrix("),
))
load_start = settings.index('        if (strncmp(line, "GripWatch=", 10) == 0)')
load_end = settings.index('        if (strncmp(line, "Cheats=", 7)', load_start)
save_start = settings.index('    fprintf(f, "GripWatch=')
save_end = settings.index(';', save_start) + 1

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
static void load(const char *line)
{
    for (int once = 0; once < 1; once++) {
        /* LOAD */
    }
}
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
    assert(gevrStereoGunMatrix(GUNLEFT, &m));
    matrix_scalar_multiply(0.1f, m.m[0]);
    assert(gevrStereoWatchGripMatrix(&m));
    return m;
}
int main(void)
{
    const float scales[] = {0.5f, 1, 2};
    const float levels[] = {0.2f, 1};
    const float trims[][6] = {{0, -5, 0, 90, 0, 0}, {2, -4, 3, 22, -31, 83}};
    assert(memcmp(VrWatchGripTrim, trims[0], sizeof(VrWatchGripTrim)) == 0);
    load("GunOffX=0\n");
    load("GripWatch=1 2 3\n");
    assert(memcmp(VrWatchGripTrim, trims[0], sizeof(VrWatchGripTrim)) == 0);
    for (int mirrored = 0; mirrored < 2; mirrored++)
    for (int scale = 0; scale < 3; scale++)
    for (int level = 0; level < 2; level++)
    for (int turn = 0; turn < 3; turn++)
    for (int trim = 0; trim < 2; trim++) {
        VrLeftHandedMode = mirrored;
        size = scales[scale]; D_800364CC = levels[level];
        memcpy(VrWatchGripTrim, trims[trim], sizeof(VrWatchGripTrim));
        coord3d angles = {.x = turn*0.41f, .y = turn*-0.72f, .z = turn*1.17f};
        matrix_4x4_set_rotation_around_xyz(&angles, &poses[0]);
        poses[0].m[3][0] = turn*23; poses[0].m[3][1] = -17; poses[0].m[3][2] = 36;
        poses[1] = poses[0];
        Mtxf regular, attached = snap();
        assert(gevrStereoGunMatrix(GUNRIGHT, &regular));
        matrix_scalar_multiply(0.1f, regular.m[0]);
        for (int i = 0; i < 3; i++) {
            float length2 = dot(regular.m[i], regular.m[i]);
            assert(fabsf(dot(attached.m[i], attached.m[i]) - length2) < length2 * 0.00001f);
            for (int j = i+1; j < 3; j++) assert(fabsf(dot(attached.m[i], attached.m[j])) < length2 * 0.00001f);
        }
        assert(determinant(&attached) * determinant(&regular) > 0);
        float o[3], x[3], y[3], z[3];
        assert(gevrWatchFaceFrame(o, x, y, z));
        float cm = GEVR_UNITS_PER_METRE * D_800364CC / 100 * size;
        for (int i = 0; i < 3; i++) {
            float palm = attached.m[3][i];
            for (int j = 0; j < 3; j++) palm += s_gevrTwoHandPalm[j]*attached.m[j][i];
            closeEnough(palm, o[i] + cm*(VrWatchGripTrim[0]*x[i] + VrWatchGripTrim[1]*y[i] + VrWatchGripTrim[2]*z[i]));
        }
        angles.x += 1.4f;
        matrix_4x4_set_rotation_around_xyz(&angles, &poses[1]);
        poses[1].m[3][0] = 500;
        Mtxf stillAttached = snap();
        assert(memcmp(&attached, &stillAttached, sizeof(attached)) == 0);
        assert(gevrStereoGunMatrix(GUNRIGHT, &regular));
        closeEnough(regular.m[3][0], 500 + ((mirrored ? -VrGunOffX : VrGunOffX)*poses[1].m[0][0]
                    + VrGunOffY*poses[1].m[1][0] + (GEVR_GRIP_TO_ORIGIN_CM+VrGunOffZ)*poses[1].m[2][0])*cm);
    }
    Mtxf before = snap(), failed = before;
    havePose[0] = 0;
    assert(!gevrStereoWatchGripMatrix(&failed));
    assert(memcmp(&before, &failed, sizeof(before)) == 0);
    havePose[0] = 1; g_gevrStereo = 0;
    assert(!gevrStereoWatchGripMatrix(&failed));
    assert(memcmp(&before, &failed, sizeof(before)) == 0);
    load("GripWatch=2 -4 3 22 -31 83\n");
    float saved[6]; memcpy(saved, VrWatchGripTrim, sizeof(saved));
    load("GripWatch=1 2 3\n");
    load("GunOffX=0\n");
    assert(memcmp(saved, VrWatchGripTrim, sizeof(saved)) == 0);
    FILE *f = tmpfile(); assert(f);
    /* SAVE */
    rewind(f); char line[128]; assert(fgets(line, sizeof(line), f)); fclose(f);
    memset(VrWatchGripTrim, 0, sizeof(VrWatchGripTrim)); load(line);
    assert(memcmp(saved, VrWatchGripTrim, sizeof(saved)) == 0);
    puts("watch grip: 72 attachment cases, scale, handedness, release, tracking loss and settings passed");
    return 0;
}
'''
fixture = fixture.replace("/* DEFINITIONS */", definitions).replace("/* PRODUCTION */", production)
fixture = fixture.replace("/* LOAD */", settings[load_start:load_end])
fixture = fixture.replace("/* SAVE */", settings[save_start:save_end])
with tempfile.TemporaryDirectory(prefix="gevr-watch-grip-") as temp:
    source = Path(temp) / "watch_grip.c"
    exe = Path(temp) / "watch_grip.exe"
    source.write_text(fixture, encoding="utf-8")
    args = [shutil.which("gcc") or "gcc", "-std=gnu11", "-O2", "-D_LANGUAGE_C=1",
            "-Wno-builtin-declaration-mismatch"]
    args += ["-I" + str(ROOT / folder) for folder in (".", "include", "src", "port/include")]
    subprocess.run(args + [str(source), "-o", str(exe), "-lm"], check=True)
    subprocess.run([str(exe)], check=True)
