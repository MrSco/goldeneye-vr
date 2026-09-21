#ifndef _AUDI_H_
#define _AUDI_H_

#include <ultra64.h>

void amCreateAudioManager(ALSynConfig* alconf);
void amStartAudioThread(void);

#ifdef GEVR
void gevrAudioMarkReady(void);
void gevrAudioFrame(void);
#endif

#endif
