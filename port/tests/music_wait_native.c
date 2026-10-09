/* Real sequence queues/voice lists; only the synthesizer and sequence parser
 * are mocked. A dropped stop must return all voices before the next track. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <libaudio.h>
#include <os.h>
#include "seqp.h"
#include "cseqp.h"
#include "system.h"

#define VOICES 16
#define EVENTS 64
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "fail %s line %d\n", #x, __LINE__); return 1; } } while (0)

static ALCSPlayer s_player;
static ALSynth s_driver;
static ALVoiceState s_voices[VOICES];
static ALEventListItem s_events[EVENTS];
static ALCSeq s_old_seq, s_new_seq;
static int s_frames, s_sleeps, s_logs, s_frees, s_stops, s_osc_stops;
static int s_block_audio, s_delay_audio, s_new_track, s_seq_steps;
static int s_freed[VOICES];

OSIntMask osSetIntMask(OSIntMask mask) { (void)mask; return 0; }
void alSynNew(ALSynth *s, ALSynConfig *c) { (void)s; (void)c; }
void alSynDelete(ALSynth *s) { (void)s; }
void alSynStopVoice(ALSynth *s, ALVoice *v)
{
    (void)v;
    if (s_logs) assert(s->paramSamples == s->curSamples);
    s_stops++;
}
void alSynFreeVoice(ALSynth *s, ALVoice *v)
{
    ALVoiceState *vs = v->clientPrivate;
    int index = (int)(vs - s_voices);
    (void)s;
    assert(index >= 0 && index < VOICES && !s_freed[index]);
    s_freed[index] = 1;
    s_frees++;
}
void alSynSetVol(ALSynth *s, ALVoice *v, s16 vol, ALMicroTime t)
{ (void)s; (void)v; (void)vol; (void)t; }
void alSynSetPriority(ALSynth *s, ALVoice *v, s16 p)
{ (void)s; (void)v; (void)p; }
void alSynSetPitch(ALSynth *s, ALVoice *v, f32 p)
{ (void)s; (void)v; (void)p; }
s16 __vsVol(ALVoiceState *vs, ALSeqPlayer *s) { (void)vs; (void)s; return 0; }
ALMicroTime __vsDelta(ALVoiceState *vs, ALMicroTime t) { (void)vs; (void)t; return 0; }
void __initFromBank(ALSeqPlayer *s, ALBank *b) { (void)s; (void)b; }
static void __setUsptFromTempo(ALCSPlayer *s, f32 t) { (void)t; s->uspt = 488; }
static void __CSPHandleNextSeqEvent(ALCSPlayer *s)
{
    assert(s_new_track && s->target == &s_new_seq);
    s_seq_steps++;
}
static void __CSPHandleMIDIMsg(ALCSPlayer *s, ALEvent *e)
{ (void)s; (void)e; assert(!"old MIDI event survived recovery"); }
static void __CSPHandleMetaMsg(ALCSPlayer *s, ALEvent *e)
{ (void)s; (void)e; assert(!"old meta event survived recovery"); }
void __CSPPostNextSeqEvent(ALCSPlayer *s)
{
    assert(s_new_track && s->target == &s_new_seq);
    s_seq_steps++;
}
static void stop_osc(void *state) { assert(state != NULL); s_osc_stops++; }
void sysLogPrintf(s32 level, const char *fmt, ...)
{ (void)level; (void)fmt; s_logs++; }
void sysSleep(s64 hns) { assert(hns == 166667); s_sleeps++; }

#include "../../src/libultra/audio/sl.c"
#include "../../src/libultra/audio/copy.c"
#include "../../src/libultra/audio/event.c"
#include "../../src/libultra/audio/cspstop.c"
#include "../../src/libultra/audio/cspgetstate.c"
#include "../../src/libultra/audio/cspsetseq.c"
#include "../../src/libultra/audio/cspplay.c"

/* INSERT_VOICE_HELPERS */
/* INSERT_FORCE_STOP */
/* INSERT_VOICE_HANDLER */

