/*
 * Optic profiles (GEVROPT4) for mod.attach.optic: parsed whole from the
 * pack's bytes, refused (NULL, logged) when anything is out of range.
 *
 *   "GEVROPT4", u32 modes (1 to 12), then each mode:
 *     u32 info[4]: [0] kind (1 collimator, 2 scope, 3 night, 4 thermal) in
 *                  bits 0-7, folded bit 8, palette bits 9-10, first focal
 *                  plane bit 11; [1] outline points (3 to 64); [2] reticle
 *                  width; [3] reticle height (1 to 1024)
 *     f32 fields[19]: centre[3], right[3], up[3], forward[3] (the lens in the
 *                  profile's frame), half[2] (its half size, metres),
 *                  reticleAxis[2] (the reticle's centre, 0..1), angular
 *                  (reticle texture units per radian, a collimator's),
 *                  lowZoom, highZoom (1 or more, low <= high)
 *     f32 outline[points][2] (the lens's shape, -1..1 of its half size)
 *     u8 reticle[width * height * 4] (RGBA8)
 *   and nothing after.
 *
 * A collimator's marks are lifted once as they load, as the Tactical build
 * the format comes from does, so a small mark stays legible.
 */
#include "gevr_modoptic.h"
#include "system.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void liftCollimatorMark(GevrOpticMode *mode, size_t length)
{
    unsigned char *source = malloc(length);
    if (!source) return;
    memcpy(source, mode->reticle, length);
    const unsigned width = mode->width, height = mode->height;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const size_t index = (size_t)y * width + x;
            const unsigned char *mark = source + index * 4;
            unsigned alpha = mark[3] * 4u;
            const size_t adjacent[] = {
                index - (x > 0), index + (x + 1 < width),
                index - (y > 0 ? width : 0), index + (y + 1 < height ? width : 0),
            };
            for (unsigned i = 0; i < 4; ++i) {
                const unsigned char *sample = source + adjacent[i] * 4;
                if (sample[3] <= alpha) continue;
                alpha = sample[3];
                mark = sample;
            }
            alpha = (alpha + 2u) / 4u;
            if (!alpha) continue;
            unsigned char *pixel = mode->reticle + index * 4;
            const float peak = fmaxf(mark[0], fmaxf(mark[1], mark[2]));
            const float lift = peak > 0.f ? .24f + .76f * 255.f / peak : 1.f;
            for (unsigned i = 0; i < 3; ++i) pixel[i] = (unsigned char)lroundf(mark[i] * lift);
            pixel[3] = alpha + (alpha * (255u - alpha) + 127u) / 255u;
        }
    }
    free(source);
}

typedef struct { const unsigned char *p, *end; } Reader;

static int take(Reader *r, void *out, size_t n)
{
    if ((size_t)(r->end - r->p) < n) return 0;
    memcpy(out, r->p, n);
    r->p += n;
    return 1;
}

static int readU32(Reader *r, uint32_t *value)
{
    unsigned char bytes[4];
    if (!take(r, bytes, sizeof(bytes))) return 0;
    *value = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
    return 1;
}

static int readFloat(Reader *r, float *value)
{
    uint32_t bits;
    if (!readU32(r, &bits)) return 0;
    memcpy(value, &bits, sizeof(bits));
    return isfinite(*value);
}

static int readMode(Reader *r, GevrOpticMode *mode)
{
    uint32_t info[4];
    float fields[19];
    for (unsigned i = 0; i < 4; ++i)
        if (!readU32(r, &info[i])) return 0;
    for (unsigned i = 0; i < 19; ++i)
        if (!readFloat(r, &fields[i])) return 0;
    const unsigned kind = info[0] & 255u;
    if (kind < GEVR_OPTIC_COLLIMATOR || kind > GEVR_OPTIC_THERMAL || (info[0] >> 12) != 0
        || info[1] < 3 || info[1] > GEVR_OPTIC_POINTS
        || info[2] < 1 || info[2] > GEVR_OPTIC_RETICLE_MAX || info[3] < 1 || info[3] > GEVR_OPTIC_RETICLE_MAX)
        return 0;
    if (fields[12] <= 0.f || fields[13] <= 0.f || fields[17] < 1.f || fields[18] < fields[17]) return 0;
    mode->kind = kind;
    mode->folded = (info[0] >> 8) & 1u;
    mode->palette = (info[0] >> 9) & 3u;
    mode->firstFocalPlane = (info[0] >> 11) & 1u;
    mode->points = info[1];
    mode->width = info[2];
    mode->height = info[3];
    memcpy(mode->centre, fields, 3 * sizeof(float));
    memcpy(mode->right, fields + 3, 3 * sizeof(float));
    memcpy(mode->up, fields + 6, 3 * sizeof(float));
    memcpy(mode->forward, fields + 9, 3 * sizeof(float));
    memcpy(mode->half, fields + 12, 2 * sizeof(float));
    memcpy(mode->reticleAxis, fields + 14, 2 * sizeof(float));
    mode->angular = fields[16];
    mode->lowZoom = fields[17];
    mode->highZoom = fields[18];
    for (unsigned i = 0; i < mode->points; ++i)
        for (unsigned axis = 0; axis < 2; ++axis) {
            float *coordinate = &mode->boundary[i][axis];
            if (!readFloat(r, coordinate) || fabsf(*coordinate) > 1.01f) return 0;
        }
    const size_t length = 4u * mode->width * mode->height;
    if ((size_t)(r->end - r->p) < length) return 0;
    mode->reticle = malloc(length);
    if (!mode->reticle || !take(r, mode->reticle, length)) return 0;
    if (kind == GEVR_OPTIC_COLLIMATOR) liftCollimatorMark(mode, length);
    return 1;
}

void gevrModOpticFree(GevrOpticProfile *profile)
{
    if (!profile) return;
    for (int i = 0; i < GEVR_OPTIC_MODES; ++i) free(profile->modes[i].reticle);
    free(profile);
}

const GevrOpticProfile *gevrModOpticParse(const unsigned char *bytes, unsigned long size, const char *name)
{
    Reader r = {bytes, bytes + size};
    char magic[8];
    uint32_t count = 0;
    GevrOpticProfile *profile = calloc(1, sizeof(*profile));
    int good = profile != NULL && take(&r, magic, 8) && !memcmp(magic, "GEVROPT4", 8)
        && readU32(&r, &count) && count >= 1 && count <= GEVR_OPTIC_MODES;
    if (good) {
        profile->count = (int)count;
        for (uint32_t i = 0; good && i < count; ++i) good = readMode(&r, &profile->modes[i]);
        good = good && r.p == r.end;
    }
    if (!good) {
        sysLogPrintf(LOG_WARNING, "mods: optic %s refused: not a whole GEVROPT4 profile", name ? name : "?");
        gevrModOpticFree(profile);
        return NULL;
    }
    return profile;
}
