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

int64_t gevrLocomotionDelay(int64_t period)
{
    if (period <= 0 || period > 100000000) return 16666667;
    /* ceil(display Hz / 60) display intervals. Allow rounding of runtime ns
     * periods at integer divisors (120 Hz is 2 intervals, not 3). */
    int64_t intervals = (1000000000LL + 60 * period - 61) / (60 * period);
    if (intervals < 1) intervals = 1;
    return intervals * period;
}

int gevrLocomotionSnapshot(GevrLocomotionHistory *h, const float position[3],
    const float tracking[3], float yaw, uint64_t sequence, int64_t now, int64_t period)
{
    int reset = 0;
    for (int i = 0; i < 3; i++) {
        if (!isfinite(position[i]) || !isfinite(tracking[i])) {
            gevrLocomotionReset(h);
            return 0;
        }
    }
    if (!isfinite(yaw) || now <= 0 || period <= 0 || period > 100000000) {
        gevrLocomotionReset(h);
        return 0;
    }
    if (h->count && (llabs(period - h->period) > 1000 || now < h->lastDisplayTime ||
        now - h->lastDisplayTime > 100000000 || sequence < h->poses[h->count - 1].sequence ||
        sequence - h->poses[h->count - 1].sequence > 6)) {
        gevrLocomotionReset(h);
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
    if (!h->count) return 0;
    const int64_t target = now - h->delay;
    *p = h->poses[h->count - 1];
    if (now < h->lastDisplayTime || now - h->lastDisplayTime > 100000000) {
        h->clamps++;
        return 1; /* a stall holds the latest confirmed position, never predicts */
    }
    if (target < h->poses[0].time) {
        *p = h->poses[0];
        h->clamps++;
        return 1;
    }
    if (target > p->time) {
        h->clamps++;
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
