/*
 * A sound is heard the way Perfect Dark hears a
 * sound (dlights.c func0f0056f4 and func0f0053d0, which propsnd.c
 * psCalculateVol measures with): in the listener's own room by straight
 * distance; from another room along the way sound would travel, through
 * the portals - to a portal of the listener's room, the shortest
 * portal-to-portal path, then from a portal of the sound's room to the
 * sound. A shot behind a wall is as far away as the way round it, not as
 * the wall is thick (user, 2026-10-07: other players were at full volume
 * through walls and from other rooms). GoldenEye measured straight lines
 * only.
 *
 * The portal-to-portal path lengths are PD's table (func0f000920): portals
 * that share a room are linked by the distance between their middles. PD
 * builds every pair at load; here a portal's row is found when a sound first
 * needs it (the listener's room's portals), so a stage of a thousand portals
 * loads no slower.
 *
 * Every sound placed in the world (propobj.c sub_GAME_7F053894, the one
 * placement the game's sounds share) is measured this way, and is heard from
 * its direction: its synth voice goes through the binaural effect the
 * players' voices use (net_spatial.c), whatever the voice chat setting
 * (user, 2026-10-07: the proximity chat's 3D audio for the game's sounds, in
 * every mode). The game itself only set a volume: every sound was centred.
 * That needs one listener - solo, co-op or online, not split screen, which
 * keeps the game's loudest-for-the-nearest rule.
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
#include "stan.h"
#include "net_spatial.h"
#include <stddef.h>

extern f32 room_data_float1;
extern s32 sub_GAME_7F0537B8(f32 distance, f32 min, f32 max);
extern bool netIsActive(void);
extern int netGetLocalSlot(void);
extern void sysLogPrintf(s32 level, const char *fmt, ...);
extern void gevrVoiceListenerBasis(float forward[3], float up[3]);

#define SP_MAX_PORTALS 1024
#define SP_FAR 1.0e9f

static f32 *s_path;         /* s_count x s_count: the shortest way from portal to portal, a row once found */
static u8 *s_rowReady;      /* which rows are found */
static u8 *s_done;          /* a row's search: the portals settled */
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

/*
 * The room a sound at pos is in, when its maker's is not known: the room of
 * the floor under it (stan.c), as explosion.c finds one. The last few are
 * remembered: a door or a machine posts its volume every frame.
 */
#define SP_ROOM_CACHE 8
static struct
{
    coord3d at;
    s32 room;
    s32 used;
} s_roomCache[SP_ROOM_CACHE];
static s32 s_roomNext;

static s32 spRoomAt(const coord3d *pos)
{
    coord3d probe;
    StandTile *tile;
    s32 i;

    for (i = 0; i < SP_ROOM_CACHE; i++)
    {
        if (s_roomCache[i].used && s_roomCache[i].at.x == pos->x && s_roomCache[i].at.y == pos->y &&
            s_roomCache[i].at.z == pos->z)
        {
            return s_roomCache[i].room;
        }
    }
    probe = *pos;
    probe.y += 30.0f;
    tile = stanFindTileBelowPos(&probe, NULL, NULL);
    i = s_roomNext;
    s_roomNext = (s_roomNext + 1) % SP_ROOM_CACHE;
    s_roomCache[i].at = *pos;
    s_roomCache[i].room = tile != NULL && tile->room != 0xff ? tile->room : -1;
    s_roomCache[i].used = 1;
    return s_roomCache[i].room;
}

/* The placed sounds heard from their direction: a binaural slot each, after the players' voices */
static struct
{
    ALSoundState *state;
    coord3d pos;
} s_tags[NET_SPATIAL_SFX_SLOTS];
static s32 s_tagCount;

/* For the mixer (mixer.c aEnvMixerImpl): the voice it is mixing, if heard from a direction */
s32 g_gevrSndSpatialSlot = -1;
f32 g_gevrSndSpatialDir[3];
s32 g_gevrSndSpatialPositioned;

static void spFree(void)
{
    memset(s_roomCache, 0, sizeof(s_roomCache));
    memset(s_tags, 0, sizeof(s_tags));
    s_tagCount = 0;
    free(s_path);
    free(s_rowReady);
    free(s_done);
    free(s_middle);
    free(s_rooms);
    s_path = NULL;
    s_rowReady = NULL;
    s_done = NULL;
    s_middle = NULL;
    s_rooms = NULL;
    s_count = 0;
}

