#include <SDL.h>
#include <opus.h>
#include <stdio.h>
#include "fs.h"
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
_Static_assert(NET_SPATIAL_SLOTS >= GEVR_MAX_PLAYERS, "a binaural effect per player slot");
static SDL_AudioDeviceID capture;
static OpusEncoder *encoder;

/*
 * What the mic gives (the phone's voice reached the Quest garbled, user
 * 2026-11-09): every ten seconds while it is open, the frames encoded and sent, the
 * stale ones thrown away and the loudest sample; and, with gevr_micdump.txt
 * in the data folder, the first 20 s as encoded in gevr_mic.wav (16 kHz mono).
 */
static struct {
    uint32_t since;
    unsigned taken, sent, cleared;
    int peak;
    FILE *wav;
    uint32_t wavFrames;
} s_micStats;

/*
 * The mic's samples go through a converter to the codec's 16 kHz, from the
 * rate the mic really runs at. SDL may say one rate and deliver another: on
 * an iPhone it reported 16000 Hz and delivered 48000 Hz (150 frames a second
 * where 50 were due, user's log 2026-11-09), so the Quest heard the phone's
 * voice three times over, slowed and chopped. The first second and a half of
 * samples is timed; a rate off by more than a sixth from the one assumed is
 * snapped to the nearest common rate and the converter made again.
 */
static SDL_AudioStream *s_micConv;
static int s_micRate;               /* the rate the converter takes */
static uint32_t s_micMeasureStart;  /* SDL_GetTicks of the first samples, 0 before */
static uint64_t s_micMeasured;      /* samples since then */
static int s_micMeasureDone;

static void micConvMake(int rate) {
    if (s_micConv) SDL_FreeAudioStream(s_micConv);
    s_micConv = SDL_NewAudioStream(AUDIO_S16SYS, 1, rate, AUDIO_S16SYS, 1, VOICE_RATE);
    s_micRate = rate;
}

static int micSnapRate(double measured) {
    static const int rates[] = { 8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 96000 };
    int best = rates[0];
    for (unsigned i = 1; i < sizeof(rates) / sizeof(rates[0]); i++) {
        if (fabs(measured - rates[i]) < fabs(measured - best)) best = rates[i];
    }
    return best;
}

/* Counts the samples taken; after 1.5 s says the rate, and fixes it if SDL's was wrong. */
static void micMeasure(unsigned samples) {
    if (s_micMeasureDone) return;
    const uint32_t now = SDL_GetTicks();
    if (!s_micMeasureStart) {
        s_micMeasureStart = now;   /* from the first delivery: the device's start-up is not counted */
        return;
    }
    s_micMeasured += samples;
    const uint32_t ms = now - s_micMeasureStart;
    if (ms < 1500) return;
    s_micMeasureDone = 1;
    const double rate = s_micMeasured * 1000.0 / ms;
    const int snapped = micSnapRate(rate);
    sysLogPrintf(LOG_NOTE, "voice: the mic runs at %.0f Hz (%d), SDL said %d", rate, snapped, s_micRate);
    if (fabs(rate - s_micRate) > s_micRate / 6.0 && s_micConv) {
        micConvMake(snapped);
        sysLogPrintf(LOG_NOTE, "voice: converting the mic from %d Hz", snapped);
    }
}

static void micWavHeader(FILE *f, uint32_t frames) {
    const uint32_t bytes = frames * VOICE_FRAME * 2;
    uint8_t h[44] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
                      0x80, 0x3E, 0, 0, 0, 0x7D, 0, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0 };   /* 16000 Hz, 32000 B/s */
    const uint32_t riff = 36 + bytes;
    for (int i = 0; i < 4; i++) {
        h[4 + i] = (uint8_t)(riff >> (8 * i));
        h[40 + i] = (uint8_t)(bytes >> (8 * i));
    }
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, sizeof(h), f);
    fseek(f, 0, SEEK_END);
}

static void micNote(const int16_t *pcm, int sent) {
    s_micStats.taken++;
    if (sent) s_micStats.sent++;
    for (int i = 0; i < VOICE_FRAME; i++) {
        const int v = pcm[i] < 0 ? -pcm[i] : pcm[i];
        if (v > s_micStats.peak) s_micStats.peak = v;
    }
    if (s_micStats.wav && s_micStats.wavFrames < 1000) {   /* 20 ms a frame: 20 s */
        fwrite(pcm, sizeof(int16_t), VOICE_FRAME, s_micStats.wav);
        s_micStats.wavFrames++;
        if (s_micStats.wavFrames == 1000) {
            micWavHeader(s_micStats.wav, s_micStats.wavFrames);
            fclose(s_micStats.wav);
            s_micStats.wav = NULL;
            sysLogPrintf(LOG_NOTE, "voice: wrote gevr_mic.wav (20 s of the mic as sent)");
        }
    }
}

