#include "gevr_bodyslot.h"
#include <math.h>
#include <string.h>

#define GEVR_BODY_PI 3.14159265358979f

/*
 * Its own atan2: in the game, atan2f, acosf and asinf are GoldenEye's
 * (src/game/math_atan2f.c, math_asinfacosf.c), linked over the C library's.
 * That atan2f returns 0..2 pi, so a small turn one way read as 359 degrees
 * and the torso "jumped" back to the head every tick (headset log,
 * 2026-10-08), and its acosf goes through a 16-bit table, half a degree a
 * step near nought. A polynomial within 2e-6 radians.
 */
static float gevrBodyAtan2(float y, float x)
{
    const float ax = fabsf(x), ay = fabsf(y);
    const float lo = ax < ay ? ax : ay, hi = ax < ay ? ay : ax;
    float z, z2, a;

    if (!(hi > 0.0f))
    {
        return 0.0f;
    }
    z = lo / hi;
    z2 = z * z;
    a = z * (0.99997726f + z2 * (-0.33262347f + z2 * (0.19354346f + z2 * (-0.11643287f
        + z2 * (0.05265332f + z2 * -0.01172120f)))));
    if (ay > ax)
    {
        a = GEVR_BODY_PI * 0.5f - a;
    }
    if (x < 0.0f)
    {
        a = GEVR_BODY_PI - a;
    }
    return y < 0.0f ? -a : a;
}

void gevrBodyQuatRotate(const float q[4], const float v[3], float out[3])
{
    const float qx = q[0], qy = q[1], qz = q[2], qw = q[3];
    const float t0 = qw * v[0] + qy * v[2] - qz * v[1];
    const float t1 = qw * v[1] + qz * v[0] - qx * v[2];
    const float t2 = qw * v[2] + qx * v[1] - qy * v[0];
    const float t3 = -qx * v[0] - qy * v[1] - qz * v[2];

    out[0] = t0 * qw - t3 * qx - t1 * qz + t2 * qy;
    out[1] = t1 * qw - t3 * qy - t2 * qx + t0 * qz;
    out[2] = t2 * qw - t3 * qz - t0 * qy + t1 * qx;
}

int gevrBodyHeading(const float fwd[3], const float up[3], float out[2])
{
    /* looking down the head's up leans forward, looking up it leans back:
     * either way forward minus up along the look's sign is ahead */
    const float s = fwd[1] > 0.0f ? 1.0f : fwd[1] < 0.0f ? -1.0f : 0.0f;
    const float x = fwd[0] - s * up[0], z = fwd[2] - s * up[2];
    const float len = sqrtf(x * x + z * z);

    if (!(len > 1e-4f))
    {
        return 0;
    }
    out[0] = x / len;
    out[1] = z / len;
    return 1;
}

float gevrBodyAngle(const float a[2], const float b[2])
{
    return gevrBodyAtan2(a[0] * b[1] - a[1] * b[0], a[0] * b[0] + a[1] * b[1]) * (180.0f / GEVR_BODY_PI);
}

void gevrBodyTurn(const float a[2], float degrees, float out[2])
{
    const float r = degrees * (GEVR_BODY_PI / 180.0f);
    const float c = cosf(r), s = sinf(r);
    const float x = a[0] * c - a[1] * s, z = a[0] * s + a[1] * c;

    out[0] = x;
    out[1] = z;
}

void gevrBodyTorsoReset(GevrBodyTorso *t)
{
    memset(t, 0, sizeof(*t));
}

int gevrBodyTorsoUpdate(GevrBodyTorso *t, const float head[2], float rate, float ticks,
                        int freeze, float maxTwist, float jumpDeg)
{
    float d;

    if (!t->valid || fabsf(gevrBodyAngle(t->last, head)) > jumpDeg)
    {
        t->heading[0] = t->last[0] = head[0];
        t->heading[1] = t->last[1] = head[1];
        t->valid = 1;
        return 1;
    }
    if (!freeze && rate > 0.0f && ticks > 0.0f)
    {
        const float k = 1.0f - powf(1.0f - (rate < 1.0f ? rate : 1.0f), ticks);

        gevrBodyTurn(t->heading, gevrBodyAngle(t->heading, head) * k, t->heading);
    }
    d = gevrBodyAngle(head, t->heading);
    if (d > maxTwist || d < -maxTwist)
    {
        gevrBodyTurn(head, d > 0.0f ? maxTwist : -maxTwist, t->heading);
    }
    t->last[0] = head[0];
    t->last[1] = head[1];
    return 0;
}

