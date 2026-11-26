/* Pack blood effects on the existing world sprite and decal paths.
 * Particle sweeps run on the simulation tick. Eyes only read their results.
 */
#include <ultra64.h>
#include <bondconstants.h>
#include <bondtypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/bondview.h"
#include "game/bg.h"
#include "game/image.h"
#include "game/lv.h"
#include "game/matrixmath.h"
#include "game/player.h"
#include "game/stan.h"
#include "game/tex.h"
#include "dyn.h"
#include "gevr_bean.h"
#include "gevr_modassets.h"
#include "gevr_modblood.h"
#include "gevr_ragdoll.h"
#include "net_core.h"
#include "pdphysics.h"
#include "pngread.h"
#include "system.h"

enum {
    FX_BLOOD_SPRAY, FX_BLOOD_STREAM, FX_BLOOD_SPLAT, FX_BLOOD_RIBBON,
    FX_BLOOD_MIST, FX_BLOOD_POOL, FX_BLOOD_DROP, FX_ATLASES
};
enum {
    FX_FRAMES = 8, FX_FRAME_SIZE = 128, FX_TILE_SIZE = 16, FX_PARTICLES = 96,
    FX_BLOOD_MARKS = 72, FX_BLOOD_MARK_LIFE = 2400,
    FX_BLOOD_FLIGHTS = 80, FX_BLOOD_FLIGHT_LIFE = 84,
    FX_BLOOD_TRACE_INTERVAL = 3, FX_BLOOD_SWEEPS_PER_TICK = 24,
    FX_FILE_MAX = 1024 * 1024
};

static const char *const s_names[FX_ATLASES] = {
    "blood-spray", "blood-stream", "blood-splat", "blood-ribbon",
    "blood-mist", "blood-pool", "blood-drop"
};
static u16 s_keys[FX_ATLASES][FX_TILE_SIZE * FX_TILE_SIZE] __attribute__((aligned(8)));
/* A campaign is selected at process boot. Keep the texture keys and pixels
 * alive across level changes and late-eye redraws. */
static u8 *s_atlases[FX_ATLASES];
static int s_attempted, s_loaded, s_enabled;
static GevrModBloodConfig s_config;
extern f32 D_800364CC;
extern int VrBloodPatch;

typedef struct BloodParticle {
    coord3d origin, velocity;
    float radius, growth, roll, gravity, spin;
    s32 birth, life;
    u8 atlas, variant, alpha;
} BloodParticle;

static BloodParticle s_particles[FX_PARTICLES];
static unsigned int s_next;
typedef struct BloodMark {
    coord3d center, across, down, normal;
    float radius;
    s32 birth, wetted;
    u8 frame, pool;
} BloodMark;
static BloodMark s_marks[FX_BLOOD_MARKS];
static unsigned int s_nextMark;

typedef struct BloodFlight {
    coord3d origin, velocity, landed;
    float width, fragmentRadius;
    s32 birth, traced, landedAt;
    int room;
    u8 active, variant;
} BloodFlight;
static BloodFlight s_flights[FX_BLOOD_FLIGHTS];
static unsigned s_nextFlight, s_traceFlight;
#define FX_BLOOD_GRAVITY 715.f


static u8 *readAtlas(const char *name)
{
    static const unsigned char signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    static const unsigned char ihdr[] = {0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 4, 0, 0, 0, 0, 128};
    FILE *file = gevrModAssetOpenEffect(name);
    if (!file) return NULL;
    const long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    u8 *bytes = size >= 24 && size <= FX_FILE_MAX && fseek(file, 0, SEEK_SET) == 0
        ? malloc((size_t)size) : NULL;
    const int read = bytes && fread(bytes, 1, (size_t)size, file) == (size_t)size;
    fclose(file);

    u8 *atlas = NULL;
    if (read && !memcmp(bytes, signature, sizeof(signature)) &&
        !memcmp(bytes + sizeof(signature), ihdr, sizeof(ihdr))) {
        s32 width = 0, height = 0;
        atlas = pngReadMem(bytes, (u32)size, name, &width, &height);
        if (width != FX_FRAME_SIZE * FX_FRAMES || height != FX_FRAME_SIZE) {
            free(atlas);
            atlas = NULL;
        }
    }
    free(bytes);
    return atlas;
}

