/* Native checks of port/src/gevr_bodyslot.c, the body slots' arithmetic. */
#include "gevr_bodyslot.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)
#define NEAR(a, b, eps) (fabsf((a) - (b)) <= (eps))

static void pitched(float pitchDeg, float yawDeg, float fwd[3], float up[3])
{
    /* yaw about +Y from +Z, then pitch (negative looks down) */
    const float p = pitchDeg * 3.14159265f / 180.0f, y = yawDeg * 3.14159265f / 180.0f;

    fwd[0] = sinf(y) * cosf(p); fwd[1] = sinf(p); fwd[2] = cosf(y) * cosf(p);
    up[0] = -sinf(y) * sinf(p); up[1] = cosf(p); up[2] = -cosf(y) * sinf(p);
}

static void cross(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void heading(void)
{
    float f[3], u[3], h[2];
    float pitches[] = { 0.0f, -45.0f, -89.0f, -90.0f, 45.0f, 89.0f, 90.0f };
    int i;

    for (i = 0; i < 7; i++)
    {
        pitched(pitches[i], 30.0f, f, u);
        CHECK(gevrBodyHeading(f, u, h));
        CHECK(NEAR(h[0], sinf(30.0f * 3.14159265f / 180.0f), 1e-4f) && NEAR(h[1], cosf(30.0f * 3.14159265f / 180.0f), 1e-4f));
    }
    {
        const float a[2] = { 0.0f, 1.0f };
        float b[2];

        gevrBodyTurn(a, 30.0f, b);
        CHECK(NEAR(gevrBodyAngle(a, b), 30.0f, 1e-3f));
        gevrBodyTurn(a, -170.0f, b);
        CHECK(NEAR(gevrBodyAngle(a, b), -170.0f, 1e-3f));
    }
}

static void torso(void)
{
    GevrBodyTorso t;
    const float ahead[2] = { 0.0f, 1.0f };
    float head[2];
    int i;

    gevrBodyTorsoReset(&t);
    CHECK(gevrBodyTorsoUpdate(&t, ahead, 0.02f, 1.0f, 0, 60.0f, 45.0f) == 1);   /* first frame */
    gevrBodyTurn(ahead, 30.0f, head);
    CHECK(gevrBodyTorsoUpdate(&t, head, 0.0f, 1.0f, 0, 60.0f, 45.0f) == 0);
    CHECK(NEAR(gevrBodyAngle(head, t.heading), -30.0f, 1e-3f));   /* a glance: the torso stays */
    for (i = 0; i < 60; i++)
    {
        gevrBodyTorsoUpdate(&t, head, 0.02f, 1.0f, 0, 60.0f, 45.0f);
    }
    /* Perfect Dark VR's chase: 0.98 a tick */
    CHECK(NEAR(gevrBodyAngle(head, t.heading), -30.0f * powf(0.98f, 60.0f), 0.05f));
    /* two ticks at once chase as far as two single ticks */
    {
        GevrBodyTorso a = t, b = t;

        gevrBodyTorsoUpdate(&a, head, 0.02f, 2.0f, 0, 60.0f, 45.0f);
        gevrBodyTorsoUpdate(&b, head, 0.02f, 1.0f, 0, 60.0f, 45.0f);
        gevrBodyTorsoUpdate(&b, head, 0.02f, 1.0f, 0, 60.0f, 45.0f);
        CHECK(NEAR(gevrBodyAngle(a.heading, b.heading), 0.0f, 1e-3f));
    }
    /* held: no chase */
    {
        const float before = gevrBodyAngle(head, t.heading);

        gevrBodyTorsoUpdate(&t, head, 0.02f, 1.0f, 1, 60.0f, 45.0f);
        CHECK(NEAR(gevrBodyAngle(head, t.heading), before, 1e-4f));
    }
    /* the head turning further than the twist allows drags the torso, even held */
    gevrBodyTorsoReset(&t);
    gevrBodyTorsoUpdate(&t, ahead, 0.0f, 1.0f, 0, 50.0f, 45.0f);
    gevrBodyTurn(ahead, 30.0f, head);
    gevrBodyTorsoUpdate(&t, head, 0.0f, 1.0f, 1, 50.0f, 45.0f);
    gevrBodyTurn(ahead, 60.0f, head);
    gevrBodyTorsoUpdate(&t, head, 0.0f, 1.0f, 1, 50.0f, 45.0f);
    CHECK(NEAR(gevrBodyAngle(head, t.heading), -50.0f, 1e-3f));
    /* a jump (a recentre) starts it again at the head */
    gevrBodyTurn(head, 100.0f, head);
    CHECK(gevrBodyTorsoUpdate(&t, head, 0.0f, 1.0f, 1, 50.0f, 45.0f) == 1);
    CHECK(NEAR(gevrBodyAngle(head, t.heading), 0.0f, 1e-4f));
}

/* the frame from a camera looking along yawDeg/pitchDeg, the torso at twist from the head */
static void frame(GevrBodyFrame *f, float yawDeg, float pitchDeg, float twist, int mirrored)
{
    GevrBodyTorso t;
    const float hp[2] = { 0.0f, 1.0f };
    float fw[3], up[3], rt[3];

    gevrBodyTorsoReset(&t);
    gevrBodyTorsoUpdate(&t, hp, 0.0f, 1.0f, 0, 90.0f, 1000.0f);
    gevrBodyTurn(hp, twist, t.heading);
    pitched(pitchDeg, yawDeg, fw, up);
    cross(fw, up, rt);
    if (mirrored)
    {
        rt[0] = -rt[0]; rt[1] = -rt[1]; rt[2] = -rt[2];
    }
    gevrBodyFrameBuild(f, &t, hp, fw, up, rt);
}

static void frames(void)
{
    GevrBodyFrame a, b;
    float fw[3], up[3], neckA[3], neckB[3], ca[3], cb[3], off[3];
    int i, s;

    /* upright, the torso with the head: the frame's eye is the eye */
    frame(&a, 0.0f, 0.0f, 0.0f, 0);
    CHECK(NEAR(a.origin[0], 0.0f, 1e-4f) && NEAR(a.origin[1], 0.0f, 1e-4f) && NEAR(a.origin[2], 0.0f, 1e-4f));
    CHECK(NEAR(a.fwd[2], 1.0f, 1e-5f) && NEAR(a.up[1], 1.0f, 1e-6f));
    /* right is the camera's right, levelled (fwd x up here: -X) */
    CHECK(NEAR(a.right[0], -1.0f, 1e-5f));
    frame(&b, 0.0f, 0.0f, 0.0f, 1);
    CHECK(NEAR(b.right[0], 1.0f, 1e-5f));   /* a mirrored camera keeps its own right */
    /* looking down 60 degrees, the slots stay put against the neck */
    frame(&b, 0.0f, -60.0f, 0.0f, 0);
    CHECK(NEAR(b.pitch, -60.0f, 1e-3f));
    for (i = 0; i < 3; i++)
    {
        pitched(0.0f, 0.0f, fw, up);
        neckA[i] = -GEVR_BODY_NECK_UP_CM * up[i] - GEVR_BODY_NECK_AHEAD_CM * fw[i];
        pitched(-60.0f, 0.0f, fw, up);
        neckB[i] = -GEVR_BODY_NECK_UP_CM * up[i] - GEVR_BODY_NECK_AHEAD_CM * fw[i];
    }
    for (s = 0; s < GEVR_BODY_SLOTS; s++)
    {
        gevrBodySlotDefault(s, 170.0f, off);
        gevrBodySlotCentre(&a, s, off, 0, ca);
        gevrBodySlotCentre(&b, s, off, 0, cb);
        for (i = 0; i < 3; i++)
        {
            CHECK(NEAR(ca[i] - neckA[i], cb[i] - neckB[i], 1e-3f));
        }
    }
    /* a stick turn (the camera's yaw) turns the torso with it */
    frame(&a, 0.0f, 0.0f, 20.0f, 0);
    frame(&b, 90.0f, 0.0f, 20.0f, 0);
    CHECK(NEAR(a.twist, 20.0f, 1e-3f) && NEAR(b.twist, 20.0f, 1e-3f));
    {
        const float fa[2] = { a.fwd[0], a.fwd[2] }, fb[2] = { b.fwd[0], b.fwd[2] };

        CHECK(NEAR(gevrBodyAngle(fa, fb), gevrBodyAngle((const float[2]) { 0.0f, 1.0f },
                                                         (const float[2]) { 1.0f, 0.0f }), 1e-3f));
    }
    /* the torso's heading in the level frame turns the same way as in play */
    {
        const float h[2] = { 0.0f, 1.0f }, ft[2] = { a.fwd[0], a.fwd[2] };

        CHECK(NEAR(gevrBodyAngle(h, ft), 20.0f, 1e-3f));
    }
}

static void slots(void)
{
    GevrBodyFrame f;
    float off[3], c[3], back[3], loc[3];
    int s, lefty;

    gevrBodySlotDefault(GEVR_BS_HIP_GUN, 170.0f, off);
    CHECK(NEAR(off[0], 64.6f, 1e-3f) && NEAR(off[1], 20.4f, 1e-3f) && NEAR(off[2], -10.2f, 1e-3f));
    gevrBodySlotDefault(GEVR_BS_HIP_GUN, 150.0f, off);
    CHECK(NEAR(off[0], 57.0f, 1e-3f));
    /* a fit replaces the default; 0 0 0 is no fit */
    {
        const float fit[3] = { 50.0f, 0.0f, 0.0f }, none[3] = { 0.0f, 0.0f, 0.0f };

        gevrBodySlotOffsets(GEVR_BS_CHEST, fit, 170.0f, off);
        CHECK(off[0] == 50.0f && off[1] == 0.0f);
        gevrBodySlotOffsets(GEVR_BS_CHEST, none, 170.0f, off);
        CHECK(NEAR(off[0], 0.23f * 170.0f, 1e-3f));
    }
    CHECK(NEAR(gevrBodySlotRadius(GEVR_BS_HIP_GUN, 1), 13.0f, 1e-5f));
    CHECK(NEAR(gevrBodySlotRadius(GEVR_BS_HIP_GUN, 0), 10.4f, 1e-4f));
    CHECK(NEAR(gevrBodySlotRadius(GEVR_BS_BACK_OFF, 2), 25.0f, 1e-4f));
    CHECK(gevrBodySlotHands(GEVR_BS_HIP_GUN) == 2 && gevrBodySlotHands(GEVR_BS_HIP_OFF) == 1
          && gevrBodySlotHands(GEVR_BS_CHEST) == 3);
    CHECK(gevrBodySlotCategory(GEVR_BS_BACK_GUN) == GEVR_BODY_CAT_RIFLES
          && gevrBodySlotCategory(GEVR_BS_BACK_OFF) == GEVR_BODY_CAT_HEAVY
          && gevrBodySlotCategory(GEVR_BS_BELT) == GEVR_BODY_CAT_GADGETS);
    /* the gun hand's hip on the right, mirrored left-handed; the belt on the off side */
    frame(&f, 0.0f, 0.0f, 0.0f, 0);
    gevrBodySlotDefault(GEVR_BS_HIP_GUN, 170.0f, off);
    gevrBodySlotCentre(&f, GEVR_BS_HIP_GUN, off, 0, c);
    gevrBodyLocal(&f, c, loc);
    CHECK(loc[1] > 20.0f && NEAR(loc[0], 64.6f, 1e-3f) && NEAR(loc[2], -10.2f, 1e-3f));
    gevrBodySlotCentre(&f, GEVR_BS_HIP_GUN, off, 1, c);
    gevrBodyLocal(&f, c, loc);
    CHECK(loc[1] < -20.0f);
    gevrBodySlotDefault(GEVR_BS_BELT, 170.0f, off);
    gevrBodySlotCentre(&f, GEVR_BS_BELT, off, 0, c);
    gevrBodyLocal(&f, c, loc);
    CHECK(loc[1] < 0.0f);
    /* fitting is the exact inverse of placing, for every slot and either hand */
    frame(&f, 37.0f, -25.0f, 15.0f, 0);
    for (lefty = 0; lefty < 2; lefty++)
    {
        for (s = 0; s < GEVR_BODY_SLOTS; s++)
        {
            const float want[3] = { 40.0f + s, 7.0f - s, -3.0f + 2.0f * s };

            gevrBodySlotCentre(&f, s, want, lefty, c);
            gevrBodySlotFitFrom(&f, s, c, lefty, back);
            CHECK(NEAR(back[0], want[0], 1e-3f) && NEAR(back[1], want[1], 1e-3f) && NEAR(back[2], want[2], 1e-3f));
        }
    }
}

static void zones(void)
{
    float dist[GEVR_BODY_SLOTS] = { 99, 99, 99, 99, 99, 99 };
    const float radius[GEVR_BODY_SLOTS] = { 10, 10, 20, 20, 10, 10 };
    int eligible[GEVR_BODY_SLOTS] = { 1, 1, 1, 1, 1, 1 };

    dist[0] = 9.9f;
    CHECK(gevrBodyZonePick(-1, dist, radius, eligible, 3.0f, 0.15f) == 0);
    dist[0] = 10.1f;
    CHECK(gevrBodyZonePick(-1, dist, radius, eligible, 3.0f, 0.15f) == -1);   /* enter inside the radius */
    dist[0] = 12.9f;
    CHECK(gevrBodyZonePick(0, dist, radius, eligible, 3.0f, 0.15f) == 0);     /* leave beyond it plus 3 cm */
    dist[0] = 13.1f;
    CHECK(gevrBodyZonePick(0, dist, radius, eligible, 3.0f, 0.15f) == -1);
    /* another slot takes over only when nearer by the margin */
    dist[0] = 8.0f;
    dist[4] = 7.0f;
    CHECK(gevrBodyZonePick(0, dist, radius, eligible, 3.0f, 0.15f) == 0);
    dist[4] = 6.0f;
    CHECK(gevrBodyZonePick(0, dist, radius, eligible, 3.0f, 0.15f) == 4);
    CHECK(gevrBodyZonePick(-1, dist, radius, eligible, 3.0f, 0.15f) == 4);    /* the nearest, in radii */
    eligible[4] = 0;
    CHECK(gevrBodyZonePick(4, dist, radius, eligible, 3.0f, 0.15f) == 0);     /* one with nothing for the hand isn't there */
}

static void choices(void)
{
    GevrBodyMru m = { { 0 }, 0 };
    const int cand[] = { 4, 5, 7, 19 };
    int out[8], n, i;

    for (i = 0; i < 12; i++)
    {
        gevrBodyMruTouch(&m, 100 + i);
    }
    CHECK(m.count == GEVR_BODY_MRU && m.items[0] == 111 && m.items[GEVR_BODY_MRU - 1] == 104);
    gevrBodyMruTouch(&m, 106);
    CHECK(m.items[0] == 106 && m.items[1] == 111 && m.count == GEVR_BODY_MRU);
    m.count = 0;
    gevrBodyMruTouch(&m, 4);
    gevrBodyMruTouch(&m, 19);
    gevrBodyMruTouch(&m, 5);
    /* holding 5 here: the others in the wheel's order, then put it away */
    n = gevrBodyChoices(cand, 4, 5, 1, out, 8);
    CHECK(n == 4 && out[0] == 4 && out[1] == 7 && out[2] == 19 && out[3] == GEVR_BODY_HOLSTER);
    /* the reach starts on the newest use the hand isn't holding */
    CHECK(gevrBodyDefaultPick(&m, out, n) == 19);
    CHECK(gevrBodyStepPick(out, n, 19, 1) == GEVR_BODY_HOLSTER);
    CHECK(gevrBodyStepPick(out, n, GEVR_BODY_HOLSTER, 1) == 4);
    CHECK(gevrBodyStepPick(out, n, 4, -1) == GEVR_BODY_HOLSTER);
    CHECK(gevrBodyStepPick(out, n, 99, 1) == 4);   /* gone: the first */
    /* holding something else: no holster choice */
    n = gevrBodyChoices(cand, 4, 26, 0, out, 8);
    CHECK(n == 4 && out[3] == 19);
    /* only what it holds: put it away is the one choice */
    n = gevrBodyChoices((const int[]) { 5 }, 1, 5, 1, out, 8);
    CHECK(n == 1 && out[0] == GEVR_BODY_HOLSTER && gevrBodyDefaultPick(&m, out, n) == GEVR_BODY_HOLSTER);
    CHECK(gevrBodyChoices(cand, 0, 1, 0, out, 8) == 0 && gevrBodyDefaultPick(&m, out, 0) == -1);
    /* a grip: draw, stow, nothing, or refused */
    CHECK(gevrBodyGripAction(7, 0) == GEVR_BODY_DRAW);
    CHECK(gevrBodyGripAction(GEVR_BODY_HOLSTER, 0) == GEVR_BODY_STOW);
    CHECK(gevrBodyGripAction(-1, 0) == GEVR_BODY_FALL && gevrBodyGripAction(-1, 1) == GEVR_BODY_FALL);
    CHECK(gevrBodyGripAction(7, 1) == GEVR_BODY_DENY && gevrBodyGripAction(GEVR_BODY_HOLSTER, 1) == GEVR_BODY_DENY);
    /* a throwable's grip goes to the slot only after a pause there */
    CHECK(!gevrBodyThrowGate(100.0f, 0.2f, 120.0f, 1.0f));
    CHECK(gevrBodyThrowGate(130.0f, 0.2f, 120.0f, 1.0f));
    CHECK(!gevrBodyThrowGate(500.0f, 1.5f, 120.0f, 1.0f));
}

static void stick(void)
{
    GevrBodyStick s = { 0 };
    int take;

    /* pushed before the reach: the player's until it centres */
    CHECK(gevrBodyStickStep(&s, 1, 0.8f, 16.0f, &take) == 0 && !take);
    CHECK(gevrBodyStickStep(&s, 1, 0.1f, 16.0f, &take) == 0 && take);
    CHECK(gevrBodyStickStep(&s, 1, 0.7f, 16.0f, &take) == 1 && take);
    CHECK(gevrBodyStickStep(&s, 1, 0.7f, 400.0f, &take) == 0);
    CHECK(gevrBodyStickStep(&s, 1, 0.7f, 60.0f, &take) == 1);    /* 460 ms held: again */
    CHECK(gevrBodyStickStep(&s, 1, 0.7f, 200.0f, &take) == 0);
    CHECK(gevrBodyStickStep(&s, 1, 0.7f, 60.0f, &take) == 1);    /* then every 250 ms */
    CHECK(gevrBodyStickStep(&s, 1, 0.2f, 16.0f, &take) == 0);
    CHECK(gevrBodyStickStep(&s, 1, -0.9f, 16.0f, &take) == -1);
    /* the hover ends with the stick still pushed: held until it centres */
    CHECK(gevrBodyStickStep(&s, 0, -0.9f, 16.0f, &take) == 0 && take);
    CHECK(gevrBodyStickStep(&s, 0, -0.5f, 16.0f, &take) == 0 && take);
    CHECK(gevrBodyStickStep(&s, 0, -0.2f, 16.0f, &take) == 0 && !take);
    CHECK(gevrBodyStickStep(&s, 0, -0.9f, 16.0f, &take) == 0 && !take);
}

static void belt(void)
{
    GevrBodyDefer d = { 0 };

    CHECK(gevrBodyBeltDefer(&d, 1, 1, 0, 16.0f, 100.0f) == 0);
    CHECK(gevrBodyBeltDefer(&d, 0, 1, 0, 50.0f, 100.0f) == 0);
    CHECK(gevrBodyBeltDefer(&d, 0, 1, 0, 50.0f, 100.0f) == 1);   /* a touch reloads */
    CHECK(gevrBodyBeltDefer(&d, 0, 1, 0, 50.0f, 100.0f) == 0);   /* once */
    CHECK(gevrBodyBeltDefer(&d, 0, 0, 0, 16.0f, 100.0f) == 0);   /* out */
    CHECK(gevrBodyBeltDefer(&d, 1, 1, 0, 16.0f, 100.0f) == 0);
    CHECK(gevrBodyBeltDefer(&d, 0, 1, 1, 16.0f, 100.0f) == 0);   /* a grip: the hand is holstering */
    CHECK(gevrBodyBeltDefer(&d, 0, 1, 0, 500.0f, 100.0f) == 0);
    CHECK(gevrBodyBeltDefer(&d, 1, 1, 0, 500.0f, 100.0f) == 0);  /* not again until it leaves */
    CHECK(gevrBodyBeltDefer(&d, 0, 0, 0, 16.0f, 100.0f) == 0);
    CHECK(gevrBodyBeltDefer(&d, 1, 1, 0, 120.0f, 100.0f) == 1);
}

int main(void)
{
    heading();
    torso();
    frames();
    slots();
    zones();
    choices();
    stick();
    belt();
    if (failures)
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("PASS: body slots (heading, torso chase, neck frame, slots and fits, zones, choices, stick, belt)");
    return 0;
}
