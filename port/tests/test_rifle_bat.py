"""The rifle bat in a PP7's hand (Bean's or GoldenEye's own) held with both hands.

Compiles gun.c's production gevrBeanBatPose and bondview2.c's
gevrBatOffHandPin against stub hands, and checks the two-handed hold as a
stick held in two hands (owner, 2026-11-22, headset): taking hold moves
nothing; the line between the two controllers steers it, so both hands
swinging together turn it with them, and a push of one hand past the other
turns it a lot with the hands together and little with them apart ("they
should be easier to break the wrists when my hands are together"); it turns
about the fist, the glove with it; and the off hand is stuck to it, turning
with the bat, not with its own controller, its grip point on the bat's line.

Usage: python port/tests/test_rifle_bat.py
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


gun = (ROOT / 'src/game/gun.c').read_text(encoding='utf-8')
view = (ROOT / 'src/game/bondview2.c').read_text(encoding='utf-8')
definitions = re.findall(r'^#define GEVR_(?:BEAN|GEX)_BAT_\w+[^\n]*', gun, re.M)
definitions.append(re.search(r'static const f32 s_gevrBeanBatPalm\[3\] = [^;]+;', gun).group())
definitions.extend(re.findall(r'^static (?:f32|s32|unsigned) s_gevrBeanBatGrip\w*\[[^;]+;', gun, re.M))
definitions.extend(re.findall(r'^static (?:f32|s32) s_gevrBatOffGrip\w*(?:\[[^;]+)?;', view, re.M))
production = [function(gun, signature) for signature in (
    'static f32 gevrGexBatHandle(void)', 'static void gevrBeanBatTurn(', 'static void gevrBeanBatIn(',
    'static void gevrBeanBatOut(', 'static s32 gevrBeanBatUnit(', 'static s32 gevrBeanBatLineFrame(',
    's32 gevrBeanBatPose(s32 hand, Mtxf *rifle)', 's32 gevrGexBatFrame(s32 hand,')]
production.append(function(view, 'static void gevrBatOffHandPin(Mtxf *matrices, s32 n, const f32 x[3], const f32 y[3], const f32 z[3])\n{'))

HARNESS = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef float f32;
typedef int s32;
typedef unsigned int u32;
typedef struct { f32 m[4][4]; } Mtxf;
#define TRUE 1
#define FALSE 0
enum { GUNRIGHT, GUNLEFT };
#define ITEM_WPPK 4
#define LOG_NOTE 0
static void sysLogPrintf(int level, const char *fmt, ...) { (void)level; (void)fmt; }
s32 g_GlobalTimer = 1;
static unsigned snapshot = 1;
unsigned gevrVrGripSnapshotId(void) { return snapshot; }
float VrBeanGunOff[3], VrGunOffX, VrGunOffY, VrGunOffZ;
int gevrBeanIsEnabled(void) { return 1; }
static const f32 *gevrGunOffOwnOf(int look, int item) { (void)look; (void)item; return 0; }
static f32 batFit[3];                       /* BeanBat: the fist along the rifle */
float *gevrBatFit(void) { return batFit; }
static Mtxf gunHand;                        /* the PP7's gun matrix in the gun hand */
s32 gevrStereoGunMatrixFit(s32 h, const f32 off[3], Mtxf *out) { (void)h; (void)off; *out = gunHand; return TRUE; }
static f32 offAt[3], offAmt;                /* the off hand's grip point and the hold's ease */
static f32 between[3];                      /* the gun controller from the off one */
static const f32 unit = 0.1f;               /* rows of length 1: a model unit is a tenth */
static f32 cm(f32 c) { return c / 0.085f * unit; }
s32 gevrStereoBatOffHand(s32 h, f32 opos[3], f32 *amt)
{
    (void)h;
    if (offAmt <= 0.0f) return FALSE;
    memcpy(opos, offAt, sizeof(offAt));
    *amt = offAmt;
    return TRUE;
}
s32 gevrStereoBatBetween(s32 h, f32 out[3], f32 *apartCm)
{
    (void)h;
    if (offAmt <= 0.0f) return FALSE;
    memcpy(out, between, sizeof(between));
    *apartCm = sqrtf(between[0] * between[0] + between[1] * between[1] + between[2] * between[2]) / cm(1);
    return TRUE;
}
static f32 s_gevrGexBatEnd[2][3];
static s32 s_gevrGexBatAt[2];
static f32 s_gevrGexBatLine[2][15];
static Mtxf s_gevrBeanBatHand[2];
/* bondview2.c, for the off hand's pin */
static s32 s_gevrTwoHandGun = GUNRIGHT;
static f32 s_gevrTwoHandAmt;
s32 gevrStereoBatOffPinned(void) { return s_gevrTwoHandAmt > 0.001f; }
static s32 gevrBatOffPalm(s32 g, f32 out[3]) { (void)g; memcpy(out, offAt, sizeof(offAt)); return TRUE; }
/* DEFINITIONS */
/* PRODUCTION */

static f32 dot(const f32 *a, const f32 *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static f32 deg(const f32 *a, const f32 *b)
{
    const double c = dot(a, b) / sqrt((double)dot(a, a) * dot(b, b));
    return (f32)(acos(c > 1.0 ? 1.0 : c) * 57.29577951);
}
/* about X by a: the swing's plane */
static void turnX(f32 a, const f32 in[3], f32 out[3])
{
    const f32 r = a / 57.29578f, c = cosf(r), s = sinf(r), y = in[1], z = in[2];
    out[0] = in[0]; out[1] = y * c - z * s; out[2] = y * s + z * c;
}
static void setGun(f32 a)
{
    const f32 rows[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    memset(&gunHand, 0, sizeof(gunHand));
    for (int r = 0; r < 3; r++) turnX(a, rows[r], gunHand.m[r]);
    gunHand.m[3][3] = 1.0f;
}
static void palmOf(const Mtxf *h, f32 out[3])
{
    f32 len[3];
    for (int r = 0; r < 3; r++) len[r] = sqrtf(dot(h->m[r], h->m[r]));
    for (int i = 0; i < 3; i++)
        out[i] = h->m[3][i] + (s_gevrBeanBatPalm[0] * h->m[0][i] / len[0] + s_gevrBeanBatPalm[1] * h->m[1][i] / len[1]
                               + s_gevrBeanBatPalm[2] * h->m[2][i] / len[2]) * unit;
}
/* how far p is from the rifle's line as drawn, cm */
static f32 offLine(const f32 p[3])
{
    const f32 *at = s_gevrGexBatLine[GUNRIGHT], *dir = s_gevrGexBatLine[GUNRIGHT] + 3;
    f32 d[3], a, e2 = 0, l = sqrtf(dot(dir, dir));
    for (int i = 0; i < 3; i++) d[i] = p[i] - at[i];
    a = dot(d, dir) / l;
    for (int i = 0; i < 3; i++) e2 += (d[i] - a * dir[i] / l) * (d[i] - a * dir[i] / l);
    return sqrtf(e2) / unit * 0.085f;
}
static Mtxf rifle;
static const f32 *line(void) { return s_gevrGexBatLine[GUNRIGHT] + 3; }
static void tick(void) { g_GlobalTimer++; snapshot++; assert(gevrBeanBatPose(GUNRIGHT, &rifle)); }
static void letGo(void) { offAmt = 0; for (int k = 0; k < 6; k++) tick(); offAmt = 1; }
/* the controllers apart along v (cm), and pushed across by `across` cm */
static void hands(const f32 v[3], f32 apartCm, const f32 x[3], f32 across)
{
    for (int i = 0; i < 3; i++) between[i] = v[i] * cm(apartCm) + x[i] * cm(across);
}

int main(void)
{
    f32 palm[3], ey[3], ex[3] = { 1, 0, 0 }, glovePalm[3], want[3], first[3];

    batFit[1] = -7.262f;   /* the owner's Bean fit: the fist ~18 cm up from the muzzle */
    setGun(0);
    palmOf(&gunHand, palm);
    for (int i = 0; i < 3; i++) ey[i] = GEVR_BEAN_BAT_GRIP_Y * gunHand.m[1][i] + GEVR_BEAN_BAT_GRIP_Z * gunHand.m[2][i];
    {
        const f32 l = sqrtf(dot(ey, ey));
        for (int i = 0; i < 3; i++) ey[i] /= l;
    }
    for (int i = 0; i < 3; i++) offAt[i] = palm[i] - ey[i] * cm(10);

    /* one hand: the fist's own line, through the fist */
    offAmt = 0;
    tick();
    assert(deg(line(), ey) < 0.05f && offLine(palm) < 0.01f);

    /* both hands on, the controllers stacked 12 cm apart, a little off the bat: taking hold moves nothing */
    offAmt = 1;
    {
        f32 v[3];
        turnX(-10, ey, v);
        hands(v, 12, ex, 0);
    }
    tick();
    assert(deg(line(), ey) < 0.05f);
    /* its end in the off hand: the muzzle 5 cm past it along the rifle */
    assert(fabsf(s_gevrGexBatLine[GUNRIGHT][6] / unit - (GEVR_GEX_BAT_OFF + cm(10) / unit)) < 1.0f);

    /* both hands swing it together, 30 degrees: it turns with them */
    letGo();
    hands(ey, 12, ex, 0);
    tick();
    setGun(30);
    palmOf(&gunHand, palm);
    turnX(30, ey, want);
    hands(want, 12, ex, 0);
    tick();
    assert(deg(line(), want) < 0.1f);
    /* ... about the fist, the glove turned with it, its grip point where the hand is */
    palmOf(&s_gevrBeanBatHand[GUNRIGHT], glovePalm);
    for (int i = 0; i < 3; i++) assert(fabsf(glovePalm[i] - palm[i]) / unit * 0.085f < 0.01f);
    for (int i = 0; i < 3; i++) assert(fabsf(s_gevrGexBatLine[GUNRIGHT][i] - palm[i]) < 1e-5f);

    /* hands together (12 cm), the top hand pushed 3 cm past the bottom one, the fist not
     * turned: the wrists break it, atan(3/12) = 14 degrees */
    letGo();
    setGun(0);
    palmOf(&gunHand, palm);
    hands(ey, 12, ex, 0);
    tick();
    hands(ey, 12, ex, 3);
    tick();
    assert(fabsf(deg(line(), ey) - 14.04f) < 0.2f && dot(line(), ex) > 0.0f);
    /* hands apart (40 cm), the same push: stiff, atan(3/40) = 4.3 degrees */
    letGo();
    hands(ey, 40, ex, 0);
    tick();
    hands(ey, 40, ex, 3);
    tick();
    assert(fabsf(deg(line(), ey) - 4.29f) < 0.2f);
    /* ... and slid together again, the push breaks it as before */
    hands(ey, 12, ex, 3);
    tick();
    assert(fabsf(deg(line(), ey) - 14.04f) < 0.2f);

    /* every draw of one controller snapshot agrees (each eye) */
    memcpy(first, line(), sizeof(first));
    assert(gevrBeanBatPose(GUNRIGHT, &rifle));
    assert(deg(line(), first) < 0.05f);

    /* controllers too near (3 cm) for a line: it keeps where it was on the fist */
    hands(ey, 3, ex, 0);
    tick();
    assert(deg(line(), first) < 0.1f);

    /* let go and taken again: a new hold, from the fist's own line */
    letGo();
    hands(ey, 12, ex, 5);
    tick();
    assert(deg(line(), ey) < 0.05f);

    /* the off hand stuck to the bat (bondview2.c gevrBatOffHandPin) */
    {
        Mtxf arm[2], before[2];
        const f32 x[3] = { 0, 0, 1 }, y[3] = { -1, 0, 0 }, z[3] = { 0, -1, 0 };   /* its wrist on the controller */
        f32 batNow[3][3], at[3], grip[3], g[3];

        s_gevrTwoHandAmt = 1;
        letGo();
        setGun(0);
        palmOf(&gunHand, palm);
        for (int i = 0; i < 3; i++) offAt[i] = palm[i] - ey[i] * cm(10) + ex[i] * cm(1);
        hands(ey, 12, ex, 0);
        tick();
        memset(arm, 0, sizeof(arm));
        for (int j = 0; j < 2; j++)
        {
            for (int i = 0; i < 3; i++) { arm[j].m[0][i] = x[i] * 0.5f; arm[j].m[1][i] = y[i] * 0.5f; arm[j].m[2][i] = z[i] * 0.5f; }
            for (int i = 0; i < 3; i++) arm[j].m[3][i] = offAt[i] + x[i] * (j ? 0.3f : -0.6f);
            arm[j].m[3][3] = 1;
        }
        memcpy(before, arm, sizeof(arm));
        gevrBatOffHandPin(arm, 2, x, y, z);   /* taking hold: turned by nothing */
        for (int r = 0; r < 3; r++) for (int i = 0; i < 3; i++) assert(fabsf(arm[0].m[r][i] - before[0].m[r][i]) < 1e-5f);
        /* the swing turns the bat 20 degrees and the off controller does not turn: the hand turns with the bat */
        setGun(20);
        palmOf(&gunHand, palm);
        turnX(20, ey, want);
        hands(want, 12, ex, 0);
        tick();
        memcpy(arm, before, sizeof(arm));
        gevrBatOffHandPin(arm, 2, x, y, z);
        {
            f32 rowWas[3], rowNow[3];
            for (int i = 0; i < 3; i++) { rowWas[i] = before[0].m[0][i]; rowNow[i] = arm[0].m[0][i]; }
            turnX(20, rowWas, g);
            assert(deg(rowNow, g) < 0.2f);   /* with the bat, not its controller (0) */
        }
        /* its grip point on the bat's line: the arm carried it rigidly */
        assert(gevrGexBatFrame(GUNRIGHT, at, batNow[0], batNow[1], batNow[2]));
        {
            f32 d[3], local[3];
            for (int i = 0; i < 3; i++) d[i] = offAt[i] - before[0].m[3][i];
            for (int k = 0; k < 3; k++) local[k] = dot(d, k == 0 ? x : k == 1 ? y : z);
            for (int i = 0; i < 3; i++)
                grip[i] = arm[0].m[3][i] + (local[0] * arm[0].m[0][i] + local[1] * arm[0].m[1][i] + local[2] * arm[0].m[2][i]) / 0.5f;
            assert(offLine(grip) < 0.05f);
        }
    }
    puts("PASS: the two-handed rifle bat follows the controllers' line: taking hold moves nothing, it swings with both hands, a push breaks it 14 degrees with the hands together and 4 apart, it turns about the fist with the glove, and the off hand is stuck to it");
    return 0;
}
'''

fixture = HARNESS.replace('/* DEFINITIONS */', '\n'.join(definitions)).replace('/* PRODUCTION */', '\n'.join(production))
with tempfile.TemporaryDirectory(prefix='gevr-rifle-bat-') as temp:
    source, exe = Path(temp) / 'test.c', Path(temp) / 'test'
    source.write_text(fixture, encoding='utf-8')
    subprocess.run([shutil.which('gcc') or 'gcc', '-std=c11', '-O2', str(source), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
