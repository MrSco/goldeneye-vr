#include <SDL.h>
#include <opus.h>
#include <math.h>
#include <string.h>
#include "net_voice.h"
#include "net_spatial.h"
#include "net_game.h"
#include "net_core.h"
#include "audio.h"
#include "system.h"
#include "game/player.h"
#include "game/bondview.h"


#define VOICE_RATE 16000
#define OUTPUT_RATE 22050
#define VOICE_FRAME 320
#define VOICE_QUEUE (VOICE_FRAME * 10)

extern int VrMicMuted;
extern float VrVoiceVolume;
extern void vrSettingsSave(void);
extern void audioVoiceIdleTick(void);

typedef struct {
    OpusDecoder *decoder;
    int16_t samples[VOICE_QUEUE];
    unsigned read, count;
    uint32_t last_sequence;
    int seen, started;
    int16_t current, next;
    unsigned phase;
    uint32_t speaking_until;
    float applied_gain;
} VoiceStream;

static VoiceStream streams[GEVR_MAX_PLAYERS];
static SDL_AudioDeviceID capture;
static OpusEncoder *encoder;
static SDL_atomic_t permission;
static SDL_atomic_t paused;
static uint32_t send_sequence;
static uint32_t capture_retry_at;
static int capture_failed;
/* Set by the game's per-player tick (net_player_sync.c) and cleared by the
 * next netPoll, which runs before it. Player structs live in the stage pool
 * and are stale between levels, so the mixer reads positions only on a tick
 * that ran the players. */
static int players_ticked;

static int voiceSession(void) {
    NetState state = netGetState();
    return state == NET_STATE_HOSTING_LOBBY || state == NET_STATE_CLIENT_LOBBY ||
           state == NET_STATE_INGAME;
}

static void voiceCloseCapture(void) {
    if (capture) {
        SDL_CloseAudioDevice(capture);
        capture = 0;
    }
    if (encoder) {
        opus_encoder_destroy(encoder);
        encoder = NULL;
    }
}

void netVoicePermissionResult(int granted) {
    SDL_AtomicSet(&permission, granted ? 1 : 0);
    sysLogPrintf(LOG_NOTE, "voice: microphone permission %s", granted ? "granted" : "denied (listen only)");
}
int netVoiceHasPermission(void) { return SDL_AtomicGet(&permission); }
int netVoiceCaptureReady(void) { return capture != 0; }
int netVoiceCaptureFailed(void) { return capture_failed; }
int netVoiceIsMuted(void) { return VrMicMuted != 0; }
int netVoiceSlotSpeaking(uint8_t slot) {
    return slot < GEVR_MAX_PLAYERS && streams[slot].speaking_until != 0 &&
           (int32_t)(streams[slot].speaking_until - SDL_GetTicks()) > 0;
}

void netVoiceSetMuted(int muted) {
    muted = muted != 0;
    if (VrMicMuted == muted) return;
    VrMicMuted = muted;
    vrSettingsSave();
    if (muted) voiceCloseCapture();
}
void netVoiceToggleMuted(void) { netVoiceSetMuted(!VrMicMuted); }
void netVoicePause(void) { SDL_AtomicSet(&paused, 1); }
void netVoiceResume(void) { SDL_AtomicSet(&paused, 0); }

void netVoiceForgetSlot(uint8_t slot) {
    if (slot >= GEVR_MAX_PLAYERS) return;
    if (streams[slot].decoder) opus_decoder_destroy(streams[slot].decoder);
    memset(&streams[slot], 0, sizeof(streams[slot]));
    netSpatialResetSlot(slot);
    if (slot == netGetLocalSlot()) {
        if (capture) SDL_ClearQueuedAudio(capture);
        if (encoder) opus_encoder_ctl(encoder, OPUS_RESET_STATE);
    }
}

void netVoiceReset(void) {
    voiceCloseCapture();
    for (unsigned i = 0; i < GEVR_MAX_PLAYERS; i++) netVoiceForgetSlot((uint8_t)i);
    netSpatialShutdown();
    send_sequence = 0;
    capture_retry_at = 0;
    capture_failed = 0;
    players_ticked = 0;
}

void netVoicePlayersTick(void) { players_ticked = 1; }
/* The level's players ran since the last netPoll, so g_playerPointers are live
 * (not a stage pool freed by a level change): events may touch them. */
int netPlayersWereTicked(void) { return players_ticked; }
void netPlayersTickedReset(void) { players_ticked = 0; }

