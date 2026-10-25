#ifndef GEVR_ARENA_H
#define GEVR_ARENA_H
#include <bondtypes.h>

/* Called after setup pads have been resolved, before props and path tables load. */
void gevrArenaPrepareSetup(s32 stage);
s32 gevrArenaCampaignDeathmatch(s32 stage);
s32 gevrArenaSkipProp(PropDefHeaderRecord *prop);
/* A door lock the level's own keys open (Dab's Mod key rule). */
s32 gevrArenaDoorKeyOpens(u32 keyflags);
/* Shared, deterministic spawn candidates from guards, navigation and arena pads. */
s32 gevrArenaPads(s32 *out, s32 capacity);
#endif
