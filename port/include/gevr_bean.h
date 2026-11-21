#ifndef GEVR_BEAN_H
#define GEVR_BEAN_H

#include <PR/ultratypes.h>

struct ModelFileHeader;
struct Model;
/* Optional presentation provider. With no provider the normal GPU path is
 * unchanged, including standalone model tools and native regression fixtures. */
typedef struct GevrBeanCuts {
    int (*active)(const struct Model *model);
    int (*piece)(const struct Model *model, int matrix);
    int (*remap)(const struct Model *model, int owner, int matrix, float p[3], float n[3]);
    /* A hole's rim in the skin, posed in matrix's space: the piece it is on
     * and the piece across the cut (optional) */
    void (*cap)(const struct Model *model, int matrix, const s16 (*rim)[2][3], int nedges, int piece, int other);
    /* The body's own number, never another's (optional) */
    unsigned (*body)(const struct Model *model);
} GevrBeanCuts;
void gevrBeanSetCuts(const GevrBeanCuts *cuts);
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
/* the first of the release's level surfaces on a shot between near and far
 * (world units), for its hole (gevr_bean_shots.inc.h; chrprop.c looks within
 * GEVR_BEAN_HOLE_REACH of the game's hit: Streets' rebuilt fronts stand 13 off) */
#define GEVR_BEAN_HOLE_REACH 50.0f
int gevrBeanWorldShotSurface(const float from[3], const float dir[3], float near, float far,
                             float hit[3], float normal[3]);
void gevrBeanSetEnabled(int on);
void gevrBeanToggle(void);

/* After a model file's pointers are promoted. Does not walk nodes on reset. */
void gevrBeanForgetModel(struct ModelFileHeader *header);
void gevrBeanForgetModelStorage(const void *base, u32 size);
/* An instance's runtime records are about to be initialized or reused. */
void gevrBeanForgetWounds(struct Model *model);
void gevrBeanModelApply(struct ModelFileHeader *header, const char *name);
/* Load a model's Bean mesh now, ahead of its first use (the watch arm at level load) */
void gevrBeanPrewarm(const char *name);
/* A level's front-load (#269): the loading screen's hook (flat screens) and
 * its steps; gevr_bean_load.c */
void gevrBeanSetLoadingHook(void (*hook)(float progress));
void gevrBeanLoadingBegin(void);
void gevrBeanLoadingTick(float progress);
void gevrBeanLoadingEnd(void);
/* Bean's level and sky built now, not on their first frame (gevr_bean_world.c) */
int gevrBeanWorldPrepare(void);
/* The level's decoded Bean pictures: stand-in address, RGBA, size (gevr_bean_tex.c) */
int gevrBeanTexEach(int index, const void **addr, const unsigned char **rgba, int *w, int *h);
/* The watch initializes only native preview joints. Fit every replaced rigid
 * part under its own rest pivot; return whether this is a Bean preview. */
int gevrBeanPreviewPose(struct Model *model, const float root[4][4]);
/* DAB fpRound: the rocket launcher being drawn has a round in its tube, and
 * whether a model draws the release's own round (GoldenEye's rocket is not
 * drawn in its mouth then) */
void gevrBeanSetLoaded(int loaded);
int gevrBeanHasRound(const void *header);
/* A magazine-fed gun's removable magazine (gevr_beanmag.h): made with the gun
 * when the gun is laid out. The gun's body list is drawn with the magazine in
 * (as the release made it), with it out and the well empty, or as the magazine
 * alone, which sits in the gun under the body's own matrix. Points are in that
 * list's space: the body matrix takes them to the view. */
/* ... or as its see-through silhouette: the placeholder in the empty well and
 * at the belt, GoldenEye X's colours (gevr_hand_reload.h), _READY's when the
 * hand is there to take or seat it. */
