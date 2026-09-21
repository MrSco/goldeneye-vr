#ifndef MIXER_H
#define MIXER_H

#include <stdint.h>
#include <ultra64.h>

#undef aSegment
#undef aClearBuffer
#undef aSetBuffer
#undef aLoadBuffer
#undef aSaveBuffer
#undef aDMEMMove
#undef aMix
#undef aEnvMixer
#undef aResample
#undef aInterleave
#undef aSetVolume
#undef aSetVolume32
#undef aSetLoop
#undef aLoadADPCM
#undef aADPCMdec
#undef aPoleFilter

void aClearBufferImpl(uint16_t addr, int nbytes);
void aLoadADPCMImpl(int num_entries_times_16, const int16_t *book_source_addr);
void aSetBufferImpl(uint8_t flags, uint16_t in, uint16_t out, uint16_t nbytes);
void aDMEMMoveImpl(uint16_t in_addr, uint16_t out_addr, int nbytes);
void aSetLoopImpl(ADPCM_STATE *adpcm_loop_state);
void aADPCMdecImpl(uint8_t flags, ADPCM_STATE state);
void aResampleImpl(uint8_t flags, uint16_t pitch, RESAMPLE_STATE state);
void aSetVolumeImpl(uint8_t flags, int16_t v, int16_t t, int16_t r);
void aLoadBufferImpl(const void *source_addr);
#ifdef GEVR
void aLoadBufferRaw16Impl(const void *source_addr);
#endif
void aSaveBufferImpl(int16_t *dest_addr);
void aInterleaveImpl(uint16_t left, uint16_t right);
void aMixImpl(int16_t gain, uint16_t in_addr, uint16_t out_addr);
void aEnvMixerImpl(uint8_t flags, ENVMIX_STATE state);
void aPoleFilterImpl(uint8_t flags, int16_t gain, POLEF_STATE state);

#define aSegment(pkt, s, b) do { } while (0)
#define aClearBuffer(pkt, d, c) aClearBufferImpl(d, c)
#define aLoadADPCM(pkt, c, d) aLoadADPCMImpl(c, (int16_t *)(uintptr_t)(d))
#define aSetBuffer(pkt, f, i, o, c) aSetBufferImpl(f, i, o, c)
#define aDMEMMove(pkt, i, o, c) aDMEMMoveImpl(i, o, c)
#define aSetLoop(pkt, a) aSetLoopImpl((ADPCM_STATE *)(uintptr_t)(a))
#define aADPCMdec(pkt, f, s) aADPCMdecImpl(f, (int16_t *)(uintptr_t)(s))
#define aResample(pkt, f, p, s) aResampleImpl(f, p, (int16_t *)(uintptr_t)(s))
#define aSetVolume(pkt, f, v, t, r) aSetVolumeImpl(f, v, t, r)
#define aSetVolume32(pkt, f, v, tr) aSetVolume(pkt, f, v, (int16_t)((tr) >> 16), (int16_t)(tr))
#define aLoadBuffer(pkt, s) aLoadBufferImpl((const void *)(uintptr_t)(s))
#define aSaveBuffer(pkt, s) aSaveBufferImpl((int16_t *)(uintptr_t)(s))
#define aInterleave(pkt, l, r) aInterleaveImpl(l, r)
#define aMix(pkt, f, g, i, o) aMixImpl((int16_t)(g), i, o)
#define aEnvMixer(pkt, f, s) aEnvMixerImpl(f, (int16_t *)(uintptr_t)(s))
#define aPoleFilter(pkt, f, g, s) aPoleFilterImpl(f, g, (int16_t *)(uintptr_t)(s))

#endif
