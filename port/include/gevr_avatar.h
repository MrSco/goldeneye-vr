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
#ifdef __cplusplus
}
#endif
#endif
