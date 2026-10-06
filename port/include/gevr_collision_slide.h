#ifndef GEVR_COLLISION_SLIDE_H
#define GEVR_COLLISION_SLIDE_H
#ifdef __cplusplus
extern "C" {
#endif
/* A retry target for a rejected edge slide. It still requires the game's
 * normal collision test; this helper never accepts or moves a player. */
int gevrCollisionSlideRetry(const float start[3], const float target[3],
    const float edge0[3], const float edge1[3], float out[3]);
#ifdef __cplusplus
}
#endif
#endif
