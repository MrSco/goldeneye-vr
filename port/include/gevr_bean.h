#ifndef GEVR_BEAN_H
#define GEVR_BEAN_H

#include <PR/ultratypes.h>

struct ModelFileHeader;
struct Model;
struct fontchar;
void gevrBeanFontRepaint(struct fontchar *glyph, int gothic, int character);
void gevrBeanFontReset(void);

#ifdef __cplusplus
extern "C" {
#endif

/* Project Bean meshes (CAFF) in place of GoldenEye's N64 models.
 * Downloaded files live under ../texture-packs/ge007-bean/files/new/;
 * added-content/xbla/files/new/ remains supported for manual installs. */

void gevrBeanInit(void);
void gevrBeanBeginFrame(void);
/* authored: one of GoldenEye's own cameras (the opening, death and ending cameras) */
void gevrBeanWorldBeginView(int authored);
/* this view's camera stands outside the level: HD rooms cull their backs */
int gevrBeanWorldCullsBackFaces(void);
int gevrBeanIsAvailable(void);
int gevrBeanIsEnabled(void);
void gevrBeanSetEnabled(int on);
void gevrBeanToggle(void);

/* After a model file's pointers are promoted. Does not walk nodes on reset. */
void gevrBeanForgetModel(struct ModelFileHeader *header);
void gevrBeanForgetModelStorage(const void *base, u32 size);
void gevrBeanModelApply(struct ModelFileHeader *header, const char *name);
/* The watch initializes only native preview joints. Fit every replaced rigid
 * part under its own rest pivot; return whether this is a Bean preview. */
int gevrBeanPreviewPose(struct Model *model, const float root[4][4]);
/* DAB fpRound: the rocket launcher being drawn has a round in its tube, and
 * whether a model draws the release's own round (GoldenEye's rocket is not
 * drawn in its mouth then) */
void gevrBeanSetLoaded(int loaded);
int gevrBeanHasRound(const void *header);
/* whether the release draws any of a model's lists now */
int gevrBeanDrawsModel(const void *header);
void gevrBeanReset(void);

/* Stand-in tile address -> decoded RGBA8. 1 when addr is one of ours. */
int gevrBeanTexture(const void *addr, const uint8_t **rgba, int *w, int *h);

/* Native texture address -> the release's picture painted over it (the
 * briefing folder, gevr_bean_folder.c). Bound while Bean is off too; the
 * renderer asks only while it is on. */
int gevrBeanRepaint(const void *addr, const uint8_t **rgba, int *w, int *h);
void gevrBeanRepaintStats(unsigned counts[3]); /* folder, font, image bindings */
/* Who bound a repaint, so each can drop its own. */
#define GEVR_PAINT_FOLDER 1   /* gevr_bean_folder.c: until the folder is unloaded */
#define GEVR_PAINT_FONT   2   /* gevr_bean_font.c */
#define GEVR_PAINT_IMAGE  3   /* gevr_bean_images.c: per image id, as each loads */
/* Drops every repaint: the texture pool is reset or Bean resets. */
void gevrBeanRepaintForget(void);
void gevrBeanRepaintForgetOwner(int owner);
/* Drops the repaint at one address: another texture took it. */
void gevrBeanRepaintUnbind(const void *addr);
/* texLoad: image (-1 for none of GoldenEye's) just loaded to data; G_IM_SIZ depth.
 * Binds the release's own picture for it (gevr_bean_images.c). */
void gevrBeanImageLoaded(int image, const void *data, int width, int height, int depth);
void gevrBeanImagesReset(void);

/* Normalized game-time texture offsets and the active HD room fog line. */
int gevrBeanTextureScroll(const void *addr, float seconds, float offset[2]);
int gevrBeanTextureTransform(const void *addr, float seconds, float matrix[6]);
const void *gevrBeanTextureFrame(const void *addr, float seconds);
int gevrBeanRoomFog(float line[2], uint8_t rgb[3]);
/* Render-time selection only. Definitions retain native lists/types for
 * gameplay, including aiming and bullet collision. Disabled returns current. */
void *gevrBeanShownGdl(void *node, void *current);
/* One G_GEVR_BEAN_FADE command at gdl (alpha 0-255, 255 ends the fade); returns the next. */
void *gevrBeanFadeMark(void *gdl, int alpha);
/* A destroyed prop's Bean list (objDeform's blackened, crumpled vertex copy): a frame copy
 * that follows it, or shown when there is none to make. */
void *gevrBeanScorchedGdl(void *node, void *shown, const void *stockVertices, const void *nowVertices, int count);
/* objDeform's vertex copy (propobj.c), until the vertex store frees it (vtxstore.c) */
void gevrBeanMarkDeformed(const void *vertices);
void gevrBeanForgetDeformed(const void *vertices);
int gevrBeanIsDeformed(const void *vertices);
void *gevrBeanRenderGdl(void *node, void *current, struct Model *model);
void *gevrBeanWholeModelGdl(struct Model *model);
struct ModelHitEntry;
void *gevrBeanWholeJointGdl(struct ModelHitEntry *entry,struct ModelHitEntry *first);
int gevrBeanShownModelType(void *node, int current);
/* DAB xblaMeshApplyNodeMode: Bean's model lists draw under the render mode the
 * game sets for the node they replace (files/gevr_bean_nodemode.txt 0: their own). */
int gevrBeanNodeMode(void);
/* The type whose render mode a node's draw applies: the replaced list's own under
 * node mode, else current. */
int gevrBeanNodeModeType(void *node, int current);
/* G_GEVR_NODE_STATE ON (on) or OFF at gdl; returns the next. */
void *gevrBeanNodeMark(void *gdl, int on);
/* One of GoldenEye's own cameras stands inside this Bean prop's bounds: draw it
 * between gevrBeanCullMark(gdl, 1) and (gdl, 0), its backs culled. */
int gevrBeanCameraInside(void *node, struct Model *model);
void *gevrBeanCullMark(void *gdl, int on);
/* The release's Rare logo (new/prop/rarewarelogo) for title.c, fitted to the
 * cartridge logo's box lo..hi: lit, texture-generated triangles to draw with
 * *picture as the environment map. NULL without Bean or the file. */
void *gevrBeanRareLogoGdl(const float lo[3], const float hi[3], const void **picture);
/* The opaque pass drew this model instance's Bean lists (model.c): the
 * translucent pass does not draw them again (gevrBeanTransparentGdl). */
void gevrBeanNoteOpaque(const void *model);
/* Self-contained replacement in a transparent-only native type-4 draw. */
void *gevrBeanTransparentGdl(void *node, void *current, struct Model *model, int flags);
/* A prop list with solid and translucent batches, split over the passes: 1 the
 * opaque pass draws its solid batches, 2 the translucent pass its translucent
 * ones, 0 the whole list. Draw it between gevrBeanPassMark(gdl, pass) and (gdl, 0). */
int gevrBeanPassSplit(void *node, struct Model *model, int flags);
void *gevrBeanPassMark(void *gdl, int pass);
/* GoldenEye's monitor screens under Bean's meshes (DAB xblamesh.c
 * xblaMeshSetScreens()): the monitor whose programmes propobj.c has just
 * written, around its model's opaque render; NULL after. Its part 0-3 lists
 * the programmes wrote are drawn as the game's, not Bean's. */
void gevrBeanSetScreens(struct Model *model);
/* DAB xblaMeshScreenQuad(): moves a screen's four corners onto Bean's own
 * screen when the model is drawn as Bean's. 1 when the screen lies on the back
 * of Bean's recess, to be drawn in the decal z mode. */
struct Vertex;
int gevrBeanScreenQuad(struct Model *model, void *node, struct Vertex *vertices);

/* HD level rooms. Advances the display list. *replaced is 1 when the N64
 * room list should be left out. The second call is the transparent pass. */
void *gevrBeanRoomAppend(void *gdl, int room, int *replaced);
void *gevrBeanRoomAppendSecondary(void *gdl, int room, int *replaced);

/* HD sky. *drew is 1 when GoldenEye's sky plane should be left out. */
void *gevrBeanSkyAppend(void *gdl, int *drew);

#ifdef __cplusplus
}
#endif

#endif