/* Same two-retrace cadence and callback scheduling as the port audio pump. */
void gevrAudioFrame(void)
{
    s32 end;
    s_frames++;
    if (s_block_audio || (s_delay_audio && s_frames < s_delay_audio)) return;
    if (!s_delay_audio && !(s_frames & 1)) return;
    end = s_driver.curSamples + 1067; /* two NTSC retraces at 32 kHz */
    while (s_player.node.samplesLeft < end)
    {
        ALMicroTime delta;
        s_driver.paramSamples = s_player.node.samplesLeft;
        delta = __CSPVoiceHandler(&s_player);
        assert(delta > 0);
        s_player.node.samplesLeft += (s32)(((s64)delta * 32000 + 999999) / 1000000);
    }
    s_driver.curSamples = end;
    s_driver.paramSamples = s_player.node.samplesLeft;
}

/* INSERT_WAIT */

static int voice_count(ALVoiceState *vs)
{
    int count = 0;
    for (; vs; vs = vs->next) { assert(count < VOICES); count++; }
    return count;
}

static int event_count(ALLink *link)
{
    int count = 0;
    for (link = link->next; link; link = link->next) { assert(count < EVENTS); count++; }
    return count;
}

static void reset(s32 state)
{
    memset(&s_player, 0, sizeof s_player);
    memset(&s_driver, 0, sizeof s_driver);
    memset(s_voices, 0, sizeof s_voices);
    memset(s_events, 0, sizeof s_events);
    memset(s_freed, 0, sizeof s_freed);
    s_frames = s_sleeps = s_logs = s_stops = s_frees = s_osc_stops = 0;
    s_block_audio = s_delay_audio = s_new_track = s_seq_steps = 0;
    s_player.drvr = &s_driver;
    s_player.state = state;
    s_player.target = &s_old_seq;
    s_player.frameTime = AL_USEC_PER_FRAME;
    s_player.nextEvent.type = AL_SEQP_API_EVT;
    s_player.vAllocHead = &s_voices[0];
    s_player.vAllocTail = &s_voices[VOICES - 1];
    for (int i = 0; i < VOICES; i++)
    {
        s_voices[i].next = i + 1 < VOICES ? &s_voices[i + 1] : NULL;
        s_voices[i].voice.clientPrivate = &s_voices[i];
        s_voices[i].envPhase = AL_PHASE_SUSTAIN;
    }
    alEvtqNew(&s_player.evtq, s_events, EVENTS);
}

static void queue_volumes(int count)
{
    ALEvent evt = {0};
    evt.type = AL_SEQP_VOL_EVT;
    for (int i = 0; i < count; i++) alEvtqPostEvent(&s_player.evtq, &evt, 0);
}

static int check_recovered(void)
{
    CHECK(s_player.state == AL_STOPPED);
    CHECK(s_player.vAllocHead == NULL && s_player.vAllocTail == NULL);
    CHECK(voice_count(s_player.vFreeList) == VOICES);
    CHECK(s_frees == VOICES && s_stops == VOICES);
    CHECK(s_player.evtq.allocList.next == NULL);
    CHECK(event_count(&s_player.evtq.freeList) == EVENTS);
    CHECK(s_player.nextEvent.type == AL_SEQP_API_EVT && s_player.nextDelta == 0);
    CHECK(s_player.node.samplesLeft == s_driver.curSamples);
    CHECK(s_player.target == NULL);
    return 0;
}

static int check_next_track(void)
{
    int frees = s_frees;
    s_new_track = 1;
    alCSPSetSeq(&s_player, &s_new_seq);
    alCSPPlay(&s_player);
    gevrAudioFrame();
    gevrAudioFrame();
    CHECK(s_player.state == AL_PLAYING && s_player.target == &s_new_seq);
    CHECK(s_seq_steps == 1);
    CHECK(s_frees == frees); /* no old note end fired */
    for (int i = 0; i < VOICES; i++)
        CHECK(__mapVoice((ALSeqPlayer *)&s_player, (u8)i, 100, 0) != NULL);
    CHECK(s_player.vFreeList == NULL && voice_count(s_player.vAllocHead) == VOICES);
    CHECK(s_frees == frees);
    return 0;
}

