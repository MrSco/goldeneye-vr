#include "gevr_collision_slide.h"
#include <math.h>

int gevrCollisionSlideRetry(const float start[3], const float target[3],
    const float edge0[3], const float edge1[3], float out[3])
{
    /* The retail horizontal collision path doesn't initialize target/edge Y. */
    for (int i = 0; i < 3; i += 2)
        if (!isfinite(start[i]) || !isfinite(target[i]) ||
            !isfinite(edge0[i]) || !isfinite(edge1[i])) return 0;
    const double ex = (double)edge1[0] - edge0[0], ez = (double)edge1[2] - edge0[2];
    const double length = hypot(ex, ez);
    if (length < 0.0001) return 0;
    const double tx = ex / length, tz = ez / length;
    const double along = ((double)target[0] - start[0])*tx + ((double)target[2] - start[2])*tz;
    if (fabs(along) < 0.05) return 0; /* a head-on push remains blocked */
    double nx = -tz, nz = tx;
    const double side = ((double)start[0] - edge0[0])*nx + ((double)start[2] - edge0[2])*nz;
    if (fabs(side) < 0.0001) return 0; /* no reliable side of the edge */
    if (side < 0) { nx = -nx; nz = -nz; }
    /* Oblique slides rounded into float world positions can land a few ulps
     * inside a touching edge. Recompute in double and give this retry 0.1 mm
     * clearance on the player's current side. Preserve tangential travel. */
    out[0] = (float)(start[0] + tx*along + nx*0.01);
    out[1] = start[1];
    out[2] = (float)(start[2] + tz*along + nz*0.01);
    const double away = ((double)out[0] - start[0])*nx + ((double)out[2] - start[2])*nz;
    return isfinite(out[0]) && isfinite(out[2]) && away > 0 && away <= 0.025;
}
