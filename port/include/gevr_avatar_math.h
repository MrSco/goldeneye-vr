#ifndef GEVR_AVATAR_MATH_H
#define GEVR_AVATAR_MATH_H
#include <stdint.h>

/* Protocol 22. World axes, centimetres (= game world units), xyzw quaternions.
 * Head position is relative to move.pos; wrists are relative to the head.
 * Hands are weapon roles (0 right weapon, 1 offhand), even when empty. */
enum { GEVR_POSE_HEAD = 1, GEVR_POSE_RIGHT = 2, GEVR_POSE_LEFT = 4,
       GEVR_POSE_LEFT_HANDED = 8 };
typedef struct GevrTrackedPose { float pos[3], q[4]; } GevrTrackedPose;
typedef struct GevrAvatarPose {
    GevrTrackedPose head, hand[2];
    float body_yaw; /* game's yaw in degrees */
    uint8_t flags;
} GevrAvatarPose;

#define GEVR_POSE_HISTORY 8
typedef struct GevrPoseHistory {
    GevrAvatarPose pose[GEVR_POSE_HISTORY];
    uint64_t time[GEVR_POSE_HISTORY], valid_time[3];
    uint64_t epoch;
    uint32_t life;
    float anchor[3];
    int count;
} GevrPoseHistory;

#ifdef __cplusplus
extern "C" {
#endif
int gevrPoseValidate(GevrAvatarPose *pose);
void gevrPosePush(GevrPoseHistory *h, const GevrAvatarPose *pose,
                  const float anchor[3], uint64_t epoch, uint32_t life, uint64_t now);
int gevrPoseSample(const GevrPoseHistory *h, uint64_t now, GevrAvatarPose *out, float weight[3]);
void gevrPoseQuatMatrix(const float q[4], float m[3][3]);
void gevrPoseMatrixQuat(const float m[3][3], float q[4]);
void gevrPoseQuatLerp(const float a[4], const float b[4], float t, float out[4]);
/* Returns elbow and reachable wrist; lengths are never stretched. */
int gevrArmSolve(const float shoulder[3], const float target[3], float upper, float lower,
                 const float pole[3], const float previous[3], float elbow[3], float wrist[3]);
#ifdef __cplusplus
}
#endif
#endif