int main(void)
{
    ALEvent evt = {0};

    reset(AL_STOPPED);
    gevrWaitSeqStopped(&s_player);
    CHECK(s_frames == 0 && s_sleeps == 0 && s_frees == 0 && s_logs == 0);

    reset(AL_PLAYING);
    gevrWaitSeqStopped(&s_player);
    CHECK(s_player.state == AL_STOPPED && s_logs == 0 && s_frames < 120);
    CHECK(s_frees == VOICES && voice_count(s_player.vFreeList) == VOICES);

    /* The initial stop and heartbeat are dropped, then a retry succeeds. */
    reset(AL_PLAYING);
    queue_volumes(EVENTS);
    gevrWaitSeqStopped(&s_player);
    CHECK(s_player.state == AL_STOPPED && s_logs == 0 && s_frames < 120);
    CHECK(s_frees == VOICES && voice_count(s_player.vFreeList) == VOICES);

    /* A fetched stop with one queue slot left; release and final stop drop. */
    reset(AL_PLAYING);
    s_player.nextEvent.type = AL_SEQP_STOPPING_EVT;
    queue_volumes(EVENTS - 1);
    gevrWaitSeqStopped(&s_player);
    CHECK(s_frames == 120 && s_sleeps == 119 && s_logs == 1);
    CHECK(check_recovered() == 0);
    CHECK(check_next_track() == 0);

    /* Already stopping: the lost follow-up still returns every voice. */
    reset(AL_STOPPING);
    gevrWaitSeqStopped(&s_player);
    CHECK(s_frames == 120 && s_logs == 1);
    CHECK(check_recovered() == 0);
    CHECK(check_next_track() == 0);

    /* No callbacks run: discard both queued and prefetched stale events. */
    reset(AL_PLAYING);
    s_block_audio = 1;
    s_driver.curSamples = 1000;
    s_driver.paramSamples = 1000000;
    s_player.node.samplesLeft = 1000000;
    s_player.nextEvent.type = AL_NOTE_END_EVT;
    s_player.nextEvent.msg.note.voice = &s_voices[0].voice;
    evt.type = AL_SEQ_REF_EVT;
    alEvtqPostEvent(&s_player.evtq, &evt, 0);
    evt.type = AL_SEQP_ENV_EVT;
    evt.msg.vol.voice = &s_voices[0].voice;
    alEvtqPostEvent(&s_player.evtq, &evt, 0);
    evt.type = AL_NOTE_END_EVT;
    evt.msg.note.voice = &s_voices[0].voice;
    alEvtqPostEvent(&s_player.evtq, &evt, 0);
    queue_volumes(EVENTS - 3);
    gevrWaitSeqStopped(&s_player);
    CHECK(s_logs == 1 && s_driver.paramSamples == 1000000);
    CHECK(check_recovered() == 0);
    s_block_audio = 0;
    CHECK(check_next_track() == 0);

    /* Stop landing on the last allowed retrace must not trigger recovery. */
    reset(AL_STOPPING);
    s_player.nextEvent.type = AL_SEQP_STOP_EVT;
    s_delay_audio = 120;
    gevrWaitSeqStopped(&s_player);
    CHECK(s_frames == 120 && s_sleeps == 119 && s_logs == 0 && s_frees == VOICES);

    /* Oscillator cleanup must see queued events and the prefetched event. */
    reset(AL_PLAYING);
    s_player.stopOsc = stop_osc;
    s_voices[0].flags = 3;
    s_player.nextEvent.type = AL_TREM_OSC_EVT;
    s_player.nextEvent.msg.osc.vs = &s_voices[0];
    s_player.nextEvent.msg.osc.oscState = &s_old_seq;
    evt.type = AL_VIB_OSC_EVT;
    evt.msg.osc.vs = &s_voices[0];
    evt.msg.osc.oscState = &s_new_seq;
    alEvtqPostEvent(&s_player.evtq, &evt, 0);
    gevrCSPForceStop(&s_player);
    CHECK(s_osc_stops == 2 && s_voices[0].flags == 0);
    CHECK(check_recovered() == 0);

    puts("music wait: queue saturation, cleanup, heartbeat and voice reuse ok");
    return 0;
}
