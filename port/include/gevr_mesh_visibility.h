#ifndef GEVR_MESH_VISIBILITY_H
#define GEVR_MESH_VISIBILITY_H
#include <math.h>

/* Conservative rejection of a mesh chunk's box. All eight corners must be
 * outside the same plane; a box spanning the camera is kept. Column-major MP.
 * Eye margins include stereo shear. Scopes pass farScale=0 (their own far plane).
 * For a camera that may still move before the box is drawn (a kept eye pass
 * replayed later, gfx_pc.cpp gevr_mesh_run_visible): marginY is a constant y
 * margin like marginX, nearW how far behind the camera plane a corner still
 * counts, farSlack how far past the far plane. */
static inline int gevrMeshBoundsVisibleSlack(const float lo[3], const float hi[3], const float mp[16],
                                           float marginX, float marginXW, float marginY, float marginYW,
                                           float nearW, float farScale, float farSlack)
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
        float mx = marginX + marginXW * fabsf(c[3]), my = marginY + marginYW * fabsf(c[3]);
        if (c[3] < minW) minW = c[3];
        if (c[3] > maxW) maxW = c[3];
        unsigned out = (c[0] < -c[3]-mx ? 1 : 0) | (c[0] > c[3]+mx ? 2 : 0) |
                       (c[1] < -c[3]-my ? 4 : 0) | (c[1] > c[3]+my ? 8 : 0) |
                       (c[3] <= -nearW ? 16 : 0) | (farScale > 0 && c[2]*farScale > c[3]+farSlack ? 32 : 0);
        all &= out;
        if (!all) return 1;
    }
    /* Stereo margins contain abs(w). Across the camera plane the side
     * tests are not affine; keeping that box avoids an interior false reject. */
    return !all || (minW <= nearW && maxW > -nearW);
}

static inline int gevrMeshBoundsVisible(const float lo[3], const float hi[3], const float mp[16],
                                      float marginX, float marginXW, float marginYW, float farScale)
{
    return gevrMeshBoundsVisibleSlack(lo, hi, mp, marginX, marginXW, 0, marginYW, 0, farScale, 0);
}

/* A kept draw's MP as the replay draws it: the vertex shader's uReproj branch
 * (gfx_opengl.cpp) takes clip back to the game frame's camera space,
 * (x / p0, y / p5, -w, 1), and on through A (projection times the head's
 * move, column-major). The 1 is the vertex's own, so it lands in column 3. */
static inline void gevrMeshReprojectMP(const float A[16], float p0, float p5, const float mp[16], float out[16])
{
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            out[c*4+r] = A[r] * mp[c*4] / p0 + A[4+r] * mp[c*4+1] / p5 - A[8+r] * mp[c*4+3] +
                         (c == 3 ? A[12+r] : 0.0f);
}
#endif
