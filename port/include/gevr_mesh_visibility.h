#ifndef GEVR_MESH_VISIBILITY_H
#define GEVR_MESH_VISIBILITY_H
#include <math.h>

/* Conservative rejection of a mesh chunk's box. All eight corners must be
 * outside the same plane; a box spanning the camera is kept. Column-major MP.
 * Eye margins include stereo shear. Scopes pass farScale=0 (their own far plane). */
static inline int gevrMeshBoundsVisible(const float lo[3], const float hi[3], const float mp[16],
                                      float marginX, float marginXW, float marginYW, float farScale)
{
    unsigned all = 63;
    float minW = INFINITY, maxW = -INFINITY;
    for (int i = 0; i < 8; i++) {
        float p[3] = {i & 1 ? hi[0] : lo[0], i & 2 ? hi[1] : lo[1], i & 4 ? hi[2] : lo[2]};
        float c[4];
        for (int r = 0; r < 4; r++) {
            c[r] = p[0] * mp[r] + p[1] * mp[4+r] + p[2] * mp[8+r] + mp[12+r];
            if (!isfinite(c[r])) return 1;
        }
        float mx = marginX + marginXW * fabsf(c[3]), my = marginYW * fabsf(c[3]);
        if (c[3] < minW) minW = c[3];
        if (c[3] > maxW) maxW = c[3];
        unsigned out = (c[0] < -c[3]-mx ? 1 : 0) | (c[0] > c[3]+mx ? 2 : 0) |
                       (c[1] < -c[3]-my ? 4 : 0) | (c[1] > c[3]+my ? 8 : 0) |
                       (c[3] <= 0 ? 16 : 0) | (farScale > 0 && c[2]*farScale > c[3] ? 32 : 0);
        all &= out;
        if (!all) return 1;
    }
    /* Stereo margins contain abs(w). Across the camera plane the side
     * tests are not affine; keeping that box avoids an interior false reject. */
    return !all || (minW <= 0 && maxW > 0);
}
#endif
