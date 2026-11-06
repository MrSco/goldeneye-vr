#include "gevr_pull.h"
#include <math.h>

float gevrPullScore(const float p[3], const float f[3], const float t[3], float cm, float downcos)
{
    float d[3], dist, c, cone;
    int k;

    if (!(cm > 0.0f))
    {
        return -1.0f;
    }
    for (k = 0; k < 3; k++)
    {
        d[k] = t[k] - p[k];
    }
    dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (dist > GEVR_PULL_RANGE_CM * cm)
    {
        return -1.0f;
    }
    if (dist < 1e-4f * cm)
    {
        return 0.0f;   /* at the hand */
    }
    c = (d[0] * f[0] + d[1] * f[1] + d[2] * f[2]) / dist;
    cone = downcos >= GEVR_PULL_DOWN_COS ? GEVR_PULL_COS_CONE_DOWN : GEVR_PULL_COS_CONE;
    if (c < cone)
    {
        return -1.0f;
    }
    /* 1 - cos: 0 on the ray, 0.0038 at 5 degrees, 0.034 at 15; a metre further adds 0.0005 */
    return (1.0f - c) + 0.0005f * dist / (100.0f * cm);
}

float gevrPullDuration(float distcm)
{
    float u = distcm / GEVR_PULL_RANGE_CM;

    if (u < 0.0f)
    {
        u = 0.0f;
    }
    if (u > 1.0f)
    {
        u = 1.0f;
    }
    return 0.30f + 0.10f * u;
}

void gevrPullFlight(const float from[3], const float to[3], const float up[3], float lift, float u, float out[3])
{
    float e, a, inv;
    int k;

    if (u < 0.0f)
    {
        u = 0.0f;
    }
    if (u > 1.0f)
    {
        u = 1.0f;
    }
    inv = 1.0f - u;
    e = 1.0f - inv * inv * inv;          /* fast away, easing into the palm */
    a = 4.0f * u * inv * lift;           /* the arc, highest at the middle */
    for (k = 0; k < 3; k++)
    {
        out[k] = from[k] + (to[k] - from[k]) * e + up[k] * a;
    }
}

int gevrPullChooseHand(int pulling, int pullingAllowed, int gunHandEmpty, int gunHandAllowed)
{
    if (pullingAllowed)
    {
        return pulling;
    }
    if (gunHandEmpty && gunHandAllowed)
    {
        return 0;
    }
    return -1;
}

void gevrPullArmRay(const float p[3], const float f[3], const float down[3], float side, float cm, float out[3])
{
    float r[3], sh[3], a[3], len, rd, w;
    int k;

    /* the head's right, level: view +X with its down part taken out */
    rd = down[0];
    r[0] = 1.0f - rd * down[0];
    r[1] = -rd * down[1];
    r[2] = -rd * down[2];
    len = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (k = 0; k < 3; k++)
    {
        r[k] = len > 1e-6f ? r[k] / len : 0.0f;
        sh[k] = (down[k] * GEVR_PULL_SHOULDER_DROP_CM + r[k] * side * GEVR_PULL_SHOULDER_SIDE_CM) * cm;
        a[k] = p[k] - sh[k];
    }
    len = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    w = cm > 0.0f ? (len / cm - GEVR_PULL_ARM_MIN_CM) / (GEVR_PULL_ARM_FULL_CM - GEVR_PULL_ARM_MIN_CM) : 0.0f;
    if (w < 0.0f)
    {
        w = 0.0f;
    }
    if (w > 1.0f)
    {
        w = 1.0f;
    }
    for (k = 0; k < 3; k++)
    {
        out[k] = (len > 1e-6f ? a[k] / len : 0.0f) * w + f[k] * (1.0f - w);
    }
    len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    for (k = 0; k < 3; k++)
    {
        out[k] = len > 1e-6f ? out[k] / len : f[k];
    }
}
