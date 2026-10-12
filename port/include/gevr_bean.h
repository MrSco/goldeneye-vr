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
void *gevrBeanRenderGdl(void *node, void *current, struct Model *model);
int gevrBeanShownModelType(void *node, int current);
/* Self-contained replacement in a transparent-only native type-4 draw. */
void *gevrBeanTransparentGdl(void *node, void *current, struct Model *model, int flags);

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
