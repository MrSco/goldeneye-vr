/*
 * Online, a sound another player makes is heard the way Perfect Dark hears a
 * sound (dlights.c func0f0056f4 and func0f0053d0, which propsnd.c
 * psCalculateVol measures with): in the listener's own room by straight
 * distance; from another room along the way sound would travel, through
 * the portals - to a portal of the listener's room, the shortest
 * portal-to-portal path, then from a portal of the sound's room to the
 * sound. A shot behind a wall is as far away as the way round it, not as
 * the wall is thick (user, 2026-10-07: other players were at full volume
 * through walls and from other rooms). GoldenEye measured straight lines
 * only; its guards keep that.
 *
 * The portal-to-portal path lengths are PD's table (func0f000920), built
 * here once a stage when online: portals that share a room are linked by the
 * distance between their middles, and every pair's shortest path is found
 * across those links.
 */

#include <ultra64.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <bondtypes.h>
#include "bg.h"
#include "bondview.h"
#include "player.h"
#include "snd.h"

extern f32 room_data_float1;
extern s32 sub_GAME_7F0537B8(f32 distance, f32 min, f32 max);
extern bool netIsActive(void);
extern int netGetLocalSlot(void);
extern void sysLogPrintf(s32 level, const char *fmt, ...);

#define SP_MAX_PORTALS 1024
#define SP_FAR 1.0e9f

static f32 *s_path;         /* s_count x s_count: the shortest way from portal to portal */
static coord3d *s_middle;   /* each portal's middle, world space */
static u8 (*s_rooms)[2];    /* the two rooms each portal joins */
static s32 s_count;

static f32 spDist(const coord3d *a, const coord3d *b)
{
    f32 dx = a->x - b->x;
    f32 dy = a->y - b->y;
    f32 dz = a->z - b->z;

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static void spFree(void)
{
    free(s_path);
    free(s_middle);
    free(s_rooms);
    s_path = NULL;
    s_middle = NULL;
    s_rooms = NULL;
    s_count = 0;
}

/* A stage loaded (boss.c): online, the portal paths of its rooms */
void gevrSndPathStageLoaded(void)
{
    s32 count = 0;
    s32 i;
    s32 j;
    s32 k;
    f32 scale;

    spFree();
    if (!netIsActive() || g_BgPortals == NULL)
    {
        return;
    }
    while (count < SP_MAX_PORTALS && g_BgPortals[count].offset_portal != NULL)
    {
        count++;
    }
    if (count == 0)
    {
        return;
    }
    s_path = malloc(sizeof(f32) * count * count);
    s_middle = malloc(sizeof(coord3d) * count);
    s_rooms = malloc(sizeof(*s_rooms) * count);
    if (s_path == NULL || s_middle == NULL || s_rooms == NULL)
    {
        spFree();
        return;
    }
    /* portal points are in the rooms' units: the world's times room_data_float1 (bg.c sub_GAME_7F0B9F14) */
    scale = room_data_float1 != 0.0f ? 1.0f / room_data_float1 : 1.0f;
    for (i = 0; i < count; i++)
    {
        bg_portal_entry *portal = g_BgPortals[i].offset_portal;
        coord3d *points = &portal->point;
        s32 n = portal->numPoints > 0 ? portal->numPoints : 1;

        s_middle[i].x = s_middle[i].y = s_middle[i].z = 0.0f;
        for (j = 0; j < n; j++)
        {
            s_middle[i].x += points[j].x;
            s_middle[i].y += points[j].y;
            s_middle[i].z += points[j].z;
        }
        s_middle[i].x *= scale / n;
        s_middle[i].y *= scale / n;
        s_middle[i].z *= scale / n;
        s_rooms[i][0] = g_BgPortals[i].connectedRoom1;
        s_rooms[i][1] = g_BgPortals[i].connectedRoom2;
    }
    for (i = 0; i < count; i++)
    {
        for (j = 0; j < count; j++)
        {
            s32 shared = i != j && (s_rooms[i][0] == s_rooms[j][0] || s_rooms[i][0] == s_rooms[j][1] ||
                                    s_rooms[i][1] == s_rooms[j][0] || s_rooms[i][1] == s_rooms[j][1]);

            s_path[i * count + j] = i == j ? 0.0f : shared ? spDist(&s_middle[i], &s_middle[j]) : SP_FAR;
        }
    }
    for (k = 0; k < count; k++)
    {
        for (i = 0; i < count; i++)
        {
            f32 ik = s_path[i * count + k];

            if (ik >= SP_FAR)
            {
                continue;
            }
            for (j = 0; j < count; j++)
            {
                f32 via = ik + s_path[k * count + j];

                if (via < s_path[i * count + j])
                {
                    s_path[i * count + j] = via;
                }
            }
        }
    }
    s_count = count;
    sysLogPrintf(1, "sound: %d portals, paths ready", count);
}

/*
 * How far a sound at pos2 in room2 is from a listener at pos1 in room1, the
 * way it travels: straight in one room (or with a room unknown), else
 * through the portals, never nearer than straight.
 */
f32 gevrSndPathDistance(s32 room1, const coord3d *pos1, s32 room2, const coord3d *pos2)
{
    f32 straight = spDist(pos1, pos2);
    f32 best = SP_FAR;
    s32 i;
    s32 j;

    if (s_path == NULL || room1 == room2 || room1 < 0 || room2 < 0 || room1 == 0xff || room2 == 0xff)
    {
        return straight;
    }
    for (i = 0; i < s_count; i++)
    {
        f32 out;

        if (s_rooms[i][0] != room1 && s_rooms[i][1] != room1)
        {
            continue;
        }
        out = spDist(pos1, &s_middle[i]);
        if (out >= best)
        {
            continue;
        }
        for (j = 0; j < s_count; j++)
        {
            f32 way;

            if (s_rooms[j][0] != room2 && s_rooms[j][1] != room2)
            {
                continue;
            }
            way = out + s_path[i * s_count + j] + spDist(&s_middle[j], pos2);
            if (way < best)
            {
                best = way;
            }
        }
    }
    return best < straight ? straight : best;
}

/*
 * A sound another player (prop) made at pos, heard by the local player: its
 * volume by the way it travels, on the game's own curve (propobj.c
 * sub_GAME_7F0537B8: full to 2 m, falling to a third by low, gone by high).
 */
void gevrSndPlaceFromProp(ALSoundState *state, PropRecord *source, coord3d *pos, f32 low, f32 high)
{
    s32 local = netGetLocalSlot();
    PropRecord *listener;
    f32 dist;

    if (state == NULL || local < 0 || local >= MAX_PLAYER_COUNT || g_playerPointers[local] == NULL ||
        (listener = g_playerPointers[local]->prop) == NULL)
    {
        return;
    }
    dist = gevrSndPathDistance(listener->rooms[0], &listener->pos, source != NULL ? source->rooms[0] : -1, pos);
    sndCreatePostEvent(state, 8, sub_GAME_7F0537B8(dist, low, high));
}