void netVoiceTick(void) {
    players_ticked = 0;
    if (SDL_AtomicGet(&paused)) for (int i=0;i<4;i++) netVoiceForgetSlot((uint8_t)i);
    static int spatial_warned;
    if (voiceSession() && !netSpatialInit() && !spatial_warned) {
        spatial_warned = 1;
        sysLogPrintf(LOG_ERROR, "voice: binaural initialization failed (%d); using directional stereo", netSpatialLastError());
    }
    if (!voiceSession() || VrMicMuted || !SDL_AtomicGet(&permission) || SDL_AtomicGet(&paused)) {
        voiceCloseCapture();
    } else {
        if (!encoder) {
            int err;
            encoder = opus_encoder_create(VOICE_RATE, 1, OPUS_APPLICATION_VOIP, &err);
            if (!encoder || err != OPUS_OK) {
                encoder = NULL;
                return;
            }
            opus_encoder_ctl(encoder, OPUS_SET_BITRATE(24000));
            opus_encoder_ctl(encoder, OPUS_SET_DTX(1));
            opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
            /* The codec runs on the game thread, which holds 90 Hz. */
            opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(5));
        }
        if (!capture && (!capture_retry_at || (int32_t)(SDL_GetTicks() - capture_retry_at) >= 0)) {
            SDL_AudioSpec want, have;
            SDL_zero(want);
            want.freq = VOICE_RATE;
            want.format = AUDIO_S16SYS;
            want.channels = 1;
            want.samples = VOICE_FRAME;
            capture = SDL_OpenAudioDevice(NULL, 1, &want, &have, 0);
            if (capture) {
                capture_failed = 0;
                SDL_PauseAudioDevice(capture, 0);
                sysLogPrintf(LOG_NOTE, "voice: capture open, %d Hz %d ch %d frames",
                             have.freq, have.channels, have.samples);
            } else {
                if (!capture_failed)
                    sysLogPrintf(LOG_ERROR, "voice: capture open failed: %s", SDL_GetError());
                capture_failed = 1;
                capture_retry_at = SDL_GetTicks() + 2000;
            }
        }
        if (capture) {
            /* Never transmit stale speech after a render or network stall. */
            if (SDL_GetQueuedAudioSize(capture) > VOICE_FRAME * sizeof(int16_t) * 8)
                SDL_ClearQueuedAudio(capture);
            for (int i = 0; i < 4 && SDL_GetQueuedAudioSize(capture) >= VOICE_FRAME * sizeof(int16_t); i++) {
                int16_t pcm[VOICE_FRAME];
                uint8_t encoded[GEVR_VOIP_MAX_BYTES];
                if (SDL_DequeueAudio(capture, pcm, sizeof(pcm)) != sizeof(pcm)) break;
                int bytes = opus_encode(encoder, pcm, VOICE_FRAME, encoded, sizeof(encoded));
                if (bytes > 2 && bytes <= GEVR_VOIP_MAX_BYTES)
                    netSendVoipChunk(send_sequence++, encoded, (uint16_t)bytes);
            }
        }
    }
    if (netGetState() != NET_STATE_INGAME) audioVoiceIdleTick();
}

static void pushSamples(VoiceStream *s, const int16_t *pcm, unsigned n) {
    if (s->count + n > VOICE_QUEUE) {
        unsigned drop = s->count + n - VOICE_QUEUE;
        s->read = (s->read + drop) % VOICE_QUEUE;
        s->count -= drop;
    }
    for (unsigned i = 0; i < n; i++) {
        s->samples[(s->read + s->count) % VOICE_QUEUE] = pcm[i];
        s->count++;
    }
}

void netVoiceReceive(uint8_t slot, uint32_t sequence, const uint8_t *packet, uint16_t size) {
    if (slot >= GEVR_MAX_PLAYERS || !packet || !size || size > GEVR_VOIP_MAX_BYTES) return;
    if (!netVoiceSameGroup(slot, netGetLocalSlot())) { netVoiceForgetSlot(slot); return; }
    VoiceStream *s = &streams[slot];
    if (s->seen && (int32_t)(sequence - s->last_sequence) <= 0) return;
    if (!s->decoder) {
        int err;
        s->decoder = opus_decoder_create(VOICE_RATE, 1, &err);
        if (!s->decoder || err != OPUS_OK) { s->decoder = NULL; return; }
    }
    if (s->seen) {
        uint32_t lost = sequence - s->last_sequence - 1;
        if (lost > 5) {
            s->read = s->count = s->started = 0;
            opus_decoder_ctl(s->decoder, OPUS_RESET_STATE);
        } else for (uint32_t i = 0; i < lost; i++) {
            int16_t concealed[VOICE_FRAME];
            int n = opus_decode(s->decoder, NULL, 0, concealed, VOICE_FRAME, 0);
            if (n > 0) pushSamples(s, concealed, (unsigned)n);
        }
    }
    int16_t decoded[VOICE_FRAME];
    int n = opus_decode(s->decoder, packet, size, decoded, VOICE_FRAME, 0);
    if (n <= 0 || n > VOICE_FRAME) return;
    {
        int64_t energy = 0;
        for (int i = 0; i < n; i++) energy += decoded[i] < 0 ? -(int32_t)decoded[i] : decoded[i];
        if (energy / n > 320) s->speaking_until = SDL_GetTicks() + 300;
    }
    pushSamples(s, decoded, (unsigned)n);
    s->last_sequence = sequence;
    s->seen = 1;
}

