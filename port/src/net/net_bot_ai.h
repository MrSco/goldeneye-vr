#ifndef _NET_BOT_AI_H
#define _NET_BOT_AI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void netBotAiInit(void);
void netBotAiReset(void);
void netBotAiTick(void);

#ifdef __cplusplus
}
#endif

#endif /* _NET_BOT_AI_H */
