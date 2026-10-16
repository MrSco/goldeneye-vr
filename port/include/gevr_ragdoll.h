#ifndef GEVR_RAGDOLL_H
#define GEVR_RAGDOLL_H
#include <ultra64.h>
#ifdef __cplusplus
extern "C" {
#endif
struct ChrRecord;
struct Model;
struct ShotData;

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
Gfx *gevrRagdollRender(Gfx *gdl);

/* Bean's cut triangles are posed on the CPU. Map cross-cut influences into
 * their triangle's chosen piece at the captured pose, so skin cannot stretch. */
int gevrRagdollModelCut(const struct Model *model);
int gevrRagdollPiece(const struct Model *model, int matrix);
int gevrRagdollRemap(const struct Model *model, int owner, int matrix, float p[3], float n[3]);
#ifdef __cplusplus
}
#endif
#endif
