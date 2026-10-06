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

typedef enum GevrLocomotionResetReason {
    GEVR_LOCO_RESET_NONE, GEVR_LOCO_RESET_OTHER, GEVR_LOCO_RESET_CONTEXT,
    GEVR_LOCO_RESET_TELEPORT, GEVR_LOCO_RESET_RECENTER, GEVR_LOCO_RESET_SNAP,
    GEVR_LOCO_RESET_PAUSE, GEVR_LOCO_RESET_TANK, GEVR_LOCO_RESET_TRACKING,
    GEVR_LOCO_RESET_SESSION, GEVR_LOCO_RESET_REFRESH, GEVR_LOCO_RESET_GAP,
    GEVR_LOCO_RESET_CLOCK, GEVR_LOCO_RESET_INVALID, GEVR_LOCO_RESET_SCREEN,
    GEVR_LOCO_RESET_PHYSICAL, GEVR_LOCO_RESET_COUNT
} GevrLocomotionResetReason;

typedef enum GevrLocomotionClampReason {
    GEVR_LOCO_CLAMP_NONE, GEVR_LOCO_CLAMP_EARLY, GEVR_LOCO_CLAMP_LATE,
    GEVR_LOCO_CLAMP_STALE, GEVR_LOCO_CLAMP_MISSING, GEVR_LOCO_CLAMP_COUNT
} GevrLocomotionClampReason;

typedef struct GevrLocomotionStats {
    unsigned resets[GEVR_LOCO_RESET_COUNT], clamps[GEVR_LOCO_CLAMP_COUNT];
    unsigned seeds;
    int64_t lastLead, maxLead, lastPhase, maxPhase;
} GevrLocomotionStats;

typedef struct GevrLocomotionHistory {
    GevrLocomotionPose poses[8];
    unsigned count;
    uint64_t anchorSequence;
    int64_t anchorTime, lastDisplayTime, period, delay;
    unsigned clamps;
    GevrLocomotionResetReason resetReason;
    GevrLocomotionClampReason clampReason;
    int64_t targetLead; /* delayed presentation target minus newest logical sample */
} GevrLocomotionHistory;

typedef struct GevrPresentationCamera {
    float position[3];
    float rotation[9]; /* row-major, camera right/up/back columns in game world */
} GevrPresentationCamera;

void gevrLocomotionReset(GevrLocomotionHistory *history);
const char *gevrLocomotionResetName(GevrLocomotionResetReason reason);
/* Presentation collision response: inferred horizontal contact normal and
 * rejected physical component. Neither changes the game's collision result. */
int gevrLocomotionCollision(const float step[3], const float requested[3],
    const float actual[3], float correction[3], float normal[3]);
void gevrLocomotionRebase(GevrLocomotionHistory *history, const float correction[3]);
void gevrLocomotionClipHead(const float rotation[9], const float contactDelta[3],
    const float normal[3], float translation[3]);
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
void gevrVrLocomotionResetReason(GevrLocomotionResetReason reason);
void gevrVrLocomotionSnapshot(const float position[3], const float tracking[3],
    float yaw, uint64_t sequence);
void gevrVrLocomotionCollision(const float step[3], const float requested[3], const float actual[3]);
void gevrVrCameraWorld(const float position[3], const float look[3], const float up[3]);
int gevrVrPresentationDelta(float out[16]);
void gevrVrStatsSimulation(unsigned ticks);

#ifdef __cplusplus
}
#endif
#endif