int gevrModGameBlood(const GevrModBloodConfig *config)
{
    if (!config) {
        s_enabled = FALSE;
        memset(&s_config, 0, sizeof(s_config));
        memset(s_particles, 0, sizeof(s_particles));
        memset(s_marks, 0, sizeof(s_marks));
        memset(s_flights, 0, sizeof(s_flights));
        s_next = s_nextMark = s_nextFlight = s_traceFlight = 0;
        return TRUE;
    }
    if (!s_attempted) {
        s_attempted = TRUE;
        for (int i = 0; i < FX_ATLASES; ++i) {
            s_atlases[i] = readAtlas(s_names[i]);
            if (!s_atlases[i]) {
                sysLogPrintf(LOG_WARNING, "mods: missing or invalid blood atlas assets/effects/%s.png", s_names[i]);
                for (int j = 0; j < i; ++j) {
                    free(s_atlases[j]);
                    s_atlases[j] = NULL;
                }
                return FALSE;
            }
        }
        s_loaded = TRUE;
        sysLogPrintf(LOG_NOTE, "mods: loaded %d blood atlases", FX_ATLASES);
    }
    if (!s_loaded) return FALSE;
    s_config = *config;
    s_enabled = TRUE;
    return TRUE;
}

int gevrModBloodEnabled(void)
{
    return s_enabled && VrBloodPatch && !netIsActive();
}

int gevrModBloodHeadChance(int body)
{
    if (!gevrModBloodEnabled() || body < 0 || body >= GEVR_MOD_BLOOD_BODIES) return 0;
    return s_config.bodies[body] ? s_config.headBurst : 0;
}

int gevrModBloodFirearm(int item)
{
    return item >= ITEM_WPPK && item <= ITEM_GOLDWPPK;
}


int gevrModBloodTexture(const void *key, const u8 **rgba, int *width, int *height)
{
    const uintptr_t offset = (uintptr_t)key - (uintptr_t)s_keys;
    if (offset >= sizeof(s_keys) || offset % sizeof(s_keys[0])) return FALSE;
    const u8 *atlas = s_atlases[offset / sizeof(s_keys[0])];
    if (!atlas) return FALSE;
    *rgba = atlas;
    *width = FX_FRAME_SIZE * FX_FRAMES;
    *height = FX_FRAME_SIZE;
    return TRUE;
}