enum { GEVR_BEAN_MAG_IN, GEVR_BEAN_MAG_OUT, GEVR_BEAN_MAG_ITSELF, GEVR_BEAN_MAG_GHOST, GEVR_BEAN_MAG_GHOST_READY };
struct gevrBeanMagInfo {
	void *node;      /* the body's list */
	int matrix;      /* and its matrix */
	float mouth[3];  /* the well's mouth */
	float top[3];    /* the seated magazine's top */
	float foot[3];   /* its foot: where a hand takes it */
	float hold[3];   /* where the off hand's palm has it */
	float axis[3];   /* up the well, unit */
	int pullUp;      /* it lifts off the top of the gun (the RC-P90's) */
	int single;      /* a loose round pushed in one at a time (a shotgun's shell): gevr_beanmag.h */
	int consumed;    /* fired, it is gone (the rocket) */
	int noPull;      /* never pulled out by hand */
};
void gevrBeanSetMagazine(int state);
int gevrBeanGunMagazine(const void *header, struct gevrBeanMagInfo *out);
/* whether the release draws any of a model's lists now */
int gevrBeanDrawsModel(const void *header);
/* Bean has an HD list on one of the model's nodes, cached (model.c's detail level) */
int gevrBeanModelHasHd(const void *header);
/* around a prop the game draws without depth on purpose (propobj.c): Bean's lists follow */
void *gevrBeanNodeOnTop(void *gdl, int on);
/* a shot (view space) on the Bean body or head a chr's model draws: its distance in dir's
 * lengths and the matrix the hit triangle hangs from (gevr_bean_bodyhit.inc.h, chr.c) */
int gevrBeanChrRayHit(void *chrModel, const float origin[3], const float dir[3], float *t, int *matrix);
/* a shot at one of a prop's lists, on the triangles Bean draws: 1 hit, 0 miss, -1 the game's own (propobj.c) */
struct HitThing;
int gevrBeanHitNode(void *node, const float pos[3], const float dir[3], struct HitThing *hit);
/* a hat on a head Bean draws, seated by the HD meshes: an offset, a size and the point
 * it is sized about, in the hat's own space (gevr_bean_hats.inc.h; chr.c) */
int gevrBeanHatSeat(void *chrModel, const void *headHeader, void *hatModel, const float joint[4][4], int hattype,
                    float seat[7]);
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
/* A Bean water picture's tile 0 or 1 under GoldenEye's motion; 0 for others. */
int gevrBeanWaterTransform(const void *addr, int tile, float matrix[6]);
/* GoldenEye's water tick (unk_092E50.c): its delta and fade phase. */
void gevrBeanWaterTick(float delta, float phase);
const void *gevrBeanTextureFrame(const void *addr, float seconds);
int gevrBeanRoomFog(float line[2], uint8_t rgb[3]);
/* Render-time selection only. Definitions retain native lists/types for
 * gameplay, including aiming and bullet collision. Disabled returns current. */
void *gevrBeanShownGdl(void *node, void *current);
/* One G_GEVR_BEAN_FADE command at gdl (alpha 0-255, 255 ends the fade); returns the next. */
void *gevrBeanFadeMark(void *gdl, int alpha);
/* Whether the node's Bean list shows a destroyed prop: 1 its own vertices are objDeform's
 * wreck copy, 2 those of the list whose part Bean draws in it (the CCTV's mount), 0 neither. */
int gevrBeanWreck(void *node, struct Model *model);
/* A destroyed prop's Bean list (objDeform's blackened, crumpled vertex copy): a frame copy
 * that follows it, or NULL when there is none to make (the caller draws the game's wreck). */
void *gevrBeanScorchedGdl(void *node, struct Model *model);
/* A destroyed prop's Bean list that follows no crumpled list: drawn as nothing, as the
 * cartridge draws its lists but the crumpled one (Bunker II's ceiling guns). */
int gevrBeanWreckHides(void *node, struct Model *model);
/* The node's Bean part is drawn in another node's list (it shows Bean's blank). */
int gevrBeanDrawsElsewhere(void *node);
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
/* a GoldenEye room the HD level hides: it stops no shot (bg.c) */
int gevrBeanRoomHidden(int room);
/* a prop left out of this view: one beside Aztec's armour closet, the camera
 * in it (gevr_bean_world.c keepOutOfRooms()) */
int gevrBeanHidesProp(const float pos[3], const unsigned char *rooms, int nrooms, int camRoom);
/* Bean draws this room and no water of its own: keep GoldenEye's water (bg.c) */
int gevrBeanRoomWantsWater(int room);
/* how far the HD level reaches, world units, 0 when it is not drawn: the far tests take it (bg.c, bgfog.c) */
float gevrBeanFarReach(void);
void *gevrBeanRoomAppendSecondary(void *gdl, int room, int *replaced);

/* HD sky. *drew is 1 when GoldenEye's sky plane should be left out. */
void *gevrBeanSkyAppend(void *gdl, int *drew);

#ifdef __cplusplus
}
#endif

#endif
