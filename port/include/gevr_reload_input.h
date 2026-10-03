#ifndef GEVR_RELOAD_INPUT_H
#define GEVR_RELOAD_INPUT_H

/* Gun indices, not controller indices: bit 0 is the dominant gun. */
typedef struct { unsigned held, pending, context; } GevrReloadInput;

static inline void gevrReloadInputUpdate(GevrReloadInput *state, unsigned held,
                                        unsigned allowed, unsigned context)
{
    held &= 3;
    if (state->context != context) {
        state->context = context;
        state->held = held;
        state->pending = 0; /* a held button must not cross a stage/mode change */
        return;
    }
    state->pending = (state->pending | (held & ~state->held)) & allowed;
    state->held = held; /* track suppressed presses too: release before retrying */
}

static inline unsigned gevrReloadInputTake(GevrReloadInput *state)
{
    unsigned pending = state->pending;
    state->pending = 0;
    return pending;
}

static inline unsigned gevrReloadTargets(unsigned requested, int rightEquipped, int leftEquipped)
{
    unsigned equipped = (rightEquipped ? 1u : 0u) | (leftEquipped ? 2u : 0u);
    return requested ? (equipped == 3 ? requested & equipped : equipped) : 3;
}

int gevrVrReloadPressedMask(void);
int gevrVrReloadHeldMask(void);
int gevrVrTakeReloadMask(void);
#endif
