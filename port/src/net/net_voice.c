#include <SDL.h>
#include <opus.h>
#include <math.h>
#include <string.h>
#include "net_voice.h"
#include "net_core.h"
#include "audio.h"
#include "system.h"
#include "game/player.h"
#include "game/bondview.h"
#include "game/stan.h"

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
}

void netVoiceReset(void) {
    voiceCloseCapture();
    for (unsigned i = 0; i < GEVR_MAX_PLAYERS; i++) netVoiceForgetSlot((uint8_t)i);
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

static int playersShareRoom(struct player *listener, struct player *speaker) {
    if (!listener || !speaker) return 0;
    if (listener->prop && speaker->prop) {
        size_t max_rooms = sizeof(listener->prop->rooms);
        for (size_t i = 0; i < max_rooms && listener->prop->rooms[i] != 0xff; i++) {
            uint8_t r1 = listener->prop->rooms[i];
            for (size_t j = 0; j < max_rooms && speaker->prop->rooms[j] != 0xff; j++) {
                if (r1 == speaker->prop->rooms[j]) return 1;
            }
        }
        if (listener->prop->stan && speaker->prop->stan &&
            listener->prop->stan->room != 0xff &&
            listener->prop->stan->room == speaker->prop->stan->room) {
            return 1;
        }
    }
    if (listener->registeredroom >= 0 && listener->registeredroom == speaker->registeredroom) {
        return 1;
    }
    return 0;
}

/* GoldenEye's BG rooms are geometry chunks, not rooms as a player sees them:
 * one hall is several of them and a player's box overlaps only a few, so a
 * shared room index misses most players in plain view. The in-view test is a
 * clear walk over the floor tiles (walls are tile edges with no neighbour)
 * from the listener, whose tile is authoritative, to the speaker. The walk
 * is flat, so it must also end on the speaker's floor: both positions use
 * the same convention, so compare their heights above the floor under them.
 * Runs on the main thread between frames (gevrAudioFrame). */
static int playersPathClear(struct player *listener, struct player *speaker) {
    if (!listener->prop || !speaker->prop || !listener->prop->stan) return 0;
    StandTile *tile = listener->prop->stan;
    coord3d a = listener->prop->pos, b = speaker->prop->pos;
    if (!walkTilesBetweenPoints_NoCallback(&tile, a.x, a.z, b.x, b.z) || !tile) return 0;
    float listener_up = a.y - stanGetPositionYValue(listener->prop->stan, a.x, a.z);
    float speaker_up = b.y - stanGetPositionYValue(tile, b.x, b.z);
    return fabsf(listener_up - speaker_up) < 200.0f;
}

void netVoiceMix(int16_t *stereo, size_t frames) {
    if (!stereo || !voiceSession() || SDL_AtomicGet(&paused) || VrVoiceVolume <= 0.0f) return;
    /* Gains ramp across each buffer from the last one applied, so a speaker
     * stepping behind a wall fades rather than clicks. */
    static float applied_left[GEVR_MAX_PLAYERS], applied_right[GEVR_MAX_PLAYERS];
    static uint32_t log_at;
    int log_now = (int32_t)(SDL_GetTicks() - log_at) >= 0;
    if (log_now) log_at = SDL_GetTicks() + 2000;
    float left[GEVR_MAX_PLAYERS], right[GEVR_MAX_PLAYERS];
    for (unsigned slot = 0; slot < GEVR_MAX_PLAYERS; slot++) {
        left[slot] = right[slot] = 0.0f;
        if ((int)slot == netGetLocalSlot() || !netGetLobbyState()->slots[slot].connected) continue;
        float gain = 1.0f * VrVoiceVolume, pan = 0.0f;
        /* Positions only on a tick that ran the players (players_ticked):
         * while a level loads or ends, everyone is heard at lobby volume. */
        if (netGetState() == NET_STATE_INGAME && players_ticked) {
            int local = netGetLocalSlot();
            int count = getPlayerCount();
            struct player *listener = local >= 0 && local < GEVR_MAX_PLAYERS && local < count ? g_playerPointers[local] : NULL;
            struct player *speaker = (int)slot < count ? g_playerPointers[slot] : NULL;
            if (!listener || !speaker) continue;
            coord3d a = listener->prop ? listener->prop->pos : listener->pos;
            coord3d b = speaker->prop ? speaker->prop->pos : speaker->pos;
            float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
            float distance = sqrtf(dx * dx + dy * dy + dz * dz);

            int same_room = playersShareRoom(listener, speaker);
            int path_clear = !same_room && playersPathClear(listener, speaker);
            if (same_room || path_clear) {
                /* In view: full volume to 500u, easing to a 60% floor at
                 * 4000u and staying there. */
                if (distance > 500.0f) {
                    float t = (distance - 500.0f) / 3500.0f;
                    if (t > 1.0f) t = 1.0f;
                    gain *= (1.0f - 0.4f * t);
                }
            } else {
                /* Out of view: cutoff at 2500u with steep quadratic falloff. */
                if (distance >= 2500.0f) gain = 0.0f;
                else if (distance > 200.0f) {
                    float t = (2500.0f - distance) / 2300.0f;
                    gain *= (t * t);
                }
            }
            if (log_now)
                sysLogPrintf(LOG_NOTE, "voice: slot %u dist %.0f room %d path %d gain %.2f",
                             slot, distance, same_room, path_clear, gain);
            if (gain <= 0.0f) continue;

            if (distance > 1.0f) {
                float yaw = listener->vv_theta * 0.01745329252f;
                /* The listener's right is (-cos theta, -sin theta) in x/z:
                 * radar.c puts a player there on the radar's right. */
                pan = -(dx * cosf(yaw) + dz * sinf(yaw)) / distance * 0.7f;
            }
        }
        left[slot] = gain * (1.0f - pan);
        right[slot] = gain * (1.0f + pan);
    }
    float step_left[GEVR_MAX_PLAYERS], step_right[GEVR_MAX_PLAYERS];
    for (unsigned slot = 0; slot < GEVR_MAX_PLAYERS; slot++) {
        step_left[slot] = frames ? (left[slot] - applied_left[slot]) / frames : 0.0f;
        step_right[slot] = frames ? (right[slot] - applied_right[slot]) / frames : 0.0f;
    }
    for (size_t i = 0; i < frames; i++) {
        float l = stereo[2 * i], r = stereo[2 * i + 1];
        for (unsigned slot = 0; slot < GEVR_MAX_PLAYERS; slot++) {
            /* Every stream keeps flowing, so a player walking back into
             * range is heard live, not from 200 ms ago. */
            float sample = streamSample(&streams[slot]);
            float gain_left = applied_left[slot] + step_left[slot] * (float)(i + 1);
            float gain_right = applied_right[slot] + step_right[slot] * (float)(i + 1);
            if (gain_left == 0.0f && gain_right == 0.0f) continue;
            l += sample * gain_left;
            r += sample * gain_right;
        }
        stereo[2 * i] = clampSample(l);
        stereo[2 * i + 1] = clampSample(r);
    }
    for (unsigned slot = 0; slot < GEVR_MAX_PLAYERS; slot++) {
        applied_left[slot] = left[slot];
        applied_right[slot] = right[slot];
    }
}