static int16_t popSample(VoiceStream *s) {
    if (!s->count) return 0;
    int16_t sample = s->samples[s->read];
    s->read = (s->read + 1) % VOICE_QUEUE;
    s->count--;
    return sample;
}

static float streamSample(VoiceStream *s) {
    if (!s->started) {
        if (s->count < VOICE_FRAME * 2) return 0.0f;
        s->current = popSample(s);
        s->next = popSample(s);
        s->phase = 0;
        s->started = 1;
    }
    float sample = s->current + (s->next - s->current) * ((float)s->phase / OUTPUT_RATE);
    s->phase += VOICE_RATE;
    if (s->phase >= OUTPUT_RATE) {
        s->phase -= OUTPUT_RATE;
        s->current = s->next;
        s->next = popSample(s);
        if (!s->count && !s->next) s->started = 0;
    }
    return sample;
}

static int16_t clampSample(float value) {
    if (value > 32767.0f) return 32767;
    if (value < -32768.0f) return -32768;
    return (int16_t)value;
}

void netVoiceMix(int16_t *stereo, size_t frames) {
    if (!stereo || !voiceSession() || SDL_AtomicGet(&paused)) return;
    float gain[4], step[4], direction[4][3];
    int positioned[4] = {0};
    float forward[3], up[3];
    gevrVoiceListenerBasis(forward, up);
    /* Cross(forward, up) is the listener's right in the game's coordinate frame. */
    float right[3] = {forward[1]*up[2]-forward[2]*up[1], forward[2]*up[0]-forward[0]*up[2], forward[0]*up[1]-forward[1]*up[0]};
    for (unsigned slot=0;slot<4;slot++) {
        gain[slot]=0; direction[slot][0]=direction[slot][1]=0; direction[slot][2]=-1;
        if ((int)slot == netGetLocalSlot()) continue;
        if (!netVoiceSameGroup(slot,netGetLocalSlot())) {
            netVoiceForgetSlot((uint8_t)slot);
        } else {
            gain[slot]=VrVoiceVolume;
            if (netGetState() == NET_STATE_INGAME && players_ticked && !netSlotIsSpectator(slot)) {
                int local=netGetLocalSlot(), count=getPlayerCount();
                struct player *a=local >= 0 && local < count ? g_playerPointers[local] : NULL;
                struct player *b=(int)slot < count ? g_playerPointers[slot] : NULL;
                if (a && b && a->prop && b->prop &&
                    isfinite(a->prop->pos.x) && isfinite(a->prop->pos.y) && isfinite(a->prop->pos.z) &&
                    isfinite(b->prop->pos.x) && isfinite(b->prop->pos.y) && isfinite(b->prop->pos.z)) {
                    float delta[3]={b->prop->pos.x-a->prop->pos.x,b->prop->pos.y-a->prop->pos.y,b->prop->pos.z-a->prop->pos.z};
                    float horizontal=sqrtf(delta[0]*delta[0]+delta[2]*delta[2]);
                    if (!netVoiceSlotSpectating(local) && !netVoiceSlotSpectating(slot))
                        gain[slot] *= netVoiceDistanceGain(netVoiceModeForPair(local,slot),horizontal);
                    float distance=sqrtf(horizontal*horizontal+delta[1]*delta[1]);
                    if (distance > 1.0f) {
                        float f=0,u=0,r=0;
                        for(int j=0;j<3;j++) { f+=delta[j]*forward[j]; u+=delta[j]*up[j]; r+=delta[j]*right[j]; }
                        direction[slot][0]=r/distance; direction[slot][1]=u/distance; direction[slot][2]=-f/distance;
                        positioned[slot]=1;
                    }
                }
            }
        }
        step[slot]=frames ? (gain[slot]-streams[slot].applied_gain)/(float)frames : 0;
    }
    for (size_t i=0;i<frames;i++) {
        float l=stereo[2*i],r=stereo[2*i+1];
        for (unsigned slot=0;slot<4;slot++) {
            if ((int)slot == netGetLocalSlot()) continue;
            float vl,vr;
            netSpatialSample(slot,streamSample(&streams[slot]),direction[slot],positioned[slot],&vl,&vr);
            float g=streams[slot].applied_gain+step[slot]*(float)(i+1);
            l+=vl*g;r+=vr*g;
        }
        stereo[2*i]=clampSample(l);stereo[2*i+1]=clampSample(r);
    }
    for(unsigned slot=0;slot<4;slot++) streams[slot].applied_gain=gain[slot];
}
