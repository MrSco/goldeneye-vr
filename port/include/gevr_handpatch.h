#ifndef _GEVR_HANDPATCH_H_
#define _GEVR_HANDPATCH_H_

#include <PR/ultratypes.h>

/*
 * Hand and arm patches for the first-person models (issue #9).
 *
 * The N64 drew Bond's hands and arms from one side only, so the models are
 * open shells: no palm, open sleeve ends, the watch band's back half missing.
 * In VR they are seen from every side. The patches close them with triangles
 * made in Blender (tools/blender/gevr_hands_patch.py, tools/handpatch/),
 * compiled into gevr_handpatch_data.c by tools/gevr_handpatch_gen.py.
 *
 * A patch holds only what we add. Its corners are the model's own vertices,
 * named by DL node (the node's offset in the cartridge file) and index in that
 * node's vertex block, or new points given as weights over such vertices; the
 * game computes every position from the player's ROM. Each node also carries a
 * fingerprint of its vertex block, and a patch is skipped whole unless every
 * node it uses matches.
 *
 * At load: the converter tells us where each cartridge node landed and the
 * texture marker words of the model's lists (gevrHandPatchWants/Note*); then
 * load_object_fill_header() calls gevrHandPatchApply(), which builds each
 * patch's display list with the game's own texture expansion and points its
 * host node at [the node's own list, the patch].
 *
 * files/gevr_handpatch.txt (read at each model load): 0 leaves the models
 * alone, 2 tints the patches magenta, anything else (or no file) patches.
 */

struct gevrHpNode   { u32 ofs; u16 numvtx; u32 fnv; };
struct gevrHpCorner { u32 node; s16 ref; s16 s, t; u8 mtx; u8 nmix; u16 firstMix; };  /* ref -1: a mix */
struct gevrHpMix    { u32 node; u16 idx; f32 w; };
struct gevrHpGroup  { u16 tex; u8 shade[4]; u16 firstCorner, numCorners; u16 firstTri, numTris; };
struct gevrHpPart   { u32 host; u16 firstNode, numNodes; u16 firstGroup, numGroups; };
struct gevrHpModel  { const char *name; u16 firstPart, numParts; };

extern const struct gevrHpNode g_gevrHpNodes[];
extern const struct gevrHpCorner g_gevrHpCorners[];
extern const struct gevrHpMix g_gevrHpMixes[];
extern const u16 g_gevrHpTris[][3];
extern const struct gevrHpGroup g_gevrHpGroups[];
extern const struct gevrHpPart g_gevrHpParts[];
extern const struct gevrHpModel g_gevrHpModels[];
extern const s32 g_gevrHpNumModels;

struct ModelFileHeader;

/* From gevrModelConvert(), for a model with a patch: where each cartridge node
 * landed in the host file, and each texture marker's first word. */
s32 gevrHandPatchWants(const char *name);
void gevrHandPatchNoteNode(u32 src, u32 dst);
void gevrHandPatchNoteMarker(u32 texnum, u32 w0);

/* From load_object_fill_header(), once the model's lists are expanded. */
void gevrHandPatchApply(struct ModelFileHeader *header, const char *name, void *texpool);

#endif
