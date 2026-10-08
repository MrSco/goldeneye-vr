/* Sound queue: an empty queue, and a queue of only zero-delta events, must return. */
#include <string.h>
#include "snd.h"

void alSynNew(ALSynth *s, ALSynConfig *config) { (void)s; (void)config; }
void alSynDelete(ALSynth *s) { (void)s; }
OSIntMask osSetIntMask(OSIntMask mask) { (void)mask; return 0; }

#include "../../src/libultra/audio/sl.c"
#include "../../src/libultra/audio/copy.c"
#include "../../src/libultra/audio/event.c"

/* The voice handler only reads common.type. The rest of the union is unused here. */
typedef union {
    struct {
        u16 type;
        void *state;
    } common;
} ALSndpEvent;

static int s_handled;
static int s_repost;

void sndHandleEvent(ALSndPlayer *sndp, ALSndpEvent *event)
{
    ALEvent again;

    (void)event;
    s_handled++;
    if (s_repost) {
        memset(&again, 0, sizeof again);
        again.type = 1;
        alEvtqPostEvent(&sndp->evtq, &again, 0);
    }
}

/* INSERT_VOICE_HANDLER */

#define EXPORT __declspec(dllexport)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

static void newPlayer(ALSndPlayer *sndp, ALEventListItem *items, s32 count)
{
    memset(sndp, 0, sizeof *sndp);
    sndp->frameTime = 33000;
    alEvtqNew(&sndp->evtq, items, count);
}

EXPORT int test_sfx_queue(void)
{
    ALSndPlayer sndp;
    ALEventListItem items[4];
    ALEvent evt;
    ALMicroTime delta;
    s32 i;

    /* an empty queue is one frame, not a zero delta the handler would spin on */
    newPlayer(&sndp, items, 4);
    memset(&evt, 0, sizeof evt);
    delta = alEvtqNextEvent(&sndp.evtq, &evt);
    CHECK(delta == AL_USEC_PER_FRAME && evt.type == -1);

    /* the handler itself: nothing queued */
    newPlayer(&sndp, items, 4);
    sndp.nextEvent.type = -1;
    s_handled = 0;
    s_repost = 0;
    delta = sndPlayerVoiceHandler(&sndp);
    CHECK(delta == sndp.frameTime && s_handled == 0 && sndp.nextEvent.type == AL_SNDP_API_EVT);

    /* only zero-delta events, then empty: the handler returns */
    newPlayer(&sndp, items, 4);
    memset(&evt, 0, sizeof evt);
    evt.type = 1;
    for (i = 0; i < 4; i++)
        alEvtqPostEvent(&sndp.evtq, &evt, 0);
    delta = alEvtqNextEvent(&sndp.evtq, &evt);
    CHECK(delta == 0 && evt.type == 1);
    delta = alEvtqNextEvent(&sndp.evtq, &evt);
    CHECK(delta == 0);
    delta = alEvtqNextEvent(&sndp.evtq, &evt);
    CHECK(delta == 0);
    delta = alEvtqNextEvent(&sndp.evtq, &evt);
    CHECK(delta == 0);
    delta = alEvtqNextEvent(&sndp.evtq, &evt);
    CHECK(delta == AL_USEC_PER_FRAME && evt.type == -1);

    memset(&evt, 0, sizeof evt);
    evt.type = 1;
    newPlayer(&sndp, items, 4);
    for (i = 0; i < 4; i++)
        alEvtqPostEvent(&sndp.evtq, &evt, 0);
    sndp.nextDelta = alEvtqNextEvent(&sndp.evtq, &sndp.nextEvent);
    CHECK(sndp.nextDelta == 0 && sndp.nextEvent.type == 1);
    s_handled = 0;
    delta = sndPlayerVoiceHandler(&sndp);
    CHECK(delta == AL_USEC_PER_FRAME && s_handled == 4);

    /* those events post another zero-delta event as they run: the loop still ends */
    newPlayer(&sndp, items, 4);
    alEvtqPostEvent(&sndp.evtq, &evt, 0);
    sndp.nextDelta = alEvtqNextEvent(&sndp.evtq, &sndp.nextEvent);
    s_handled = 0;
    s_repost = 1;
    delta = sndPlayerVoiceHandler(&sndp);
    CHECK(delta == sndp.frameTime && s_handled > 256 && s_handled < 300);

    /* the heartbeat could not be queued; it is posted once a slot frees, and we return */
    newPlayer(&sndp, items, 4);
    s_repost = 0;
    for (i = 0; i < 4; i++)
        alEvtqPostEvent(&sndp.evtq, &evt, 0);
    sndp.nextEvent.type = AL_SNDP_API_EVT;
    s_handled = 0;
    delta = sndPlayerVoiceHandler(&sndp);
    CHECK(delta == sndp.frameTime && sndp.nextEvent.type == AL_SNDP_API_EVT && s_handled == 4);
    return 0;
}
