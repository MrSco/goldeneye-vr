#ifndef GEVR_MODOPTIC_H
#define GEVR_MODOPTIC_H
/*
 * A mod's optics (mod.attach.optic, docs/mod-system.md): a lens drawn on an
 * attachment, from an optic profile in the pack (GEVROPT4). The lens draws
 * the world through the per-hand scope pass (issue #40) when it magnifies,
 * and the profile's own reticle over it, depth-tested at the attachment's
 * drawn pose, in the same hand draw as its housing.
 *
 * Profile coordinates are the pack mesh's own file frame (metres, X right,
 * Y forward, Z up), as GEVRWPN files are; they're converted with the mesh
 * (1000 x (-x, z, y)), so a profile exported with its housing lines up.
 */
#ifdef __cplusplus
extern "C" {
#endif

/* gfx_pc.cpp: a lens surface in the display list (w1: a GevrOpticSurface) */
#define G_GEVR_OPTIC_SURFACE 0x67
/* lv.c: keep the world for an optic's lens this frame (gfx_pc.cpp) */
#define GEVR_OPTIC_SCOPE_RECORD 0x566d0000u

enum { GEVR_OPTIC_COLLIMATOR = 1, GEVR_OPTIC_SCOPE, GEVR_OPTIC_NIGHT, GEVR_OPTIC_THERMAL };
enum { GEVR_OPTIC_POINTS = 64, GEVR_OPTIC_MODES = 12, GEVR_OPTIC_RETICLE_MAX = 1024 };
#define GEVR_OPTIC_MAX_FILE (12ul + GEVR_OPTIC_MODES * (92ul + GEVR_OPTIC_POINTS * 8ul \
    + GEVR_OPTIC_RETICLE_MAX * GEVR_OPTIC_RETICLE_MAX * 4ul))

/* One mode of a profile (a sight's 1x and 3x, a night or thermal view) */
typedef struct GevrOpticMode {
    unsigned kind, folded, palette, firstFocalPlane, points, width, height;
    float centre[3], right[3], up[3], forward[3];   /* in the profile's frame (see above) */
    float half[2], reticleAxis[2], angular, lowZoom, highZoom;
    float boundary[GEVR_OPTIC_POINTS][2];
    unsigned char *reticle;                          /* width x height RGBA8 */
} GevrOpticMode;

/* A lens as drawn this frame, in view space (the gun model's units) */
typedef struct GevrOpticSurface {
    const GevrOpticMode *mode;
    float centre[3], right[3], up[3], forward[3], aim[3], zero[3], half[2], zoom;
    int live;
} GevrOpticSurface;

/* A profile file's modes (gevr_modoptic.c); NULL and logged when malformed */
typedef struct GevrOpticProfile {
    int count;
    GevrOpticMode modes[GEVR_OPTIC_MODES];
} GevrOpticProfile;
const GevrOpticProfile *gevrModOpticParse(const unsigned char *bytes, unsigned long size, const char *name);
void gevrModOpticFree(GevrOpticProfile *profile);

#ifdef __cplusplus
}
#endif
#endif
