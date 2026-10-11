#ifndef GEVR_BEAN_H
#define GEVR_BEAN_H

#include <PR/ultratypes.h>

struct ModelFileHeader;
struct Model;

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
void gevrBeanModelApply(struct ModelFileHeader *header, const char *name);
void gevrBeanReset(void);

/* Stand-in tile address -> decoded RGBA8. 1 when addr is one of ours. */
int gevrBeanTexture(const void *addr, const uint8_t **rgba, int *w, int *h);

/* While the toggle is off, a replaced node draws the list and render type it
 * had before. The definition is left pointing at the Bean list. */
void *gevrBeanShownGdl(void *node, void *current);
void *gevrBeanRenderGdl(void *node, void *current, struct Model *model);
int gevrBeanShownModelType(void *node, int current);

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
