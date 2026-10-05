#include "gevr_locomotion.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static float gevrYawDifference(float a, float b)
{
    float d = fmodf(a - b, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

void gevrLocomotionReset(GevrLocomotionHistory *h)
{
    memset(h, 0, sizeof(*h));
}

const char *gevrLocomotionResetName(GevrLocomotionResetReason reason)
{
    static const char *names[] = {"NONE", "OTHER", "CONTEXT", "TELEPORT", "RECENTER",
        "SNAP", "PAUSE", "TANK", "TRACKING", "SESSION", "REFRESH", "GAP", "CLOCK",
        "INVALID", "SCREEN", "PHYSICAL"};
    return reason >= 0 && reason < GEVR_LOCO_RESET_COUNT ? names[reason] : "INVALID";
}

int gevrLocomotionPhysicalBlocked(const float step[3], const float requested[3],
    const float actual[3])
{
    /* Ignore sub-millimetre tracking jitter and collision loss perpendicular
     * to the head's movement (e.g. strafing along a wall while pushing into it).
     * This gates presentation resets only; physical input is never discarded. */
    const float lengthSquared = step[0]*step[0] + step[2]*step[2];
    const float blocked = step[0]*(requested[0]-actual[0]) + step[2]*(requested[2]-actual[2]);
    return lengthSquared > 0.01f && blocked > 0.1f * sqrtf(lengthSquared);
}

int64_t gevrLocomotionDelay(int64_t period)
{
    if (period <= 0 || period > 100000000) return 16666667;
    /* Runtime periods vary by a few ns even without a refresh change. Near
     * an integer divisor, ceil() could turn two 120 Hz intervals into three
     * (25 ms). Use the exact logical tick when the nearest display multiple
     * differs by at most 1 us; otherwise cover the full normal game interval. */
    const int64_t tick = 16666667;
    const int64_t nearest = (tick + period / 2) / period;
    if (nearest >= 1 && llabs(nearest * period - tick) <= 1000) return tick;
    return ((tick + period - 1) / period) * period;
}

int gevrLocomotionSnapshot(GevrLocomotionHistory *h, const float position[3],
    const float tracking[3], float yaw, uint64_t sequence, int64_t now, int64_t period)
{
    int reset = 0;
    h->resetReason = GEVR_LOCO_RESET_NONE;
    for (int i = 0; i < 3; i++) {
        if (!isfinite(position[i]) || !isfinite(tracking[i])) {
            gevrLocomotionReset(h);
            h->resetReason = GEVR_LOCO_RESET_INVALID;
            return 0;
        }
    }
    if (!isfinite(yaw) || now <= 0 || period <= 0 || period > 100000000) {
        gevrLocomotionReset(h);
        h->resetReason = GEVR_LOCO_RESET_INVALID;
        return 0;
    }
    if (h->count && (llabs(period - h->period) > 1000 || now < h->lastDisplayTime ||
        now - h->lastDisplayTime > 100000000 || sequence < h->poses[h->count - 1].sequence ||
        sequence - h->poses[h->count - 1].sequence > 6)) {
        const GevrLocomotionResetReason reason = llabs(period - h->period) > 1000
            ? GEVR_LOCO_RESET_REFRESH : now < h->lastDisplayTime || sequence < h->poses[h->count - 1].sequence
            ? GEVR_LOCO_RESET_CLOCK : GEVR_LOCO_RESET_GAP;
        gevrLocomotionReset(h);
        h->resetReason = reason;
        reset = 1;
    }
    if (h->count && sequence == h->poses[h->count - 1].sequence) return 0;
    if (!h->count) {
        h->anchorTime = now;
        h->anchorSequence = sequence;
        h->period = period;
        h->delay = gevrLocomotionDelay(period);
    }
    if (h->count == 8) {
        memmove(h->poses, h->poses + 1, sizeof(h->poses[0]) * 7);
        h->count--;
    }
    GevrLocomotionPose *p = &h->poses[h->count++];
    for (int i = 0; i < 3; i++) p->position[i] = position[i] - tracking[i];
    p->yaw = yaw;
    p->sequence = sequence;
    const uint64_t ticks = sequence - h->anchorSequence;
    p->time = h->anchorTime + (int64_t)(ticks / 60) * 1000000000LL
        + (int64_t)(ticks % 60) * 1000000000LL / 60;
    h->lastDisplayTime = now;
    return reset ? 2 : 1;
}

int gevrLocomotionQuery(GevrLocomotionHistory *h, int64_t now, GevrLocomotionPose *p)
{
    h->clampReason = GEVR_LOCO_CLAMP_NONE;
    h->targetLead = 0;
    if (!h->count) {
        h->clampReason = GEVR_LOCO_CLAMP_MISSING;
        return 0;
    }
    const int64_t target = now - h->delay;
    *p = h->poses[h->count - 1];
    h->targetLead = target - p->time;
    if (now < h->lastDisplayTime || now - h->lastDisplayTime > 100000000) {
        h->clamps++;
        h->clampReason = GEVR_LOCO_CLAMP_STALE;
        return 1; /* a stall holds the latest confirmed position, never predicts */
    }
    if (target < h->poses[0].time) {
        *p = h->poses[0];
        h->clamps++;
        h->clampReason = GEVR_LOCO_CLAMP_EARLY;
        return 1;
    }
    if (target > p->time) {
        h->clamps++;
        h->clampReason = GEVR_LOCO_CLAMP_LATE;
        return 1;
    }
    for (unsigned i = 1; i < h->count; i++) {
        const GevrLocomotionPose *a = &h->poses[i - 1], *b = &h->poses[i];
        if (target <= b->time) {
            const float t = (float)((double)(target - a->time) / (double)(b->time - a->time));
            for (int j = 0; j < 3; j++) p->position[j] = a->position[j] + t * (b->position[j] - a->position[j]);
            p->yaw = a->yaw + t * gevrYawDifference(b->yaw, a->yaw);
            p->time = target;
            return 1;
        }
    }
    return 1;
}

void gevrLocomotionCamera(const GevrPresentationCamera *source,
    const GevrLocomotionPose *confirmed, const GevrLocomotionPose *presented,
    const float headRotationDelta[9], const float headTranslation[3],
    GevrPresentationCamera *camera)
{
    float r[9];
    for (int y = 0; y < 3; y++) {
        camera->position[y] = source->position[y] + presented->position[y] - confirmed->position[y];
        for (int k = 0; k < 3; k++) camera->position[y] += source->rotation[y * 3 + k] * headTranslation[k];
        for (int x = 0; x < 3; x++) {
            r[y * 3 + x] = 0;
            for (int k = 0; k < 3; k++) r[y * 3 + x] += source->rotation[y * 3 + k] * headRotationDelta[k * 3 + x];
        }
    }
    /* GoldenEye body yaw rotates by -yaw about world +Y. Rotate about the
     * current eye, not the play-space origin, so turning cannot orbit it. */
    const float a = -gevrYawDifference(presented->yaw, confirmed->yaw) * 0.017453292519943295f;
    const float c = cosf(a), s = sinf(a);
    for (int x = 0; x < 3; x++) {
        camera->rotation[x] = c * r[x] + s * r[6 + x];
        camera->rotation[3 + x] = r[3 + x];
        camera->rotation[6 + x] = -s * r[x] + c * r[6 + x];
    }
}

void gevrCameraDelta(const GevrPresentationCamera *source,
    const GevrPresentationCamera *camera, float scale, float out[16])
{
    memset(out, 0, 16 * sizeof(float));
    for (int y = 0; y < 3; y++) {
        for (int x = 0; x < 3; x++) {
            for (int k = 0; k < 3; k++) out[x * 4 + y] += camera->rotation[k * 3 + y] * source->rotation[k * 3 + x];
        }
        for (int k = 0; k < 3; k++) out[12 + y] += camera->rotation[k * 3 + y] * (source->position[k] - camera->position[k]) * scale;
    }
    out[15] = 1;
}

void gevrMat4Multiply(const float a[16], const float b[16], float out[16])
{
    float m[16] = {0};
    for (int x = 0; x < 4; x++) for (int y = 0; y < 4; y++)
        for (int k = 0; k < 4; k++) m[x * 4 + y] += a[k * 4 + y] * b[x * 4 + k];
    memcpy(out, m, sizeof(m));
}

void gevrPresentationClip(const float delta[16], const float projection[16],
    const float clip[4], float out[4])
{
    const float c[4] = {clip[0] / projection[0], clip[1] / projection[5], -clip[3], 1};
    float view[4] = {0};
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) view[y] += delta[x * 4 + y] * c[x];
    for (int y = 0; y < 4; y++) {
        out[y] = 0;
        for (int x = 0; x < 4; x++) out[y] += projection[x * 4 + y] * view[x];
    }
}
