#ifndef GEVR_MIRROR_H
#define GEVR_MIRROR_H

/*
 * Wreck's Mirror Mode (GEVR_CAMPAIGN_MIRROR) keeps GoldenEye's own models and
 * pictures, flipped: each level about x = 0 (rooms, collision, setups), 140
 * object models in their own x, the stage pictures; characters, hats and
 * guns are left alone. Bean (XBLA) and HD packs, which replace GoldenEye's
 * own, follow it: Bean flips its rooms, sky, those models and pictures the
 * same way (gevr_mirror.c). Measured from the cartridges with
 * tools/gevr_mirror_lists.py (docs/mirror-mode.md).
 */

#ifdef __cplusplus
extern "C" {
#endif

struct gevrBeanMesh;

/* this run is Mirror Mode */
int gevrRomMirrored(void);
/* Mirror Mode flipped this object model (a file name, "Pdesk1Z") in its own x */
int gevrMirrorPropFlipped(const char *name);
/* Mirror Mode changed this image (GoldenEye's image id) */
int gevrMirrorImageFlipped(int image);
/* x to -x for every triangle, its normals, bones and binds; each triangle's
 * last two corners swap so it still faces out */
void gevrBeanMeshMirrorX(struct gevrBeanMesh *mesh);
/* a picture's columns reversed (rgba, w x h) */
void gevrMirrorRgbaX(unsigned char *rgba, int w, int h);

#ifdef __cplusplus
}
#endif

#endif