static void micStatsTick(void) {
    const uint32_t now = SDL_GetTicks();
    if (!s_micStats.since) s_micStats.since = now;
    if (now - s_micStats.since < 10000) return;
    sysLogPrintf(LOG_NOTE, "voice: mic %u frames encoded, %u sent, %u queue clears, peak %d in %u ms (50 a second expected)",
                 s_micStats.taken, s_micStats.sent, s_micStats.cleared, s_micStats.peak, (unsigned)(now - s_micStats.since));
    s_micStats.since = now;
    s_micStats.taken = s_micStats.sent = s_micStats.cleared = 0;
    s_micStats.peak = 0;
}
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
    if (s_micConv) {
        SDL_FreeAudioStream(s_micConv);
        s_micConv = NULL;
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
    if (SDL_AtomicGet(&paused)) for (int i=0;i<GEVR_MAX_PLAYERS;i++) netVoiceForgetSlot((uint8_t)i);
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
#ifdef GEVR_IOS
            /* the audio session's own rate, which the game's output already
             * uses: asking for 16 kHz had SDL move the whole session to it */
            want.freq = 48000;
#else
            want.freq = VOICE_RATE;
#endif
            want.format = AUDIO_S16SYS;
            want.channels = 1;
            want.samples = VOICE_FRAME;
            capture = SDL_OpenAudioDevice(NULL, 1, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
            if (capture) {
                capture_failed = 0;
                micConvMake(have.freq);
                s_micMeasureStart = 0;
                s_micMeasured = 0;
                s_micMeasureDone = 0;
#ifdef GEVR_IOS
                /* the phone's speaker is beside its mic: Apple's echo
                 * cancellation (port/ios/ios_app.m) */
                extern void gevrIosVoiceChatMode(void);
                gevrIosVoiceChatMode();
#endif
                SDL_PauseAudioDevice(capture, 0);
                sysLogPrintf(LOG_NOTE, "voice: capture open, %d Hz %d ch %d frames, format 0x%04x",
                             have.freq, have.channels, have.samples, (unsigned)have.format);
                s_micStats.since = 0;
                if (!s_micStats.wav && fsFileSize("gevr_micdump.txt") >= 0) {
                    s_micStats.wav = fopen("gevr_mic.wav", "wb");
                    s_micStats.wavFrames = 0;
                    if (s_micStats.wav) micWavHeader(s_micStats.wav, 0);
                }
            } else {
                if (!capture_failed)
                    sysLogPrintf(LOG_ERROR, "voice: capture open failed: %s", SDL_GetError());
                capture_failed = 1;
                capture_retry_at = SDL_GetTicks() + 2000;
            }
        }
        if (capture && s_micConv) {
            /* Never transmit stale speech after a render or network stall:
             * more than 160 ms waiting (eight frames at the mic's own rate). */
            const Uint32 stale = (Uint32)(s_micRate / 50) * sizeof(int16_t) * 8;
            if (SDL_GetQueuedAudioSize(capture) > stale) {
                SDL_ClearQueuedAudio(capture);
                SDL_AudioStreamClear(s_micConv);
                s_micStats.cleared++;
            }
            int16_t raw[2048];
            Uint32 got;
            while ((got = SDL_DequeueAudio(capture, raw, sizeof(raw))) > 0) {
                micMeasure(got / sizeof(int16_t));
                SDL_AudioStreamPut(s_micConv, raw, (int)got);
            }
            for (int i = 0; i < 4 && SDL_AudioStreamAvailable(s_micConv) >= (int)(VOICE_FRAME * sizeof(int16_t)); i++) {
                int16_t pcm[VOICE_FRAME];
                uint8_t encoded[GEVR_VOIP_MAX_BYTES];
                if (SDL_AudioStreamGet(s_micConv, pcm, sizeof(pcm)) != (int)sizeof(pcm)) break;
                int bytes = opus_encode(encoder, pcm, VOICE_FRAME, encoded, sizeof(encoded));
                const int sent = bytes > 2 && bytes <= GEVR_VOIP_MAX_BYTES;
                if (sent)
                    netSendVoipChunk(send_sequence++, encoded, (uint16_t)bytes);
                micNote(pcm, sent);
            }
            micStatsTick();
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
    float gain[GEVR_MAX_PLAYERS], step[GEVR_MAX_PLAYERS], direction[GEVR_MAX_PLAYERS][3];
    int positioned[GEVR_MAX_PLAYERS] = {0};
    float forward[3], up[3];
    gevrVoiceListenerBasis(forward, up);
    /* Cross(forward, up) is the listener's right in the game's coordinate frame. */
    float right[3] = {forward[1]*up[2]-forward[2]*up[1], forward[2]*up[0]-forward[0]*up[2], forward[0]*up[1]-forward[1]*up[0]};
    for (unsigned slot=0;slot<GEVR_MAX_PLAYERS;slot++) {
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
        for (unsigned slot=0;slot<GEVR_MAX_PLAYERS;slot++) {
            if ((int)slot == netGetLocalSlot()) continue;
            float vl,vr;
            netSpatialSample(slot,streamSample(&streams[slot]),direction[slot],positioned[slot],&vl,&vr);
            float g=streams[slot].applied_gain+step[slot]*(float)(i+1);
            l+=vl*g;r+=vr*g;
        }
        stereo[2*i]=clampSample(l);stereo[2*i+1]=clampSample(r);
    }
    for(unsigned slot=0;slot<GEVR_MAX_PLAYERS;slot++) streams[slot].applied_gain=gain[slot];
}
