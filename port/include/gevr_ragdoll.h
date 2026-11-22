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

/* Jedi and Ninja modes (#270): telekinetic body/limb grabbing, cutting and 24s fade. */
int gevrRagdollForceCutNear(const float pos[3], float radiusCm, const float impulseVel[3], int maxCuts);
int gevrRagdollFindNearestBody(const float from[3], const float dir[3], float maxDistCm, float minDot, float outPos[3]);
void gevrRagdollPullBody(int slot, const float target[3], float maxForce, float freq);
void gevrRagdollThrowBody(int slot, const float vel[3]);
int gevrRagdollGetBodyPos(int slot, float outPos[3]);
int gevrRagdollPullLooseLimbs(const float from[3], const float center[3], float maxDistCm, float timeSec, int maxLimbs);
int gevrRagdollLaunchLooseLimbs(const float center[3], const float aimDir[3], float speedCmS, float radiusCm);
#ifdef __cplusplus
}
#endif
#endif