static Gfx *beginSprites(Gfx *g)
{
    gDPPipeSync(g++);
    gSPClearGeometryMode(g++, G_LIGHTING | G_FOG | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
    gDPSetCycleType(g++, G_CYC_1CYCLE);
    gDPSetAlphaCompare(g++, G_AC_NONE);
    gDPSetTextureLUT(g++, G_TT_NONE);
    gDPSetTextureLOD(g++, G_TL_TILE);
    gDPSetTexturePersp(g++, G_TP_PERSP);
    gDPSetTextureFilter(g++, G_TF_BILERP);
    gDPSetRenderMode(g++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
    gDPSetCombineMode(g++, G_CC_MODULATERGBA, G_CC_MODULATERGBA2);
    gSPTexture(g++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON);
    return g;
}

static Gfx *endSprites(Gfx *g)
{
    gDPPipeSync(g++);
    gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetRenderMode(g++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    return g;
}

static Gfx *quadTint(Gfx *g, int atlas, int frame, const coord3d *center, const coord3d *across,
    const coord3d *up, u8 alpha, u32 tint)
{
    Mtxf pose = {.m = {{0,0,0,0}, {0,0,0,0}, {0,0,1,0}, {0,0,0,1}}};
    for (int axis = 0; axis < 3; ++axis) {
        pose.m[0][axis] = across->f[axis] / 64.f;
        pose.m[1][axis] = up->f[axis] / 64.f;
        pose.m[3][axis] = center->f[axis];
    }
    Mtx *matrix = dynAllocateMatrix();
    guMtxF2L(pose.m, matrix);
    Vtx *v = dynAllocateVertices(4);
    const int cell = FX_TILE_SIZE * 32 / FX_FRAMES;
    /* Fast3D adds half a logical texel when filtering triangles. With the
     * small proxy tile that shifts the atlas by a quarter of a frame. */
    const int filterOffset = 16;
    for (int i = 0; i < 4; ++i) {
        v[i] = (Vtx){0};
        v[i].v.ob[0] = i & 1 ? 64 : -64;
        v[i].v.ob[1] = i & 2 ? -64 : 64;
        v[i].v.tc[0] = frame * cell + ((i & 1) ? cell - 2 : 2) - filterOffset;
        v[i].v.tc[1] = ((i & 2) ? FX_TILE_SIZE * 32 - 2 : 2) - filterOffset;
        v[i].v.cn[0] = (u8)(tint >> 16);
        v[i].v.cn[1] = (u8)(tint >> 8);
        v[i].v.cn[2] = (u8)tint;
        v[i].v.cn[3] = alpha;
    }
    gDPLoadTextureBlock(g++, s_keys[atlas], G_IM_FMT_RGBA, G_IM_SIZ_16b, FX_TILE_SIZE, FX_TILE_SIZE,
        0, G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPMatrix(g++, osVirtualToPhysical(matrix), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH);
    gSPVertex(g++, osVirtualToPhysical(v), 4, 0);
    gSP2Triangles(g++, 0, 1, 2, 0, 2, 1, 3, 0);
    gSPPopMatrix(g++, G_MTX_MODELVIEW);
    return g;
}

static Gfx *quad(Gfx *g, int atlas, int frame, const coord3d *center, const coord3d *across,
    const coord3d *up, u8 alpha)
{
    return quadTint(g, atlas, frame, center, across, up, alpha, 0xffffff);
}

static void emitParticle(BloodParticle particle)
{
    particle.birth = g_GlobalTimer;
    s_particles[s_next++ % FX_PARTICLES] = particle;
}

static float vectorLength(const f32 f[3])
{
    return sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
}

static int bloodSurface(coord3d from, coord3d to, int room, HitThing *nearest)
{
    if (room <= 0 || room >= MAXROOMCOUNT) return FALSE;
    s32 candidates[8] = {room};
    s32 count = 0;
    if (g_BgPortals) {
        u8 rooms[16], finalRooms[16];
        memset(rooms, 255, sizeof(rooms));
        rooms[0] = room;
        bgFindRoomsAlongSegment(&from, &to, rooms, finalRooms, candidates + 1, &count, 7);
    }
    const float roomScale = get_room_data_float1();
    const coord3d ray = {.f = {to.x - from.x, to.y - from.y, to.z - from.z}};
    const float range2 = ray.x*ray.x + ray.y*ray.y + ray.z*ray.z;
    if (roomScale <= 0.f || range2 < 1e-4f) return FALSE;
    const float invScale = 1.f / roomScale;
    float best = INFINITY;
    for (int i = 0; i <= count; ++i) {
        const int candidate = candidates[i];
        if (candidate <= 0 || candidate >= MAXROOMCOUNT || (i && candidate == room)) continue;
        HitThing hit;
        if (!bgTestBulletHitBackground(&from, &to, candidate, &hit)) continue;
        float distance = 0.f, along = 0.f;
        for (int axis = 0; axis < 3; ++axis) {
            hit.hitpos.f[axis] *= invScale;
            const float delta = hit.hitpos.f[axis] - from.f[axis];
            distance += delta * delta;
            along += delta * ray.f[axis];
        }
        /* The gun tracer can report hits beyond its requested endpoint. */
        if (along >= 0.f && along <= range2 * 1.002f && distance < best) {
            best = distance;
            *nearest = hit;
        }
    }
    return isfinite(best);
}

static void bloodMark(coord3d center, coord3d normal, const coord3d *direction, float radius)
{
    const float length = vectorLength(normal.f);
    if (!isfinite(length) || length < 1e-6f) return;
    const float facing = normal.x * direction->x + normal.y * direction->y + normal.z * direction->z;
    const float scale = (facing > 0.f ? -1.f : 1.f) / length;
    for (int i = 0; i < 3; ++i) {
        normal.f[i] *= scale;
    }
    const int pool = normal.y > .66f && direction->y < 0.f;
    if (pool) {
        const unsigned count = s_nextMark < FX_BLOOD_MARKS ? s_nextMark : FX_BLOOD_MARKS;
        for (unsigned j = 0; j < count; ++j) {
            BloodMark *mark = &s_marks[(s_nextMark - 1 - j) % FX_BLOOD_MARKS];
            if (!mark->pool || g_GlobalTimer - mark->wetted > FX_BLOOD_MARK_LIFE
                || fabsf(mark->center.y - center.y) > 3.f) continue;
            const float dx = mark->center.x - center.x, dz = mark->center.z - center.z;
            if (dx*dx + dz*dz > 21.f * 21.f) continue;
            mark->radius = fminf(57.f, sqrtf(mark->radius * mark->radius + radius * radius * .68f));
            mark->wetted = g_GlobalTimer;
            if (sysArgCheck("--input-script"))
                sysLogPrintf(LOG_NOTE, "mods: blood floor pool grew to %.1f cm radius", mark->radius);
            return;
        }
    }

    coord3d across = {.f = {normal.z, 0.f, -normal.x}};
    if (fabsf(normal.y) > .93f) across = (coord3d){.f = {normal.y, -normal.x, 0.f}};
    const float axisLength = vectorLength(across.f);
    for (int i = 0; i < 3; ++i) across.f[i] /= axisLength;
    coord3d down = {.f = {normal.y * across.z - normal.z * across.y,
        normal.z * across.x - normal.x * across.z, normal.x * across.y - normal.y * across.x}};
    const float angle = .96f * (s_nextMark % 13);
    const float cosine = cosf(angle), sine = sinf(angle);
    for (int i = 0; i < 3; ++i) {
        const float x = across.f[i], y = down.f[i];
        across.f[i] = x * cosine + y * sine;
        down.f[i] = (y * cosine - x * sine) * (pool ? .77f : .9f);
    }
    const unsigned index = s_nextMark++;
    s_marks[index % FX_BLOOD_MARKS] = (BloodMark){
        .center = center, .across = across, .down = down, .normal = normal, .radius = radius,
        .birth = g_GlobalTimer, .wetted = g_GlobalTimer, .frame = index % FX_FRAMES, .pool = pool
    };
    if (sysArgCheck("--input-script"))
        sysLogPrintf(LOG_NOTE, "mods: blood %s landing at %.1f %.1f %.1f, radius %.1f",
            pool ? "floor pool" : "surface spatter", center.x, center.y, center.z, radius);
}

static BloodFlight *launchBlood(coord3d origin, coord3d velocity, float width, int room)
{
    const unsigned id = s_nextFlight++;
    s_flights[id % FX_BLOOD_FLIGHTS] = (BloodFlight){
        .origin = origin, .velocity = velocity, .width = width, .room = room,
        .birth = g_GlobalTimer, .active = TRUE, .variant = (id * 5) % FX_FRAMES
    };
    return &s_flights[id % FX_BLOOD_FLIGHTS];
}

static coord3d bloodPosition(const BloodFlight *flight, float time)
{
    coord3d p = flight->origin;
    for (int k = 0; k < 3; ++k) p.f[k] += flight->velocity.f[k] * time;
    p.y -= .5f * FX_BLOOD_GRAVITY * time * time;
    return p;
}

static float bloodDistance2(const coord3d *a, const coord3d *b)
{
    const float x = a->x - b->x, y = a->y - b->y, z = a->z - b->z;
    return x*x + y*y + z*z;
}

static int bloodDecalSurface(const HitThing *collision, int room, HitThing *surface)
{
    const float length = vectorLength(collision->normal.f);
    if (!isfinite(length) || length < 1e-6f) return FALSE;
    const float reach = 4.f;
    coord3d from, to, along;
    for (int axis = 0; axis < 3; ++axis) {
        along.f[axis] = -collision->normal.f[axis] / length;
        from.f[axis] = collision->hitpos.f[axis] - reach * along.f[axis];
        to.f[axis] = collision->hitpos.f[axis] + reach * along.f[axis];
    }
    if (!bloodSurface(from, to, room, surface)) return FALSE;
    const float alignment = along.x * surface->normal.x + along.y * surface->normal.y + along.z * surface->normal.z;
    return fabsf(alignment) >= .8f * vectorLength(surface->normal.f);
}

static int bloodWorldContact(const BloodFlight *flight, coord3d from, coord3d to, HitThing *hit, int *canStamp)
{
    int room = flight->room;
    if (room <= 0 || room >= MAXROOMCOUNT) {
        StandTile *tile = stanFindTileBelowPos(&from, NULL, NULL);
        room = tile ? getTileRoom(tile) : -1;
    }
    *canStamp = FALSE;
    hit->texturenum = -1;
    int atPhysicsSurface = FALSE;
    if (pdphysIsReady()) {
        int hitRoom = -1;
        atPhysicsSurface = pdphysRaycastAnyRoom(from.f, to.f, hit->hitpos.f, hit->normal.f, &hitRoom);
        if (atPhysicsSurface && hitRoom > 0) room = hitRoom;
    }
    HitThing visible;
    if ((!pdphysIsReady() || atPhysicsSurface) && bloodSurface(from, to, room, &visible)
        && (!atPhysicsSurface || bloodDistance2(&from, &visible.hitpos) <= bloodDistance2(&from, &hit->hitpos))) {
        *hit = visible;
        *canStamp = TRUE;
        return TRUE;
    }
    if (atPhysicsSurface) {
        if (bloodDecalSurface(hit, room, &visible)) {
            *hit = visible;
            *canStamp = TRUE;
        }
        return TRUE;
    }

    const float drop = from.y - to.y;
    if (drop <= 0.f || room <= 0 || room >= MAXROOMCOUNT) return FALSE;
    coord3d probe = {.f = {to.x, from.y + 1.f, to.z}};
    u8 rooms[2] = {(u8)room, 255};
    f32 floor;
    if (!stanFindTileBelowPos(&probe, rooms, &floor) || floor > from.y || floor < to.y) return FALSE;
    const float t = (from.y - floor) / drop;
    hit->hitpos = (coord3d){.f = {from.x + (to.x - from.x) * t, floor, from.z + (to.z - from.z) * t}};
    hit->normal = (coord3d){.f = {0, 1, 0}};
    if (bloodDecalSurface(hit, room, &visible)) {
        *hit = visible;
        *canStamp = TRUE;
    }
    return TRUE;
}

static void bloodLandingMist(coord3d point, int body)
{
    BloodParticle puff = {.origin = point, .atlas = FX_BLOOD_MIST, .radius = body ? 9.f : 7.f,
        .growth = 9.f, .life = 13, .velocity = {.f = {0, 24.f, 0}},
        .alpha = body ? 156 : 112, .spin = .45f};
    puff.origin.y += 1.5f;
    emitParticle(puff);
}

static int traceBloodFlight(BloodFlight *flight, coord3d from, coord3d to)
{
    HitThing world;
    int canStamp;
    const int hitWorld = bloodWorldContact(flight, from, to, &world, &canStamp);
    const coord3d delta = {.f = {to.x - from.x, to.y - from.y, to.z - from.z}};
    coord3d bodyHit;
    int handle, part;
    if (flight->traced >= 8 && pdphysIsReady()
        && pdphysRaycastRagdoll(from.f, to.f, .35f, bodyHit.f, &handle, &part)
        && (!hitWorld || bloodDistance2(&from, &bodyHit) < bloodDistance2(&from, &world.hitpos))) {
        const float length = vectorLength(delta.f);
        if (length > 1e-4f) {
            const float direction[3] = {delta.x / length, delta.y / length, delta.z / length};
            gevrRagdollBloodStain(handle, part, bodyHit.f, direction);
        }
        bloodLandingMist(bodyHit, TRUE);
        if (sysArgCheck("--input-script"))
            sysLogPrintf(LOG_NOTE, "mods: blood spray struck a ragdoll at %.1f %.1f %.1f", bodyHit.x, bodyHit.y, bodyHit.z);
        return TRUE;
    }
    if (!hitWorld) return FALSE;
    if (flight->fragmentRadius > 0.f) {
        flight->landed = world.hitpos;
        flight->landed.y += flight->fragmentRadius*.65f;
        flight->landedAt = g_GlobalTimer;
    }
    if (gevrBeanIsEnabled()) {
        const float length = vectorLength(delta.f);
        if (length > 1e-4f) {
            const float dir[3] = {delta.x / length, delta.y / length, delta.z / length};
            const float at = dir[0] * (world.hitpos.x - from.x) + dir[1] * (world.hitpos.y - from.y)
                + dir[2] * (world.hitpos.z - from.z);
            if (gevrBeanWorldShotSurface(from.f, dir, at - 4.f, at + 4.f, world.hitpos.f, world.normal.f))
                canStamp = TRUE;
        }
    }
    if (!canStamp) return TRUE;
    if (world.texturenum >= 0 && world.texturenum < gevrTextureCount()
        && g_Textures[world.texturenum].hitTexture == HIT_WATER) return TRUE;

    const float normalLength = vectorLength(world.normal.f);
    const int wall = normalLength > 0.f && fabsf(world.normal.y) < normalLength * .66f;
    const float radius = wall ? 19.f + 9.f * flight->width : 9.f + 7.f * flight->width;
    bloodMark(world.hitpos, world.normal, &delta, radius);
    if ((flight->variant & 1) == 0) bloodLandingMist(world.hitpos, FALSE);
    return TRUE;
}

void gevrModBloodTick(void)
{
    if (!s_nextFlight || !gevrModBloodEnabled()) return;
    /* Sweep a capped number each simulation tick. Both eyes only read results. */
    int sweeps = 0;
    for (int visit = 0; visit < FX_BLOOD_FLIGHTS && sweeps < FX_BLOOD_SWEEPS_PER_TICK; ++visit) {
        BloodFlight *flight = &s_flights[s_traceFlight++ % FX_BLOOD_FLIGHTS];
        if (!flight->active) continue;
        const int age = g_GlobalTimer - flight->birth;
        if (age - flight->traced < FX_BLOOD_TRACE_INTERVAL) continue;
        const int next = age < FX_BLOOD_FLIGHT_LIFE ? age : FX_BLOOD_FLIGHT_LIFE;
        if (traceBloodFlight(flight, bloodPosition(flight, flight->traced / 60.f),
            bloodPosition(flight, next / 60.f)) || age >= FX_BLOOD_FLIGHT_LIFE) flight->active = FALSE;
        flight->traced = next;
        ++sweeps;
    }
}

int gevrModBloodImpact(int room, const f32 point[3], const f32 direction[3])
{
    if (!gevrModBloodEnabled()) return FALSE;
    const float length = vectorLength(direction);
    if (!isfinite(length) || length < 1e-6f) return FALSE;
    for (int i = 0; i < 3; ++i)
        if (!isfinite(point[i])) return FALSE;

    const unsigned variant = (s_next * 7) % FX_FRAMES;
    const float side = variant & 1 ? -1.f : 1.f;
    const coord3d shot = {.f = {direction[0] / length, direction[1] / length, direction[2] / length}};
    const coord3d from = {.f = {point[0] - 4.f * shot.x, point[1] - 4.f * shot.y, point[2] - 4.f * shot.z}};
    BloodParticle cloud = {.origin = from, .atlas = FX_BLOOD_MIST, .life = 26, .radius = 22.f,
        .growth = 33.f, .alpha = 239, .roll = side * (.10f + .055f * variant), .spin = -.68f * side};
    cloud.velocity = (coord3d){.f = {-41.f * shot.x - 24.f * shot.z * side,
        26.f - 31.f * shot.y, -41.f * shot.z + 24.f * shot.x * side}};
    emitParticle(cloud);
    BloodParticle spray = cloud;
    spray.atlas = FX_BLOOD_SPRAY;
    spray.radius = 43.f;
    spray.growth = 18.f;
    spray.life = 18;
    spray.alpha = 245;
    spray.gravity = 37.f;
    spray.velocity.x += -shot.z * 53.f * side;
    spray.velocity.z += shot.x * 53.f * side;
    emitParticle(spray);
    spray.atlas = FX_BLOOD_STREAM;
    spray.radius = 31.f;
    spray.life = 13;
    spray.alpha = 212;
    spray.roll = -.36f * side;
    spray.velocity.x += shot.z * 92.f * side;
    spray.velocity.z -= shot.x * 92.f * side;
    spray.velocity.y -= 18.f;
    emitParticle(spray);

    for (int i = 0; i < 3; ++i) {
        const float lateral = (i - 1) * 67.f + (variant - 3.f) * 8.f;
        const float out = 77.f + i * 36.f;
        const coord3d velocity = {.f = {-shot.x * out - shot.z * lateral, 168.f + i * 17.f,
            -shot.z * out + shot.x * lateral}};
        launchBlood(from, velocity, 1.5f + .35f * i, room);
    }
    /* Give nearby walls behind the impact a separate fan of droplets. */
    const coord3d exit = {.f = {point[0], point[1], point[2]}};
    for (int fan = -1; fan <= 1; fan += 2) {
        const float spread = fan * (40.f + 6.f * variant);
        const coord3d velocity = {.f = {shot.x * 248.f - shot.z * spread,
            108.f + shot.y * 115.f + 17.f * fan, shot.z * 248.f + shot.x * spread}};
        launchBlood(exit, velocity, fan > 0 ? 2.5f : 1.7f, room);
    }
    if (sysArgCheck("--input-script"))
        sysLogPrintf(LOG_NOTE, "mods: blood spray at %.1f %.1f %.1f", point[0], point[1], point[2]);
    return TRUE;
}

void gevrModBloodHeadBurst(int room, const f32 point[3], const f32 direction[3])
{
    if (!gevrModBloodEnabled()) return;
    const float length = vectorLength(direction);
    if (!isfinite(length) || length < 1e-6f) return;
    for (int i = 0; i < 3; ++i)
        if (!isfinite(point[i])) return;
    const coord3d at = {.f={point[0], point[1], point[2]}};
    const float angle = atan2f(direction[2], direction[0]);
    for (int i = 0; i < 5; ++i) {
        const float heading = angle + .78f*(i-2);
        const float speed = 145.f+23.f*i;
        const coord3d velocity = {.f={speed*cosf(heading), 102.f+17.f*i, speed*sinf(heading)}};
        BloodFlight *gib = launchBlood(at, velocity, 1.7f+.17f*i, room);
        gib->fragmentRadius = 1.9f + .41f*((i*3)%5);
    }
    BloodParticle mist = {.origin=at, .atlas=FX_BLOOD_MIST, .life=15, .radius=14.f, .growth=14.f,
        .alpha=190, .velocity={.f={direction[0]*38.f/length,23.f,direction[2]*38.f/length}}, .spin=.6f};
    emitParticle(mist);
    if (sysArgCheck("--input-script"))
        sysLogPrintf(LOG_NOTE, "mods: blood head burst at %.1f %.1f %.1f", point[0], point[1], point[2]);
}

static Gfx *beginWorldSprites(Gfx *g, int *begun)
{
    if (*begun) return g;
    *begun = TRUE;
    gSPMatrix(g++, osVirtualToPhysical(currentPlayerGetProjectionMatrix()), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    return beginSprites(g);
}

static Gfx *drawBloodFragments(Gfx *g, Mtxf *view, float scale)
{
    int begun = FALSE;
    for (int i = 0; i < FX_BLOOD_FLIGHTS; ++i) {
        const BloodFlight *flight = &s_flights[i];
        if (flight->fragmentRadius <= 0.f) continue;
        const int age = g_GlobalTimer - flight->birth;
        const int settled = !flight->active && flight->landedAt > 0 && g_GlobalTimer - flight->landedAt < 130;
        if (age < 0 || (!settled && !flight->active)) continue;
        coord3d centre = settled ? flight->landed : bloodPosition(flight, age/60.f);
        mtx4TransformVecInPlace(view, &centre);
        if (centre.z > -4.f || centre.z < -1500.f) continue;
        if (!begun) {
            begun = TRUE;
            gDPPipeSync(g++);
            gSPMatrix(g++, osVirtualToPhysical(currentPlayerGetProjectionMatrix()),
                G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
            gSPClearGeometryMode(g++, G_LIGHTING | G_FOG | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
            gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
            gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
            gDPSetCycleType(g++, G_CYC_1CYCLE);
            gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
            gDPSetRenderMode(g++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
        }
        const float radius = flight->fragmentRadius*scale*.01f;
        const float theta = .9f*flight->variant + .19f*(settled ? flight->landedAt - flight->birth : age);
        const float c = cosf(theta)*radius, s = sinf(theta)*radius;
        Mtxf pose = {.m={{c,s,0,0},{-s,c,0,0},{0,0,radius,0},
            {centre.x*scale,centre.y*scale,centre.z*scale,1}}};
        Mtx *fixed = dynAllocateMatrix();
        guMtxF2L(pose.m, fixed);
        gSPMatrix(g++, osVirtualToPhysical(fixed), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH);
        static const s16 shape[6][3] = {{-96,12,6},{9,16,71},{78,0,12},{-14,-19,-52},{10,92,8},{-23,-71,-3}};
        Vtx *v = dynAllocateVertices(6);
        for (int k = 0; k < 6; ++k) {
            v[k] = (Vtx){0};
            memcpy(v[k].v.ob, shape[k], sizeof(shape[k]));
            v[k].v.cn[0] = 83 + 12*k;
            v[k].v.cn[1] = k == 4 ? 40 : 14;
            v[k].v.cn[2] = k == 4 ? 31 : 11;
            v[k].v.cn[3] = 255;
        }
        gSPVertex(g++, osVirtualToPhysical(v), 6, 0);
        for (int k = 0; k < 4; ++k) {
            const int next = (k+1)%4;
            gSP2Triangles(g++, k, next, 4, 0, next, k, 5, 0);
        }
        gSPPopMatrix(g++, G_MTX_MODELVIEW);
    }
    return g;
}

static Gfx *drawBloodFlights(Gfx *g, Mtxf *view, float scale, int *begun)
{
    if (!s_nextFlight || !gevrModBloodEnabled()) return g;
    for (int i = 0; i < FX_BLOOD_FLIGHTS; ++i) {
        const BloodFlight *flight = &s_flights[i];
        const int ticks = g_GlobalTimer - flight->birth;
        if (!flight->active || ticks <= 0 || ticks >= FX_BLOOD_FLIGHT_LIFE) continue;
        const float age = ticks / 60.f;
        const int broken = age > flight->velocity.y / FX_BLOOD_GRAVITY + .075f;
        /* Parcels curve over the crest, then split into shorter beads as they fall. */
        const float tail = fmaxf(0.f, age - (broken ? .073f : .157f));
        coord3d previous = bloodPosition(flight, tail);
        mtx4TransformVecInPlace(view, &previous);
        for (int n = 1; n <= 2; ++n) {
            coord3d point = bloodPosition(flight, tail + (age - tail) * (.5f * n));
            mtx4TransformVecInPlace(view, &point);
            coord3d center, along;
            for (int k = 0; k < 3; ++k) {
                center.f[k] = (previous.f[k] + point.f[k]) * .5f * scale;
                along.f[k] = (point.f[k] - previous.f[k]) * .55f * scale;
            }
            previous = point;
            if (point.z > -7.f || point.z < -4000.f) continue;
            const float flatLength = sqrtf(along.x*along.x + along.y*along.y);
            if (flatLength < .06f) continue;
            const float thickness = flight->width * scale * fmaxf(.38f, 1.f - age * .57f);
            const float width = broken ? thickness * .74f : thickness;
            const coord3d up = {.f = {-along.y * width / flatLength, along.x * width / flatLength, 0.f}};
            if (broken) {
                const float streak = fminf(1.f, thickness * (1.f + .25f * n) / flatLength);
                for (int k = 0; k < 3; ++k) along.f[k] *= streak;
            }
            g = beginWorldSprites(g, begun);
            g = quad(g, broken ? FX_BLOOD_DROP : FX_BLOOD_RIBBON, (flight->variant + ticks / 4 + n) % FX_FRAMES,
                &center, &along, &up, (u8)(n == 2 ? 246 : 207));
        }
    }
    return g;
}

Gfx *gevrModBloodDraw(Gfx *g)
{
    if (!gevrModBloodEnabled() || (!s_next && !s_nextMark && !s_nextFlight)) return g;
    const float scale = D_800364CC;
    Mtxf *view = camGetWorldToScreenMtxf();
    g = drawBloodFragments(g, view, scale);
    int begun = FALSE;
    {
        int decals = FALSE;
        const unsigned int count = s_nextMark < FX_BLOOD_MARKS ? s_nextMark : FX_BLOOD_MARKS;
        for (unsigned int i = s_nextMark - count; i < s_nextMark; ++i) {
            const BloodMark *mark = &s_marks[i % FX_BLOOD_MARKS];
            const int age = g_GlobalTimer - mark->birth;
            const int sinceWet = g_GlobalTimer - mark->wetted;
            if (age < 0 || sinceWet >= FX_BLOOD_MARK_LIFE) continue;
            const float size = mark->radius * (mark->pool ? .60f + .40f * fminf(1.f, age / 48.f) : 1.f);
            coord3d center = mark->center, across = mark->across, down = mark->down, normal = mark->normal;
            mtx4TransformVecInPlace(view, &center);
            if (center.z > -10.f || center.z < -4000.f) continue;
            mtx4RotateVecInPlace(view, &normal);
            const float facing = -(normal.x * center.x + normal.y * center.y + normal.z * center.z)
                / vectorLength(center.f);
            if (facing <= .025f) continue;
            /* Near parallel, tiny depth variations can make a whole stain
             * switch between adjacent pixels. Fade through that narrow angle. */
            const float angle = fminf(1.f, (facing - .025f) / .085f);
            const float visibility = angle * angle * (3.f - 2.f * angle);
            mtx4RotateVecInPlace(view, &across);
            mtx4RotateVecInPlace(view, &down);
            for (int j = 0; j < 3; ++j) {
                /* A sub-millimetre offset clears the quantized room plane.
                 * The depth band still clips the sprite to that surface. */
                center.f[j] = (center.f[j] + normal.f[j] * .065f) * scale;
                across.f[j] *= scale * size;
                down.f[j] *= scale * size;
            }
            const int fade = FX_BLOOD_MARK_LIFE - sinceWet;
            const u8 alpha = (u8)(243.f * visibility * fminf(1.f, fade / 210.f));
            if (!alpha) continue;
            const int atlas = mark->pool ? FX_BLOOD_POOL : FX_BLOOD_SPLAT;
            g = beginWorldSprites(g, &begun);
            if (!decals) {
                /* Use the bullet-hole depth band. Its stencil rejects the
                 * part of this quad across an edge or in front of a surface. */
                gDPNoOpTag(g++, 0x56610000); /* VR_IMPACT_DL_BEGIN */
                gDPSetRenderMode(g++, G_RM_AA_ZB_XLU_DECAL, G_RM_AA_ZB_XLU_DECAL2);
                decals = TRUE;
            }
            g = quad(g, atlas, mark->frame, &center, &across, &down, alpha);
        }
        if (decals) {
            gDPNoOpTag(g++, 0x56610001); /* VR_IMPACT_DL_END */
            gDPSetRenderMode(g++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
        }
    }
    for (unsigned int i = 0; i < FX_PARTICLES; ++i) {
        const BloodParticle *p = &s_particles[(s_next + i) % FX_PARTICLES];
        const int ticks = g_GlobalTimer - p->birth;
        if (!p->life || ticks < 0 || ticks >= p->life) continue;
        const int directionalBlood = p->atlas == FX_BLOOD_SPRAY || p->atlas == FX_BLOOD_STREAM;
        const float at = (float)ticks / p->life;
        const float time = ticks / 60.f;
        coord3d center = p->origin;
        for (int axis = 0; axis < 3; ++axis) center.f[axis] += p->velocity.f[axis] * time;
        center.y -= p->gravity * time * time;
        mtx4TransformVecInPlace(view, &center);
        if (center.z > -1.f || center.z < -9000.f) continue;
        for (int axis = 0; axis < 3; ++axis) center.f[axis] *= scale;
        const float size = (p->radius + p->growth * at) * scale;
        float angle = p->roll + p->spin * time;
        if (directionalBlood) {
            coord3d heading = p->velocity;
            heading.y -= p->gravity * time * 2.f;
            mtx4RotateVecInPlace(view, &heading);
            angle += atan2f(heading.y, heading.x);
        }
        const float c = cosf(angle) * size, s = sinf(angle) * size;
        const coord3d x = {.f = {c, s, 0}}, y = {.f = {-s, c, 0}};
        if (directionalBlood) {
            /* The animated fluid starts at 22% of the image width. */
            center.x += .56f * c;
            center.y += .56f * s;
        }
        const int frame = (int)(FX_FRAMES * at);
        g = beginWorldSprites(g, &begun);
        g = quad(g, p->atlas, frame, &center, &x, &y, (u8)(p->alpha * (1.f - .79f * at)));
    }
    g = drawBloodFlights(g, view, scale, &begun);
    return begun ? endSprites(g) : g;
}
