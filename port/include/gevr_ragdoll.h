#ifndef GEVR_RAGDOLL_H
#define GEVR_RAGDOLL_H
#include <ultra64.h>
#ifdef __cplusplus
extern "C" {
#endif
struct ChrRecord;
struct Model;
struct ShotData;
struct coord3d;

/* Cosmetic snapshots own their runtime model data and survive actor reuse.
 * Never change authoritative health, position, hitboxes or network messages. */
void gevrRagdollReset(void);
void gevrRagdollTick(int ticks60);
int gevrRagdollNeedsPose(struct ChrRecord *chr);
void gevrRagdollCapture(struct ChrRecord *chr);
void gevrRagdollCapturePlayer(struct ChrRecord *chr);
int gevrRagdollSuppress(struct ChrRecord *chr);
void gevrRagdollForgetChr(struct ChrRecord *chr);
void gevrRagdollNoteHit(struct ChrRecord *chr, int matrix, int hitpart, const float dir[3], int weapon);
void gevrRagdollShot(struct ShotData *shot);
void gevrRagdollVolley(int begin);
struct coord3d;
void gevrRagdollExplosionChr(struct ChrRecord *chr, const struct coord3d *centre, float radius, float damage);
void gevrRagdollBlast(const struct coord3d *centre, float radius, float damage);
Gfx *gevrRagdollRender(Gfx *gdl);

/* Bean's cut triangles are posed on the CPU. Map cross-cut influences into
 * their triangle's chosen piece at the captured pose, so skin cannot stretch. */
int gevrRagdollModelCut(const struct Model *model);
unsigned gevrRagdollModelBody(const struct Model *model);
int gevrRagdollPiece(const struct Model *model, int matrix);
int gevrRagdollRemap(const struct Model *model, int owner, int matrix, float p[3], float n[3]);
void gevrRagdollBeanCap(const struct Model *model, int matrix, const s16 (*rim)[2][3], int nedges, int piece, int other);
/* A moving door (propobj.c sub_GAME_7F0448A8): the body slots to look at (0 with ragdolls
 * off), a body's part positions in world units (0 when the slot is empty or already
 * fading), and the half-second fade of one it touched. The door never waits: doors keep
 * the original timing with ragdolls on or off (chraction.c gevrBodyDoorHit). */
#define GEVR_RAGDOLL_PARTS_MAX 16
int gevrRagdollBodies(void);
int gevrRagdollBodyParts(int body, struct coord3d *pos, int max);
void gevrRagdollDoorFade(int body);
#ifdef __cplusplus
}
#endif
#endif