void gevrBodyFrameBuild(GevrBodyFrame *f, const GevrBodyTorso *t, const float headPlay[2],
                        const float fwd[3], const float up[3], const float right[3])
{
    float h[2] = { 0.0f, 1.0f }, ft[2];
    float c[3];
    const float delta = t->valid ? gevrBodyAngle(headPlay, t->heading) : 0.0f;
    int i;

    gevrBodyHeading(fwd, up, h);
    gevrBodyTurn(h, delta, ft);
    f->up[0] = 0.0f; f->up[1] = 1.0f; f->up[2] = 0.0f;
    f->fwd[0] = ft[0]; f->fwd[1] = 0.0f; f->fwd[2] = ft[1];
    /* right as the camera has it: fwd x up, or up x fwd in a mirrored frame */
    c[0] = fwd[1] * up[2] - fwd[2] * up[1];
    c[1] = fwd[2] * up[0] - fwd[0] * up[2];
    c[2] = fwd[0] * up[1] - fwd[1] * up[0];
    if (c[0] * right[0] + c[1] * right[1] + c[2] * right[2] >= 0.0f)
    {
        f->right[0] = -ft[1]; f->right[1] = 0.0f; f->right[2] = ft[0];    /* fwd x up */
    }
    else
    {
        f->right[0] = ft[1]; f->right[1] = 0.0f; f->right[2] = -ft[0];    /* up x fwd */
    }
    /* the neck's pivot behind and below the eye, then the eye upright over it */
    for (i = 0; i < 3; i++)
    {
        f->origin[i] = -GEVR_BODY_NECK_UP_CM * up[i] - GEVR_BODY_NECK_AHEAD_CM * fwd[i]
                     + GEVR_BODY_NECK_UP_CM * f->up[i] + GEVR_BODY_NECK_AHEAD_CM * f->fwd[i];
    }
    f->twist = delta;
    f->pitch = gevrBodyAtan2(fwd[1], sqrtf(fwd[0] * fwd[0] + fwd[2] * fwd[2])) * (180.0f / GEVR_BODY_PI);
}

/*
 * Defaults as fractions of the standing eye height (170 cm: the numbers in
 * brackets). The hips sit under the neck, out at the belt where a holster
 * hangs (the user's belt reaches, 2026-10-04: 45-75 below, 17-22 out); the
 * shoulders' centres behind the head at ear height, where a hand reaching
 * over the shoulder ends (Lambda1VR's backpack: within 40 cm of the head and
 * behind it); the chest in front of the sternum; the gadgets at the front of
 * the belt, off the buckle on the off side.
 */
static const float s_gevrBodyDefault[GEVR_BODY_SLOTS][3] = {
    { 0.38f, 0.12f, -0.06f },   /* hips: 65 below, 20 out, 10 behind */
    { 0.38f, 0.12f, -0.06f },
    { 0.05f, 0.11f, -0.12f },   /* shoulders: 8 below, 19 out, 20 behind */
    { 0.05f, 0.11f, -0.12f },
    { 0.23f, 0.00f, 0.025f },   /* chest: 39 below, 4 ahead; closer to the sternum */
    { 0.35f, 0.04f, 0.06f },    /* belt: 60 below, 7 out, 10 ahead */
};
/* cm at Normal; the shoulders are reached blind, so they are larger (Quake VR's 30 cm) */
static const float s_gevrBodyRadius[GEVR_BODY_SLOTS] = { 13.0f, 13.0f, 20.0f, 20.0f, 10.0f, 10.0f };

void gevrBodySlotDefault(int slot, float eyeHeight, float out[3])
{
    int i;

    for (i = 0; i < 3; i++)
    {
        out[i] = slot >= 0 && slot < GEVR_BODY_SLOTS ? s_gevrBodyDefault[slot][i] * eyeHeight : 0.0f;
    }
}

void gevrBodySlotOffsets(int slot, const float fit[3], float eyeHeight, float out[3])
{
    int i;

    if (fit != NULL && (fit[0] != 0.0f || fit[1] != 0.0f || fit[2] != 0.0f))
    {
        for (i = 0; i < 3; i++)
        {
            out[i] = fit[i];
        }
        return;
    }
    gevrBodySlotDefault(slot, eyeHeight, out);
}

int gevrBodySlotSide(int slot, int leftHanded)
{
    const int gun = leftHanded ? -1 : 1;

    switch (slot)
    {
        case GEVR_BS_HIP_OFF:
        case GEVR_BS_BACK_OFF:
        case GEVR_BS_BELT:
            return -gun;
        default:
            return gun;
    }
}

