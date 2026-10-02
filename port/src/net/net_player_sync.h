#ifndef _NET_PLAYER_SYNC_H
#define _NET_PLAYER_SYNC_H

#include <PR/ultratypes.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void netPlayerSyncInit(void);
void netPlayerSyncBeforeTick(s32 playernum);
void netPlayerSyncAfterTick(s32 playernum);

#ifdef __cplusplus
}
#endif

#endif /* _NET_PLAYER_SYNC_H */
