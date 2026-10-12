#include <ultra64.h>
#include <bondgame.h>
#include "chrobjdata.h"

//D:800375D0
#include <assets/obseg/prop/propModelFileHeaders.inc.c>

//D:8003A050
#include <assets/embedded/skeletons/props.inc.c>



//D:8003A228                     .globl PitemZ_entries
/* A hack campaign may replace both tables with its own, longer ones (gevr_romcampaign.c). */
#define PitemZ_entries gevrRetailPropRecords
#include <assets/obseg/prop/propItemModelFileRecord.inc.c>
#undef PitemZ_entries
ItemModelFileRecord *PitemZ_entries = gevrRetailPropRecords;
/* rows before the terminator */
s32 g_GevrPropCount = ARRAYCOUNT(gevrRetailPropRecords) - 1;

#if defined(VERSION_EU)
#include <assets/obseg/prop/propExplosionDetailsRecords-eu.inc.c>
#else
//D:8003b224
#define object_explosion_details gevrRetailExplosions
#include <assets/obseg/prop/propExplosionDetailsRecords.inc.c>
#undef object_explosion_details
ExplosionDetailsRecord *object_explosion_details = gevrRetailExplosions;
#endif
