#ifndef GEVR_SURFACE_MATH_H
#define GEVR_SURFACE_MATH_H

#include <stdint.h>

/* Host-only display-list extension: pre-projected water, no s16 packing. */
#define G_GEVR_SKY_VTX 0x7F
typedef struct GevrSkyVertex {
    float x, y, w;
    float s, t; /* S10.5 units, as produced by the game's plane projection */
    uint8_t rgba[4];
} GevrSkyVertex;

static inline void gevrSkyVertexPosition(GevrSkyVertex *v,
    float sx, float sy, float w, float s, float t,
    float left, float top, float width, float height)
{
    v->x = (2.0f * ((sx - left) / width) - 1.0f) * w;
    v->y = (1.0f - 2.0f * ((sy - top) / height)) * w;
    v->w = w;
    v->s = s;
    v->t = t;
}

/* fast3d transforms LookAt from view space to each model's space. Convert
 * the body's world axes to view space first so the camera rotations cancel.
 * Call before the view matrix acquires the level's visibility scale. */
static inline void gevrReflectionAxisToView(int8_t dir[3], const float view[4][4])
{
    float world[3] = {dir[0] / 127.0f, dir[1] / 127.0f, dir[2] / 127.0f};
    int axis;
    for (axis = 0; axis < 3; ++axis) {
        float component = world[0] * view[0][axis]
            + world[1] * view[1][axis] + world[2] * view[2][axis];
        float fixed = component * 127.0f;
        if (fixed > 127.0f) fixed = 127.0f;
        if (fixed < -127.0f) fixed = -127.0f;
        dir[axis] = (int8_t) fixed;
    }
}

#endif