float gevrBodySlotRadius(int slot, int size)
{
    static const float scale[3] = { 0.8f, 1.0f, 1.25f };

    if (slot < 0 || slot >= GEVR_BODY_SLOTS)
    {
        return 0.0f;
    }
    return s_gevrBodyRadius[slot] * scale[size < 0 ? 0 : size > 2 ? 2 : size];
}

int gevrBodySlotCategory(int slot)
{
    static const int cat[GEVR_BODY_SLOTS] = {
        GEVR_BODY_CAT_PISTOLS, GEVR_BODY_CAT_PISTOLS, GEVR_BODY_CAT_RIFLES,
        GEVR_BODY_CAT_HEAVY, GEVR_BODY_CAT_THROWN, GEVR_BODY_CAT_GADGETS,
    };

    return slot >= 0 && slot < GEVR_BODY_SLOTS ? cat[slot] : -1;
}

int gevrBodySlotHands(int slot)
{
    return slot == GEVR_BS_HIP_GUN ? 2 : slot == GEVR_BS_HIP_OFF ? 1 : 3;
}

const char *gevrBodySlotName(int slot)
{
    static const char *names[GEVR_BODY_SLOTS] = {
        "gun hip", "off hip", "gun shoulder", "off shoulder", "chest", "belt",
    };

    return slot >= 0 && slot < GEVR_BODY_SLOTS ? names[slot] : "none";
}

void gevrBodySlotCentre(const GevrBodyFrame *f, int slot, const float off[3], int leftHanded, float out[3])
{
    const float side = (float) gevrBodySlotSide(slot, leftHanded) * off[1];
    int i;

    for (i = 0; i < 3; i++)
    {
        out[i] = f->origin[i] - off[0] * f->up[i] + side * f->right[i] + off[2] * f->fwd[i];
    }
}

void gevrBodyLocal(const GevrBodyFrame *f, const float p[3], float out[3])
{
    const float d[3] = { p[0] - f->origin[0], p[1] - f->origin[1], p[2] - f->origin[2] };

    out[0] = -(d[0] * f->up[0] + d[1] * f->up[1] + d[2] * f->up[2]);
    out[1] = d[0] * f->right[0] + d[1] * f->right[1] + d[2] * f->right[2];
    out[2] = d[0] * f->fwd[0] + d[1] * f->fwd[1] + d[2] * f->fwd[2];
}

void gevrBodySlotFitFrom(const GevrBodyFrame *f, int slot, const float p[3], int leftHanded, float out[3])
{
    gevrBodyLocal(f, p, out);
    out[1] *= (float) gevrBodySlotSide(slot, leftHanded);
}

int gevrBodyZonePick(int current, const float dist[GEVR_BODY_SLOTS], const float radius[GEVR_BODY_SLOTS],
                     const int eligible[GEVR_BODY_SLOTS], float exitCm, float margin)
{
    int best = -1, s;
    float bestn = 1.0f;

    for (s = 0; s < GEVR_BODY_SLOTS; s++)
    {
        if (eligible[s] && radius[s] > 0.0f && dist[s] / radius[s] < bestn)
        {
            best = s;
            bestn = dist[s] / radius[s];
        }
    }
    if (current >= 0 && current < GEVR_BODY_SLOTS && eligible[current] && radius[current] > 0.0f
        && dist[current] < radius[current] + exitCm)
    {
        const float n = dist[current] / radius[current];

        return best >= 0 && best != current && bestn < n - margin ? best : current;
    }
    return best;
}

void gevrBodyMruTouch(GevrBodyMru *m, int item)
{
    int i, j;

    for (i = 0; i < m->count && m->items[i] != item; i++)
    {
    }
    if (i == 0 && m->count > 0)
    {
        return;   /* already the newest */
    }
    if (i == m->count)
    {
        i = m->count < GEVR_BODY_MRU ? m->count++ : GEVR_BODY_MRU - 1;   /* new: the oldest drops off */
    }
    for (j = i; j > 0; j--)
    {
        m->items[j] = m->items[j - 1];
    }
    m->items[0] = item;
}

static int gevrBodyHas(const int *list, int n, int item)
{
    int i;

    for (i = 0; i < n; i++)
    {
        if (list[i] == item)
        {
            return 1;
        }
    }
    return 0;
}

int gevrBodyChoices(const int *candidates, int n, int held, int heldHere, int *out, int max)
{
    int k = 0, i;

    for (i = 0; i < n && k < max; i++)
    {
        if (candidates[i] != held && candidates[i] >= 0 && !gevrBodyHas(out, k, candidates[i]))
        {
            out[k++] = candidates[i];
        }
    }
    if (heldHere && k < max)
    {
        out[k++] = GEVR_BODY_HOLSTER;
    }
    return k;
}

