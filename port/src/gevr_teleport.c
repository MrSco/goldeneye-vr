/* Teleport locomotion's arithmetic and state (port/include/gevr_teleport.h). */
#include "gevr_teleport.h"
#include <math.h>
#include <string.h>

void gevrTeleportReset(GevrTeleport *t)
{
    memset(t, 0, sizeof(*t));
    t->stamina = GEVR_TP_MAX_CM;
}

int gevrTeleportStick(GevrTeleport *t, float x, float y)
{
    const float mag = sqrtf(x * x + y * y);

    if (!isfinite(mag))
    {
        gevrTeleportStickDrop(t);
        return GEVR_TP_NONE;
    }
    if (t->aiming)
    {
        if (mag < GEVR_TP_STICK_REST)
        {
            t->aiming = 0;
            t->armed = 1;
            return GEVR_TP_CONFIRM;
        }
        return GEVR_TP_AIM;
    }
    if (mag < GEVR_TP_STICK_REST)
    {
        t->armed = 1;
        return GEVR_TP_NONE;
    }
    if (!t->armed || mag < GEVR_TP_STICK_PUSH)
    {
        return GEVR_TP_NONE;
    }
    t->armed = 0;
    /* within 50 degrees of forward (tan 40 = 0.84): the arc */
    if (y > 0.0f && y >= fabsf(x) * 0.84f)
    {
        t->aiming = 1;
        return GEVR_TP_AIM;
    }
    if (fabsf(x) > fabsf(y))
    {
        return x < 0.0f ? GEVR_TP_HOP_LEFT : GEVR_TP_HOP_RIGHT;
    }
    return GEVR_TP_HOP_BACK;
}

void gevrTeleportStickDrop(GevrTeleport *t)
{
    t->aiming = 0;
    t->armed = 0;   /* a stick still held when it comes back starts nothing */
}

void gevrTeleportRest(GevrTeleport *t, float dt, float speed)
{
    if (!(dt > 0.0f) || !isfinite(dt))
    {
        return;
    }
    if (dt > 0.25f)
    {
        dt = 0.25f;   /* a stall is not a rest */
    }
    if (!(speed > 0.0f) || !isfinite(speed))
    {
        speed = 1.0f;
    }
    t->stamina += GEVR_TP_RUN_CM_S * GEVR_TP_RECHARGE * speed * dt;
    if (t->stamina > GEVR_TP_MAX_CM)
    {
        t->stamina = GEVR_TP_MAX_CM;
    }
    t->cooldown -= dt;
    if (t->cooldown < 0.0f)
    {
        t->cooldown = 0.0f;
    }
}

float gevrTeleportReach(const GevrTeleport *t)
{
    if (t->stamina < GEVR_TP_MIN_CM)
    {
        return 0.0f;
    }
    return t->stamina < GEVR_TP_MAX_CM ? t->stamina : GEVR_TP_MAX_CM;
}

int gevrTeleportSpend(GevrTeleport *t, float cm)
{
    if (t->cooldown > 0.0f || !(cm >= GEVR_TP_MIN_CM) || cm > gevrTeleportReach(t) + 0.5f)
    {
        return 0;
    }
    t->stamina -= cm;
    if (t->stamina < 0.0f)
    {
        t->stamina = 0.0f;
    }
    t->cooldown = GEVR_TP_COOLDOWN_S;
    return 1;
}

int gevrTeleportBlinkStart(GevrTeleport *t)
{
    if (t->fade == GEVR_TP_FADE_OUT)
    {
        return 0;
    }
    /* from a blink still clearing: darken again from where it is */
    t->fadeTime = t->fade == GEVR_TP_FADE_IN ? gevrTeleportBlack(t) * GEVR_TP_FADE_OUT_S : 0.0f;
    t->fade = GEVR_TP_FADE_OUT;
    return 1;
}

int gevrTeleportBlinkStep(GevrTeleport *t, float dt)
{
    if (t->fade == GEVR_TP_FADE_IDLE || !(dt > 0.0f) || !isfinite(dt))
    {
        return 0;
    }
    t->fadeTime += dt;
    if (t->fade == GEVR_TP_FADE_OUT)
    {
        if (t->fadeTime >= GEVR_TP_FADE_OUT_S)
        {
            t->fade = GEVR_TP_FADE_IN;
            t->fadeTime = 0.0f;
            return 1;
        }
    }
    else if (t->fadeTime >= GEVR_TP_FADE_IN_S)
    {
        t->fade = GEVR_TP_FADE_IDLE;
        t->fadeTime = 0.0f;
    }
    return 0;
}

float gevrTeleportBlack(const GevrTeleport *t)
{
    float k;

    switch (t->fade)
    {
        case GEVR_TP_FADE_OUT:
            k = t->fadeTime / GEVR_TP_FADE_OUT_S;
            break;
        case GEVR_TP_FADE_IN:
            k = 1.0f - t->fadeTime / GEVR_TP_FADE_IN_S;
            break;
        default:
            return 0.0f;
    }
    return k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k;
}

void gevrTeleportArcPoint(const float hand[3], const float dir[3], float t, float out[3])
{
    out[0] = hand[0] + dir[0] * GEVR_TP_ARC_CM_S * t;
    out[1] = hand[1] + dir[1] * GEVR_TP_ARC_CM_S * t - 0.5f * GEVR_TP_GRAVITY * t * t;
    out[2] = hand[2] + dir[2] * GEVR_TP_ARC_CM_S * t;
}

void gevrTeleportCurvePoint(const float hand[3], const float dir[3], const float end[3], float u, float out[3])
{
    const float dx = end[0] - hand[0], dy = end[1] - hand[1], dz = end[2] - hand[2];
    const float half = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);
    const float a = (1.0f - u) * (1.0f - u), b = 2.0f * u * (1.0f - u), c = u * u;
    int i;

    for (i = 0; i < 3; i++)
    {
        out[i] = a * hand[i] + b * (hand[i] + dir[i] * half) + c * end[i];
    }
}
