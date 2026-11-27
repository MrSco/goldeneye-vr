#ifndef GEVR_AVATAR_H
#define GEVR_AVATAR_H
#include <bondtypes.h>
#include "gevr_avatar_math.h"
struct player;
#ifdef __cplusplus
extern "C" {
#endif
void gevrAvatarCapture(GevrAvatarPose *pose, struct player *player);
void gevrAvatarApply(ChrRecord *chr, ModelRenderData *data);
/* Active only during the avatar's second matrix build. */
int gevrAvatarJointRotation(Model *model, ModelNode *node, coord3d *rotation);
void gevrAvatarJointOffset(Model *model, ModelNode *node);
RenderPosView *gevrAvatarCollisionPose(Model *model);
/* Weapon roles follow physical hands in left-handed mode. */
ModelNode *gevrAvatarWeaponHand(ChrRecord *chr, int hand, int *physicalLeft);
/* mod.attach on another player (#335): its hand's (on 1) or body anchor's (on 2)
 * frame, view space, unit cm a gun model's unit; 0 untracked */
int gevrAvatarAttachFrame(int slot, int on, int where, const float bodyAt[3], float unit, Mtxf *out);
/* the same frame from a player's body model as just drawn, for one without
 * tracking (a flat player, a bot, the flat third person's own body); 0 none */
int gevrAvatarModelAttachFrame(ChrRecord *chr, int on, int where, const float bodyAt[3], float unit, Mtxf *out);
#ifdef __cplusplus
}
#endif
#endif