int gevrBodyDefaultPick(const GevrBodyMru *m, const int *choices, int n)
{
    int i;

    for (i = 0; m != NULL && i < m->count; i++)
    {
        if (gevrBodyHas(choices, n, m->items[i]))
        {
            return m->items[i];
        }
    }
    return n > 0 ? choices[0] : -1;
}

int gevrBodyStepPick(const int *choices, int n, int pick, int dir)
{
    int i;

    if (n <= 0)
    {
        return -1;
    }
    for (i = 0; i < n && choices[i] != pick; i++)
    {
    }
    if (i == n)
    {
        return choices[0];
    }
    return choices[((i + dir) % n + n) % n];
}

int gevrBodyGripAction(int choice, int blocked)
{
    if (choice >= 0 || choice == GEVR_BODY_HOLSTER)
    {
        return blocked ? GEVR_BODY_DENY : choice >= 0 ? GEVR_BODY_DRAW : GEVR_BODY_STOW;
    }
    return GEVR_BODY_FALL;
}

int gevrBodyThrowGate(float hoverMs, float speed, float dwellMs, float maxSpeed)
{
    return hoverMs >= dwellMs && speed < maxSpeed;
}

float gevrBodySettleMs(float ms, float speed, float dtMs, float maxSpeed)
{
    return speed < maxSpeed ? ms + dtMs : 0.0f;
}

int gevrBodyGripHeld(int wasHeld, int pressed, float squeeze)
{
    return pressed || (wasHeld && squeeze >= 0.25f);
}

int gevrBodyGaze(int was, const float fwd[3], const float at[3])
{
    const float dot = fwd[0] * at[0] + fwd[1] * at[1] + fwd[2] * at[2];
    const float len = sqrtf((fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2])
                            * (at[0] * at[0] + at[1] * at[1] + at[2] * at[2]));

    return len > 1e-6f && dot >= (was ? GEVR_BODY_GAZE_OUT_COS : GEVR_BODY_GAZE_IN_COS) * len;
}

int gevrBodyStickPoint(GevrBodyStick *s, int hovering, int count, float x, float y, int *take)
{
    const float magnitude = sqrtf(x * x + y * y);
    float span, hyst, deg, rel;

    *take = 0;
    if (!hovering || count <= 0)
    {
        s->armed = 0;
        s->wedge = -1;
        if (s->latched)
        {
            if (magnitude <= 0.3f)
            {
                s->latched = 0;
            }
            else
            {
                *take = 1;
            }
        }
        return -1;
    }
    if (!s->armed)
    {
        if (magnitude > 0.3f)
        {
            return -1;   /* still pushed from before the reach: it stays the player's */
        }
        s->armed = 1;
        s->wedge = -1;
    }
    if (magnitude <= 0.3f) s->latched = 0;
    else if (magnitude >= 0.5f) s->latched = 1;
    *take = s->latched;
    if (magnitude < 0.5f)
    {
        s->wedge = -1;   /* the highlight stays put */
        return -1;
    }
    span = 360.0f / count;
    hyst = span * 0.25f < 6.0f ? span * 0.25f : 6.0f;
    deg = gevrBodyAtan2(x, y) * (180.0f / GEVR_BODY_PI);   /* clockwise from up */
    if (deg < 0.0f)
    {
        deg += 360.0f;
    }
    if (s->wedge >= 0 && s->wedge < count)
    {
        rel = deg - s->wedge * span;
        while (rel >= 180.0f) rel -= 360.0f;
        while (rel < -180.0f) rel += 360.0f;
        if (fabsf(rel) <= span * 0.5f + hyst)
        {
            return s->wedge;
        }
    }
    s->wedge = (int) (deg / span + 0.5f) % count;
    return s->wedge;
}

int gevrBodyBeltDefer(GevrBodyDefer *d, int entered, int inside, int gripped, float dtMs, float deferMs)
{
    if (!inside)
    {
        d->pending = d->spent = 0;
        d->ms = 0.0f;
        return 0;
    }
    if (entered && !d->spent)
    {
        d->pending = 1;
        d->ms = 0.0f;
    }
    if (!d->pending)
    {
        return 0;
    }
    if (gripped)
    {
        d->pending = 0;
        d->spent = 1;   /* the hand went to the belt to holster */
        return 0;
    }
    d->ms += dtMs;
    if (d->ms >= deferMs)
    {
        d->pending = 0;
        d->spent = 1;
        return 1;
    }
    return 0;
}
