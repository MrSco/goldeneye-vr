#ifndef GEVR_CAMERA_COLLISION_H
#define GEVR_CAMERA_COLLISION_H

#ifdef __cplusplus
extern "C" {
#endif

/* World units, before the level's visibility scale. Only lowers the eye;
 * walking, crouch controls and the player's collision cylinder are unchanged. */
void gevrBgClampCameraCeiling(float position[3], float floorY, int room, int loadRooms);
/* Recheck the local camera after display-time tracking/interpolation. Uses the
 * context saved by its game camera, not whichever player was ticked last. */
void gevrClampCameraCeiling(float position[3]);
/* Height removed from the current game camera, for absolute XR composition. */
float gevrCameraCeilingDrop(void);

#ifdef __cplusplus
}
#endif
#endif
