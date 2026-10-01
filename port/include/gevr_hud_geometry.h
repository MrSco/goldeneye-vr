#ifndef GEVR_HUD_GEOMETRY_H
#define GEVR_HUD_GEOMETRY_H
#include <math.h>
/* Screen coordinates: clockwise positive. Native gauge x includes a +1
 * pixel skew; remove it so the two arcs share the radar's true center.
 * Inner rim is 3 scaled HUD pixels outside the 16-pixel radar radius.
 * Both arcs move down two scaled HUD pixels to match the visible contour. */
static inline void gevrRadarGaugePoint(float nativeX, float nativeY, float radius,
    float degrees, float *x, float *y) {
    float scale = radius * (19.0f / 16.0f) / 520.0f;
    float a = degrees * (3.14159265358979323846f / 180.0f);
    float nx = (nativeX - 1.0f) * scale, ny = nativeY * scale;
    *x = nx * cosf(a) - ny * sinf(a);
    *y = nx * sinf(a) + ny * cosf(a) + radius / 8.0f;
}
#endif
