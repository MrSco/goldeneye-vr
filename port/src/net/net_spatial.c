#include "net_spatial.h"
#include <string.h>
#ifdef GEVR_STEAMAUDIO
#include <phonon.h>
#define BLOCK 256
#define SPATIAL_BLOCK (BLOCK * 2)
static IPLContext context;
static IPLHRTF hrtf;
static IPLBinauralEffect effects[NET_SPATIAL_SLOTS];
static int attempted;
static int last_error;
static struct { float input[SPATIAL_BLOCK], native_output[2][SPATIAL_BLOCK], output[2][BLOCK], previous; unsigned frame; } blocks[NET_SPATIAL_SLOTS];
int netSpatialInit(void) {
    if (attempted) return context && hrtf;
    attempted = 1;
    IPLContextSettings ctx = {0}; ctx.version = STEAMAUDIO_VERSION;
    /* The built-in HRTF ships at 44.1/48 kHz, not 22.05 kHz. The game
     * and Opus rates stay unchanged; only this voice effect runs at 44.1. */
    IPLAudioSettings audio = {44100, SPATIAL_BLOCK};
    IPLHRTFSettings settings = {0}; settings.type = IPL_HRTFTYPE_DEFAULT; settings.volume = 1.0f;
    IPLerror err=iplContextCreate(&ctx, &context);
    if (err != IPL_STATUS_SUCCESS) { last_error=10+err; goto failed; }
    err=iplHRTFCreate(context, &audio, &settings, &hrtf);
    if (err != IPL_STATUS_SUCCESS) { last_error=20+err; goto failed; }
    IPLBinauralEffectSettings effect = {0}; effect.hrtf = hrtf;
    for (int i=0;i<NET_SPATIAL_SLOTS;i++) { err=iplBinauralEffectCreate(context, &audio, &effect, &effects[i]); if (err != IPL_STATUS_SUCCESS) { last_error=30+err; goto failed; } }
    return 1;
failed:
    netSpatialShutdown(); attempted = 1; return 0;
}
int netSpatialLastError(void) { return last_error; }
void netSpatialResetSlot(unsigned slot) {
    if (slot >= NET_SPATIAL_SLOTS) return;
    memset(&blocks[slot], 0, sizeof(blocks[slot]));
    if (effects[slot]) iplBinauralEffectReset(effects[slot]);
}
void netSpatialShutdown(void) {
    for (int i=0;i<NET_SPATIAL_SLOTS;i++) { if (effects[i]) iplBinauralEffectRelease(&effects[i]); netSpatialResetSlot(i); }
    if (hrtf) iplHRTFRelease(&hrtf);
    if (context) iplContextRelease(&context);
    attempted = 0;
}
void netSpatialSample(unsigned slot, float sample, const float direction[3], int positioned, float *left, float *right) {
    if (slot >= NET_SPATIAL_SLOTS || !effects[slot]) {
        float pan = positioned ? direction[0] * 0.7f : 0;
        *left = sample * (1-pan); *right = sample * (1+pan); return;
    }
    unsigned frame = blocks[slot].frame;
    *left = blocks[slot].output[0][frame]; *right = blocks[slot].output[1][frame];
    blocks[slot].input[2*frame] = (blocks[slot].previous + sample) * 0.5f;
    blocks[slot].input[2*frame+1] = sample;
    blocks[slot].previous = sample;
    if (++frame == BLOCK) {
        float *input[] = {blocks[slot].input};
        float *output[] = {blocks[slot].native_output[0], blocks[slot].native_output[1]};
        IPLAudioBuffer in = {1,SPATIAL_BLOCK,input}, out = {2,SPATIAL_BLOCK,output};
        if (positioned) {
            IPLBinauralEffectParams params = {0};
            params.direction = (IPLVector3){direction[0],direction[1],direction[2]};
            params.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
            params.spatialBlend = 1.0f; params.hrtf = hrtf;
            iplBinauralEffectApply(effects[slot], &params, &in, &out);
            for (int i=0;i<BLOCK;i++) {
                blocks[slot].output[0][i]=(output[0][2*i]+output[0][2*i+1])*0.5f;
                blocks[slot].output[1][i]=(output[1][2*i]+output[1][2*i+1])*0.5f;
            }
        } else {
            for (int i=0;i<BLOCK;i++) blocks[slot].output[0][i] = blocks[slot].output[1][i] = input[0][2*i+1];
            iplBinauralEffectReset(effects[slot]);
        }
        frame = 0;
    }
    blocks[slot].frame = frame;
}
#else
int netSpatialInit(void) { return 0; }
int netSpatialLastError(void) { return -1; }
void netSpatialShutdown(void) {}
void netSpatialResetSlot(unsigned slot) { (void)slot; }
void netSpatialSample(unsigned slot, float sample, const float direction[3], int positioned, float *left, float *right) {
    (void)slot; float pan = positioned ? direction[0]*0.7f : 0;
    *left = sample*(1-pan); *right = sample*(1+pan);
}
#endif