s32 gevrSndPathVolume(coord3d *pos, s32 room, f32 low, f32 high);
void gevrSndSpatialPlace(ALSoundState *state, const coord3d *pos);

/* A stage loaded (boss.c): its portals, for the paths between its rooms */
void gevrSndPathStageLoaded(void)
{
    s32 count = 0;
    s32 i;
    s32 j;
    f32 scale;

    spFree();
    if (g_BgPortals == NULL)
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
    s_rowReady = calloc(count, 1);
    s_done = malloc(count);
    s_middle = malloc(sizeof(coord3d) * count);
    s_rooms = malloc(sizeof(*s_rooms) * count);
    if (s_path == NULL || s_rowReady == NULL || s_done == NULL || s_middle == NULL || s_rooms == NULL)
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
    s_count = count;
    sysLogPrintf(1, "sound: %d portals", count);
}

/* Two portals open on one room */
static s32 spShared(s32 i, s32 j)
{
    return s_rooms[i][0] == s_rooms[j][0] || s_rooms[i][0] == s_rooms[j][1] ||
           s_rooms[i][1] == s_rooms[j][0] || s_rooms[i][1] == s_rooms[j][1];
}

/* A portal's shortest ways to every portal (Dijkstra over the shared rooms), found once a stage */
static const f32 *spRow(s32 from)
{
    f32 *row = &s_path[from * s_count];
    s32 i;

    if (s_rowReady[from])
    {
        return row;
    }
    for (i = 0; i < s_count; i++)
    {
        row[i] = SP_FAR;
        s_done[i] = 0;
    }
    row[from] = 0.0f;
    for (;;)
    {
        s32 next = -1;

        for (i = 0; i < s_count; i++)
        {
            if (!s_done[i] && row[i] < SP_FAR && (next < 0 || row[i] < row[next]))
            {
                next = i;
            }
        }
        if (next < 0)
        {
            break;
        }
        s_done[next] = 1;
        for (i = 0; i < s_count; i++)
        {
            if (!s_done[i] && spShared(next, i))
            {
                f32 via = row[next] + spDist(&s_middle[next], &s_middle[i]);

                if (via < row[i])
                {
                    row[i] = via;
                }
            }
        }
    }
    s_rowReady[from] = 1;
    return row;
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
        const f32 *row;
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
        row = spRow(i);
        for (j = 0; j < s_count; j++)
        {
            f32 way;

            if (s_rooms[j][0] != room2 && s_rooms[j][1] != room2)
            {
                continue;
            }
            way = out + row[j] + spDist(&s_middle[j], pos2);
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
    if (state == NULL)
    {
        return;
    }
    sndCreatePostEvent(state, 8, gevrSndPathVolume(pos, source != NULL ? source->rooms[0] : -1, low, high));
    gevrSndSpatialPlace(state, pos);
}

/* The one listener: the local player online, the player in solo; none in split screen */
static PropRecord *spListener(void)
{
    s32 local = netIsActive() ? netGetLocalSlot() : getPlayerCount() == 1 ? 0 : -1;

    if (local < 0 || local >= MAX_PLAYER_COUNT || g_playerPointers[local] == NULL)
    {
        return NULL;
    }
    return g_playerPointers[local]->prop;
}

/* propobj.c: the sounds are measured here, to one listener */
s32 gevrSndHasListener(void)
{
    return spListener() != NULL;
}

/*
 * The volume of a sound at pos (in room, or -1 for the room under it) for the
 * local player, on the game's curve (propobj.c sub_GAME_7F0537B8), measured
 * the way it travels.
 */
s32 gevrSndPathVolume(coord3d *pos, s32 room, f32 low, f32 high)
{
    PropRecord *listener = spListener();
    f32 straight;

    if (listener == NULL)
    {
        return 0;
    }
    straight = spDist(&listener->pos, pos);
    if (straight >= high || s_path == NULL)
    {
        return sub_GAME_7F0537B8(straight, low, high);   /* the way round is never shorter */
    }
    if (room < 0 || room == 0xff)
    {
        room = spRoomAt(pos);
    }
    return sub_GAME_7F0537B8(gevrSndPathDistance(listener->rooms[0], &listener->pos, room, pos), low, high);
}

/* A sound placed at pos is heard from there (or moved there, if placed already) */
void gevrSndSpatialPlace(ALSoundState *state, const coord3d *pos)
{
    s32 i;
    s32 free_tag = -1;

    if (state == NULL || spListener() == NULL)
    {
        return;
    }
    for (i = 0; i < NET_SPATIAL_SFX_SLOTS; i++)
    {
        if (s_tags[i].state == state)
        {
            s_tags[i].pos = *pos;
            return;
        }
        if (s_tags[i].state == NULL && free_tag < 0)
        {
            free_tag = i;
        }
    }
    if (free_tag < 0)
    {
        return;   /* every slot busy: this one stays centred */
    }
    netSpatialInit();
    netSpatialResetSlot(NET_SPATIAL_SFX_FIRST + free_tag);
    s_tags[free_tag].state = state;
    s_tags[free_tag].pos = *pos;
    s_tagCount++;
}

/* A sound state freed (snd.c sndUnlinkClearSound): the next sound to use it is not placed */
void gevrSndSpatialForget(ALSoundState *state)
{
    s32 i;

    if (s_tagCount == 0)
    {
        return;
    }
    for (i = 0; i < NET_SPATIAL_SFX_SLOTS; i++)
    {
        if (s_tags[i].state == state)
        {
            s_tags[i].state = NULL;
            s_tagCount--;
        }
    }
}

/*
 * The synth is about to mix this voice (env.c _pullSubFrame), or none: if it
 * plays a placed sound, the mixer's binaural slot and the direction to it,
 * head-relative (+x right, +y up, -z ahead), as net_voice.c netVoiceMix.
 * The voice is matched by address alone: one not of the sound player (the
 * music's) is never read through.
 */
void gevrSndSpatialVoice(ALVoice *voice)
{
    ALSoundState *state;
    PropRecord *listener;
    float forward[3];
    float up[3];
    float right[3];
    float delta[3];
    float distance;
    s32 i;

    g_gevrSndSpatialSlot = -1;
    if (voice == NULL || s_tagCount == 0)
    {
        return;
    }
    state = (ALSoundState *)((u8 *)voice - offsetof(ALSoundState, voice));
    for (i = 0; i < NET_SPATIAL_SFX_SLOTS && s_tags[i].state != state; i++)
    {
    }
    if (i == NET_SPATIAL_SFX_SLOTS || (listener = spListener()) == NULL)
    {
        return;
    }
    gevrVoiceListenerBasis(forward, up);
    right[0] = forward[1] * up[2] - forward[2] * up[1];
    right[1] = forward[2] * up[0] - forward[0] * up[2];
    right[2] = forward[0] * up[1] - forward[1] * up[0];
    delta[0] = s_tags[i].pos.x - listener->pos.x;
    delta[1] = s_tags[i].pos.y - listener->pos.y;
    delta[2] = s_tags[i].pos.z - listener->pos.z;
    distance = sqrtf(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
    /* centred while a camera, not the head, shows the scene: the intro, the swirl, the ending */
    g_gevrSndSpatialPositioned = distance > 1.0f && isfinite(distance) && g_CameraMode != CAMERAMODE_INTRO &&
                                 g_CameraMode != CAMERAMODE_FADESWIRL && g_CameraMode != CAMERAMODE_SWIRL &&
                                 g_CameraMode != CAMERAMODE_POSEND && g_CameraMode != CAMERAMODE_FADE_TO_TITLE;
    if (g_gevrSndSpatialPositioned)
    {
        float f = delta[0] * forward[0] + delta[1] * forward[1] + delta[2] * forward[2];
        float u = delta[0] * up[0] + delta[1] * up[1] + delta[2] * up[2];
        float r = delta[0] * right[0] + delta[1] * right[1] + delta[2] * right[2];

        g_gevrSndSpatialDir[0] = r / distance;
        g_gevrSndSpatialDir[1] = u / distance;
        g_gevrSndSpatialDir[2] = -f / distance;
    }
    g_gevrSndSpatialSlot = NET_SPATIAL_SFX_FIRST + i;
}
