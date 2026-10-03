#ifndef GEVR_PAUSE_INPUT_H
#define GEVR_PAUSE_INPUT_H
/* Raw triggers feed the UI; game fire stays blocked until release after closing. */
typedef struct { int wasOpen, releaseFire; } GevrPauseInputState;
static inline int gevrPauseBlocksFire(GevrPauseInputState *state,int open,int triggerHeld) {
    if(state->wasOpen && !open) state->releaseFire=1;
    if(!triggerHeld) state->releaseFire=0;
    state->wasOpen=open;
    return open || state->releaseFire;
}
#endif
