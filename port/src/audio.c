#include <PR/ultratypes.h>
#include <stdio.h>
#include <SDL.h>
#include "platform.h"
#include "config.h"
#include "audio.h"
#include "system.h"

static SDL_AudioDeviceID dev;
static const s16 *nextBuf;
static u32 nextSize = 0;

static s32 bufferSize = 512;
static s32 queueLimit = 8192;
#ifdef GEVR
static s32 sAudioPaused;
#endif

s32 audioInit(void)
{
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		sysLogPrintf(LOG_ERROR, "SDL audio init error: %s", SDL_GetError());
		return -1;
	}

	SDL_AudioSpec want, have;
	SDL_zero(want);
	#ifdef GEVR
    want.freq = 22050; /* matches GE's OUTPUT_RATE */
#else
    want.freq = 22020;
#endif
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = bufferSize;
	want.callback = NULL;

	nextBuf = NULL;

	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (dev == 0) {
		sysLogPrintf(LOG_ERROR, "SDL_OpenAudio error: %s", SDL_GetError());
		return -1;
	}

#ifdef GEVR
	sAudioPaused = 0;
#endif
	SDL_PauseAudioDevice(dev, 0);

	return 0;
}

#ifdef GEVR
void audioPause(void)
{
	if (!dev) {
		return;
	}
	sAudioPaused = 1;
	nextBuf = NULL;
	nextSize = 0;
	SDL_ClearQueuedAudio(dev);
	SDL_PauseAudioDevice(dev, 1);
	sysLogPrintf(LOG_NOTE, "audio: paused (queue cleared)");
}

void audioResume(void)
{
	if (!dev) {
		return;
	}
	sAudioPaused = 0;
	SDL_PauseAudioDevice(dev, 0);
	sysLogPrintf(LOG_NOTE, "audio: resumed");
}

void audioShutdown(void)
{
	if (!dev) {
		return;
	}
	sAudioPaused = 1;
	nextBuf = NULL;
	nextSize = 0;
	SDL_ClearQueuedAudio(dev);
	SDL_PauseAudioDevice(dev, 1);
	SDL_CloseAudioDevice(dev);
	dev = 0;
	sysLogPrintf(LOG_NOTE, "audio: shutdown");
}

s32 audioIsPaused(void)
{
	return sAudioPaused;
}
#endif

s32 audioGetBytesBuffered(void)
{
#ifdef GEVR
	if (!dev) {
		return 0;
	}
#endif
	return SDL_GetQueuedAudioSize(dev);
}

s32 audioGetSamplesBuffered(void)
{
	return audioGetBytesBuffered() / 4;
}

void audioSetNextBuffer(const s16 *buf, u32 len)
{
	nextBuf = buf;
	nextSize = len;
}

void audioEndFrame(void)
{
#ifdef GEVR
	if (sAudioPaused || !dev) {
		nextBuf = NULL;
		nextSize = 0;
		return;
	}
#endif
	if (nextBuf && nextSize) {
#ifdef GEVR
        static u64 lastSubmit, lastReport;
        static u32 empty, dropped, minQueued = ~0u, maxQueued, maxGap;
        u64 now = sysGetMicroseconds();
        u32 queued = audioGetSamplesBuffered();
        u32 gap = lastSubmit ? (u32)(now - lastSubmit) : 0;
        if (lastSubmit && !queued) empty++;
        if (queued >= (u32)queueLimit) dropped++;
        if (queued < minQueued) minQueued = queued;
        if (queued > maxQueued) maxQueued = queued;
        if (gap > maxGap) maxGap = gap;
        lastSubmit = now;
        if (!lastReport) lastReport = now;
        if (now - lastReport >= 2000000) {
            sysLogPrintf(LOG_NOTE, "audio queue: frames=%u..%u empty=%u dropped=%u maxgap=%u us", minQueued, maxQueued, empty, dropped, maxGap);
            lastReport = now;
            empty = dropped = maxQueued = maxGap = 0;
            minQueued = ~0u;
        }
#endif
		if (audioGetSamplesBuffered() < queueLimit) {
			if (SDL_QueueAudio(dev, nextBuf, nextSize) != 0) {
                sysLogPrintf(LOG_ERROR, "SDL_QueueAudio: %s", SDL_GetError());
            }
#ifdef GEVR
            static s32 reportedPcm;
            if (!reportedPcm) {
                for (u32 i = 0; i < nextSize / sizeof(s16); i++) {
                    if (nextBuf[i] != 0) {
                        sysLogPrintf(LOG_NOTE, "audio: nonzero stereo PCM submitted to SDL at 22050 Hz");
                        reportedPcm = 1;
                        break;
                    }
                }
            }
#endif
		}
		nextBuf = NULL;
		nextSize = 0;
	}
}

PD_CONSTRUCTOR static void audioConfigInit(void)
{
	configRegisterInt("Audio.BufferSize", &bufferSize, 0, 1 * 1024 * 1024);
	configRegisterInt("Audio.QueueLimit", &queueLimit, 0, 1 * 1024 * 1024);
}
