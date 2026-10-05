#ifndef GEVR_LOCOMOTION_H
#define GEVR_LOCOMOTION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Game units (centimetres), degrees, and OpenXR nanoseconds. No game state
 * lives here: these snapshots are used only to present confirmed movement. */
typedef struct GevrLocomotionPose {
    float position[3];
    float yaw;
    uint64_t sequence;
    int64_t time;
} GevrLocomotionPose;

typedef struct GevrLocomotionHistory {
    GevrLocomotionPose poses[8];
    unsigned count;
    uint64_t anchorSequence;
    int64_t anchorTime, lastDisplayTime, period, delay;
    unsigned clamps;
} GevrLocomotionHistory;

typedef struct GevrPresentationCamera {
    float position[3];
    float rotation[9]; /* row-major, camera right/up/back columns in game world */
} GevrPresentationCamera;

void gevrLocomotionReset(GevrLocomotionHistory *history);
int64_t gevrLocomotionDelay(int64_t displayPeriod);
int gevrLocomotionSnapshot(GevrLocomotionHistory *history, const float position[3],
    const float tracking[3], float yaw, uint64_t sequence, int64_t displayTime,
    int64_t displayPeriod);
int gevrLocomotionQuery(GevrLocomotionHistory *history, int64_t displayTime,
    GevrLocomotionPose *pose);
/* Compose current tracking exactly once with interpolated locomotion. */
void gevrLocomotionCamera(const GevrPresentationCamera *source,
    const GevrLocomotionPose *confirmed, const GevrLocomotionPose *presented,
    const float headRotationDelta[9], const float headTranslation[3],
    GevrPresentationCamera *camera);
void gevrCameraDelta(const GevrPresentationCamera *source,
    const GevrPresentationCamera *camera, float scale, float out[16]);
void gevrMat4Multiply(const float a[16], const float b[16], float out[16]);
void gevrPresentationClip(const float delta[16], const float projection[16],
    const float clip[4], float out[4]);

/* C bridge into the XR/rendering layer. Never writes a player's simulation. */
void gevrVrLocomotionReset(void);
void gevrVrLocomotionSnapshot(const float position[3], const float tracking[3],
    float yaw, uint64_t sequence);
void gevrVrCameraWorld(const float position[3], const float look[3], const float up[3]);
int gevrVrPresentationDelta(float out[16]);
void gevrVrStatsSimulation(unsigned ticks);

#ifdef __cplusplus
}
#endif
#endif
