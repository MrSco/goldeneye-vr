#ifndef _GEVR_BOT_H_
#define _GEVR_BOT_H_
/*
 * Bots online (gevr_bot.c) and their routes over the floor tiles
 * (gevr_botnav.c).
 */
#include <ultra64.h>
#include <bondtypes.h>

#define GEVR_BOT_ROUTE_POINTS 64

/* A route: shared sides and tile middles, ending at the goal */
typedef struct GevrBotRoute {
    coord3d points[GEVR_BOT_ROUTE_POINTS];
    s32 count;              /* 0: none */
    s32 next;               /* the point being walked to */
    StandTile *goaltile;    /* the tile it was planned to */
} GevrBotRoute;

/* Plans a route from the tile under from to the goal on goaltile. FALSE when
 * no floor connects them without crossing a path blocker such as glass
 * (or the search ran out of its budget). Doors may be opened on the way. */
s32 gevrBotNavPlan(GevrBotRoute *route, StandTile *fromtile, const coord3d *from,
                   StandTile *goaltile, const coord3d *goal);

/* Where to head for now along the route, from pos on tile: the farthest of the
 * next points a straight walk reaches without crossing a path blocker.
 * FALSE at the end, with no route, or when a blocker requires replanning. */
s32 gevrBotNavNext(GevrBotRoute *route, const coord3d *pos, StandTile *tile, coord3d *aim);

/* A stage loaded: the floor graph is rebuilt on the next route (another stage's
 * floor can load where the last one was) */
void gevrBotNavForget(void);

/* The floor tile under a point, from a nearby known tile */
StandTile *gevrBotNavTileAt(const coord3d *pos, StandTile *near);

#endif
