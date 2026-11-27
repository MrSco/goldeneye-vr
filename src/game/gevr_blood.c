/*
 * Mods: the Blood patch's blood on the walls and floors (#342).
 *
 * Perfect Dark's splats, as Dab's Mod keeps them (other_projects/dabs-mod
 * src/game/splat.c and wallhit.c), drawn as GoldenEye's own wall hits
 * (explosion.c explosionRenderBulletImpactOnProp), which PD's wallhit.c grew
 * out of:
 *
 * - A hit sprays 0 to 2 splats along the shot, each turned up to 12 degrees
 *   off it, onto the first wall or floor within 180 units behind the body
 *   (splatsCreateForChrHit, splat0f149274). Each grows into place.
 * - A wounded guard drips a small drop now and then, the more bullets it has
 *   taken the sooner, at most 8 in one place (splatTickChr).
 * - A body bleeds a pool under it once it has hit the floor: one splat
 *   straight down every half second, six in all (splatTickChr's puddles),
 *   each growing over about a second and a half.
 *
 * The picture is the splat GoldenEye's Paintball cheat paints (impact type 16,
 * bg/wallhit), tinted with the blood colour the game stains bodies with
 * (gBloodColour). No art ships with it.
 *
 * What differs from PD:
 * - A shot's splats land on objects, doors and guns too (PD's objTestHit
 *   pass, done as a shot tests them), stuck to the object as its bullet holes
 *   are and in their ring, whole at once. Splats on the level have a ring of
 *   96 of their own, so they never push out bullet holes.
 * - A body bleeds whatever killed it, not only after a shot this headset saw:
 *   in co-op another player's kill bleeds on every headset, as its death
 *   plays everywhere. A Ragdolls body bleeds where it came to rest.
 * - All of it is cosmetic and local, so it never draws from the game's
 *   random sequence (co-op, replays and speedruns depend on it), as the
 *   ragdolls' cuts don't (gevr_ragdoll.c rdChance).
 */
#ifdef GEVR
#include <ultra64.h>
#include <math.h>
#include <string.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <bondaicommands.h>
#include "bg.h"
#include "bgroomtrans.h"
#include "bondview.h"
#include "chr.h"
#include "chrai.h"
#include "chrobjdata.h"
#include "matrixmath.h"
#include "model.h"
#include "stan.h"
#include "image_bank.h"
#include "gevr_blood.h"
#include "gevr_bean.h"
#include "gevr_ragdoll.h"
#include "system.h"

extern int VrBloodPatch;   /* vr_settings: Mods, the blood patch */
s32 bgGetConnectedRooms(s32 roomIndex, s32 *list, s32 max);   /* bg.c */
s32 chrpropRayIntersectsRoomBbox(s32 room, coord3d *start, coord3d *dir);   /* chrprop.c */
void sub_GAME_7F04E9BC(PropRecord *prop, struct ShotData *shotdata);   /* propobj.c: a shot's object test */
void gevrExplosionCreateDecalOnProp(PropRecord *prop, s8 mtxindex, s16 impact_type, const f32 centre[3],
                                    const f32 across[3], const f32 up[3], const u8 rgba[4], s8 room_clear_flag);   /* explosion.c */
void gevrExplosionClearBlood(void);   /* explosion.c */

#define BLOOD_DECALS        96
#define BLOOD_CHRS          512        /* gevr_ragdoll.c keeps 512 chrs too */
#define BLOOD_BODIES        GEVR_BLOOD_BODIES
#define BLOOD_POOLS         6          /* splatTickChr's puddles */
#define BLOOD_POOL_TRIES    12         /* a body with no floor below (a crate, a ledge) stops looking */

/* Cosmetic: the dedicated server draws nothing, so it makes none. */
#ifdef GEVR_HEADLESS
#define BLOOD_ON 0
#else
#define BLOOD_ON VrBloodPatch
#endif

enum { SPLAT_HIT, SPLAT_PUDDLE, SPLAT_DROP };

typedef struct BloodDecal {
    Vtx vtx[4];
    float centre[3], across[3], up[3];  /* room units: the full splat's half sides */
    float timer, timermax;
    float fade;                         /* the oldest fade out before the ring reuses them */
    s16 room;
    s16 fine;
    u8 rgb[3], alpha;
} BloodDecal;

typedef struct BloodChr {
    ChrRecord *chr;
    Model *model;   /* Horde reuses a slot's chrnum for the next guard */
    s16 chrnum;
    u8 bulletstaken, deaddrops, droppedhere, wounded;
    u8 deadtries;   /* pool attempts, found a floor or not (BLOOD_POOL_TRIES) */
    u8 died;        /* seen dying: alive again is the slot's next guard */
    s32 tickssincesplat;
    coord3d lastdroppos;
} BloodChr;

typedef struct BloodBody {
    unsigned serial;
    s32 ticks;
    u8 pools, tries;
} BloodBody;

static BloodDecal s_decals[BLOOD_DECALS];
static int s_next, s_live;
static int s_onobjects;   /* splats were put on objects (in the bullet holes' ring) */
static BloodChr s_chrs[BLOOD_CHRS];
static BloodBody s_bodies[BLOOD_BODIES];

/* Cosmetic and local: never the game's own random sequence. */
static unsigned s_random = 0x2545f491u;
static unsigned bloodRandom(void)
{
    s_random = s_random * 1664525u + 1013904223u;
    return s_random >> 8;
}
static float bloodFrac(void) { return (float)(bloodRandom() & 0xffff) / 65536.0f; }

/* ---- geometry (pure; port/tests/blood_splat_native.c) ---- */
/* BEGIN_BLOOD_GEOMETRY */
/* splat.c's tuning, under its names */
#define SPLAT_SIZE_PER_UNIT 0.15f      /* var8007f8a0 */
#define SPLAT_SPREAD_DEG    12.0f      /* var8007f8a8 */
#define SPLAT_MAX_DISTANCE  180.0f     /* g_SplatMaxDistance */
#define SPLAT_SIZE_MIN      5.0f       /* var8007f8b4 */
#define SPLAT_SIZE_MAX      50.0f      /* var8007f8b8 */
#define SPLAT_DROP_HEIGHT   50.0f      /* splatsCreate's extraheight for a chr */
#define SPLAT_TIMER_STEP    1.5f       /* wallhit.c: a 60 Hz frame's share */
#define BLOOD_PI            3.14159265f

/*
 * splat0f14986c: a splat's half sides from how far it flew. One in two is
 * small, one in three big, one in six between; each side then varies by half
 * either way, and the kind's scale (0.8 a hit, 1.1 a pool, 0.3 a drop) goes on
 * last. r1 picks the size, r2/r3 in [0, 1) vary the sides.
 */
static void bloodSplatSize(float distance, float scale, unsigned r1, float r2, float r3, float *w, float *h)
{
    static const float kinds[6] = { 1.5f, 1.5f, 1.5f, 5.0f, 5.0f, 3.0f };
    float size = SPLAT_SIZE_PER_UNIT * distance * kinds[r1 % 6];
    float sw, sh;

    if (size > SPLAT_SIZE_MAX) size = SPLAT_SIZE_MAX;
    if (size < SPLAT_SIZE_MIN) size = SPLAT_SIZE_MIN;
    sw = sh = 0.5f * size;
    if (sw < 1.0f) sw = sh = 1.0f;
    *w = r2 * sw * 2.0f - sw + size;
    *h = r3 * sh * 2.0f - sh + size;
    if (*w > SPLAT_SIZE_MAX) *w = SPLAT_SIZE_MAX;
    if (*h > SPLAT_SIZE_MAX) *h = SPLAT_SIZE_MAX;
    *w *= scale;
    *h *= scale;
}

/*
 * The quad's sides on a surface, as explosionCreateBulletImpact lays a bullet
 * hole (across level, up down the slope; a floor's across is +x, up +z), then
 * turned by deg in the surface's plane as wallhit.c turns blood.
 */
static void bloodSurfaceAxes(const float normal[3], float deg, float across[3], float up[3])
{
    float a[3], u[3], n[3];
    float len = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    float c, s;
    int i;

    for (i = 0; i < 3; i++) n[i] = len > 0.0f ? normal[i] / len : (i == 1);
    if (n[0] == 0.0f && n[2] == 0.0f) {
        a[0] = 1.0f; a[1] = 0.0f; a[2] = 0.0f;
        u[0] = 0.0f; u[1] = 0.0f; u[2] = 1.0f;
    } else {
        const float flat = sqrtf(n[0] * n[0] + n[2] * n[2]);
        const float tx = n[0] / flat, tz = n[2] / flat;
        a[0] = tz;          a[1] = 0.0f;  a[2] = -tx;
        u[0] = n[1] * tx;   u[1] = -flat; u[2] = n[1] * tz;
    }
    c = cosf(deg * BLOOD_PI / 180.0f);
    s = sinf(deg * BLOOD_PI / 180.0f);
    for (i = 0; i < 3; i++) {
        across[i] = a[i] * c + u[i] * s;
        up[i] = u[i] * c - a[i] * s;
    }
}

/* wallhit.c's growth: the size from a fifth to whole on a quarter sine, the
 * alpha whole by half way. f = timer / timermax. */
static float bloodGrowScale(float f) { if (f > 1.0f) f = 1.0f; return 0.2f + 0.8f * sinf(1.5707964f * f); }
static float bloodGrowAlpha(float f) { f *= 2.0f; return f > 1.0f ? 1.0f : f; }
/* END_BLOOD_GEOMETRY */

static void bloodWrite(BloodDecal *d)
{
    const float f = d->timermax > 0.0f ? d->timer / d->timermax : 1.0f;
    const float scale = bloodGrowScale(f);
    const float alpha = (float)d->alpha * bloodGrowAlpha(f) * d->fade;
    static const float sa[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
    static const float su[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
    const s16 tw = impactimages[GEVR_BLOOD_IMPACT_TYPE].width << 5;
    const s16 th = impactimages[GEVR_BLOOD_IMPACT_TYPE].height << 5;
    int j, k;

    for (j = 0; j < 4; j++) {
        Vtx *v = &d->vtx[j];
        for (k = 0; k < 3; k++) {
            const float at = d->centre[k] + scale * (sa[j] * d->across[k] + su[j] * d->up[k]);
            v->v.ob[k] = (s16)lroundf(at * d->fine);
        }
        v->v.flag = d->fine;
        v->v.tc[0] = sa[j] > 0.0f ? tw : 0;
        v->v.tc[1] = (j == 0 || j == 3) ? th : 0;
        v->v.cn[0] = d->rgb[0];
        v->v.cn[1] = d->rgb[1];
        v->v.cn[2] = d->rgb[2];
        v->v.cn[3] = (u8)(alpha > 255.0f ? 255.0f : alpha);
    }
}

/* gBloodColour, a little lighter or darker splat to splat, and wallhit.c's
 * alpha: minalpha 0xc0 (0x40 on glass) to maxalpha 0xff */
static void bloodColour(u8 rgba[4], int glass)
{
    const float shade = 0.85f + 0.3f * bloodFrac();
    const unsigned minalpha = glass ? 0x40 : 0xc0;
    int k;

    for (k = 0; k < 3; k++) {
        const float c = (float)gBloodColour.rgba[k] * shade;
        rgba[k] = (u8)(c > 255.0f ? 255.0f : c);
    }
    rgba[3] = (u8)(minalpha + bloodRandom() % (0x100 - minalpha));
}

/* A splat on the level at pos (world units) in room, half sides w by h. */
static void bloodDecalCreate(const float pos[3], const float normal[3], int room, float w, float h, float timermax)
{
    BloodDecal *d = &s_decals[s_next];
    const float roomscale = get_room_data_float1();
    const coord3d *roompos = getRoomPositionByIndex(room);
    float across[3], up[3], most = 0.0f;
    int i, k, fine = 8;

    if (room <= 0 || room >= MAXROOMCOUNT || roomscale <= 0.0f || !roompos) return;
    bloodSurfaceAxes(normal, (float)(bloodRandom() % 360), across, up);

    for (k = 0; k < 3; k++) {
        const float reach = fabsf(pos[k] * roomscale - roompos->f[k])
                          + (fabsf(across[k] * w) + fabsf(up[k] * h)) * roomscale;
        if (reach > most) most = reach;
    }
    /* too far from the room's origin for a vertex: leave the slot's splat be */
    if (most > 32000.0f) return;
    memset(d, 0, sizeof(*d));
    for (k = 0; k < 3; k++) {
        d->centre[k] = pos[k] * roomscale - roompos->f[k];
        d->across[k] = across[k] * w * roomscale;
        d->up[k] = up[k] * h * roomscale;
    }
    /* the corners in eighths of a room unit, as a bullet hole's (explosion.c, #269) */
    while (fine > 1 && most * fine > 32000.0f) fine >>= 1;
    d->fine = (s16)fine;
    d->room = (s16)room;
    d->timermax = timermax;
    d->fade = 1.0f;
    {
        u8 rgba[4];
        bloodColour(rgba, 0);
        memcpy(d->rgb, rgba, 3);
        d->alpha = rgba[3];
    }
    bloodWrite(d);

    s_next = (s_next + 1) % BLOOD_DECALS;
    if (s_live < BLOOD_DECALS) s_live++;
    /* the next ten to be reused fade out first, as bullet holes do
     * (explosionSetBulletImpactAlpha) */
    if (s_live == BLOOD_DECALS) {
        for (i = 0; i < 10; i++) {
            BloodDecal *old = &s_decals[(s_next + i) % BLOOD_DECALS];
            const float fade = (float)i / 10.0f;
            if (old->room > 0 && fade < old->fade) {
                old->fade = fade;
                bloodWrite(old);
            }
        }
    }
}

/*
 * The first of the level's surfaces from from to to (world units). The rooms
 * looked in are a shot's (chrprop.c): the one it starts in, the ones the
 * segment crosses (as the mod API's blood finds them, gevr_modblood.c), the
 * ones next to it (chrpropFindFirstBgHitInConnectedRooms) and the ones in
 * view (chrpropFindCloserBgHitInVisibleRooms) - Dam's building fronts are
 * rooms of their own that no portal leads to - each past a box test first.
 * With Project Bean on, the release's surface near the hit, as a bullet hole
 * moves to it (chrprop.c).
 */
static int bloodTrace(const coord3d *from, const coord3d *to, int room, float hit[3], float normal[3], int *hitroom)
{
    s32 candidates[160];
    s32 count = 1;
    u8 listed[256];
    const float roomscale = get_room_data_float1();
    coord3d ray = { .f = { to->x - from->x, to->y - from->y, to->z - from->z } };
    const float range2 = ray.x * ray.x + ray.y * ray.y + ray.z * ray.z;
    coord3d scaledfrom;
    float best = 1e30f;
    int i, k, found = 0;

    if (room <= 0 || room >= MAXROOMCOUNT || roomscale <= 0.0f || range2 < 1e-4f) return 0;
    memset(listed, 0, sizeof(listed));
    candidates[0] = room;
    listed[room] = 1;
    {
        s32 more[100];
        s32 nmore = 0, j;

        if (g_BgPortals) {
            u8 rooms[16], finalrooms[16];
            coord3d a = *from, b = *to;
            s32 along[8];
            s32 nalong = 0;
            memset(rooms, 255, sizeof(rooms));
            rooms[0] = (u8)room;
            bgFindRoomsAlongSegment(&a, &b, rooms, finalrooms, along, &nalong, 7);
            for (j = 0; j < nalong; j++) more[nmore++] = along[j];
            nmore += bgGetConnectedRooms(room, more + nmore, 40);
        }
        for (j = 0; j < nmore && count < 160; j++) {
            if (more[j] > 0 && more[j] < MAXROOMCOUNT && !listed[more[j]]) {
                listed[more[j]] = 1;
                candidates[count++] = more[j];
            }
        }
        nmore = bgCopyVisibleRoomsToList(more, 100);
        for (j = 0; j < nmore && count < 160; j++) {
            if (more[j] > 0 && more[j] < MAXROOMCOUNT && !listed[more[j]]) {
                listed[more[j]] = 1;
                candidates[count++] = more[j];
            }
        }
    }
    for (k = 0; k < 3; k++) scaledfrom.f[k] = from->f[k] * roomscale;
    for (i = 0; i < count; i++) {
        const int candidate = candidates[i];
        struct HitThing h;
        coord3d a = *from, b = *to;
        float distance = 0.0f, along = 0.0f;

        if (i && !chrpropRayIntersectsRoomBbox(candidate, &scaledfrom, &ray)) continue;
        if (!bgTestBulletHitBackground(&a, &b, candidate, &h)) continue;
        for (k = 0; k < 3; k++) {
            const float delta = h.hitpos.f[k] / roomscale - from->f[k];
            distance += delta * delta;
            along += delta * ray.f[k];
        }
        /* the tracer can report a hit past the end it was given */
        if (along >= 0.0f && along <= range2 * 1.002f && distance < best) {
            best = distance;
            for (k = 0; k < 3; k++) {
                hit[k] = h.hitpos.f[k] / roomscale;
                normal[k] = h.normal.f[k];
            }
            *hitroom = candidate;
            found = 1;
        }
    }
    if (found && gevrBeanIsEnabled()) {
        const float len = sqrtf(range2);
        const float dir[3] = { ray.x / len, ray.y / len, ray.z / len };
        const float at = sqrtf(best);
        float bh[3], bn[3];
        if (gevrBeanWorldShotSurface(from->f, dir, at > GEVR_BEAN_HOLE_REACH ? at - GEVR_BEAN_HOLE_REACH : 0.0f,
                                     at + GEVR_BEAN_HOLE_REACH, bh, bn)) {
            memcpy(hit, bh, sizeof(bh));
            memcpy(normal, bn, sizeof(bn));
        }
    }
    return found;
}

/* An object's hit, with its triangle's corners copied out: Bean's props
 * name one static triangle (gevr_bean.c gevrBeanHitNode), which the next
 * prop's test overwrites. */
typedef struct BloodObjectHit {
    BulletHit hit;
    float tri[3][3];
    int hastri;
} BloodObjectHit;

/*
 * PD's objTestHit pass (splat0f149274): the nearest object, door or gun on
 * screen along the spray within reach (world units), found as a shot finds
 * them (chrprop.c: sub_GAME_7F04E9BC on each on-screen prop, in view space),
 * one prop at a time. The hit is in the hit matrix's own space
 * (BulletHit.room is its index); *distance is how far it is from from, in
 * world units.
 */
static int bloodObjectHit(const coord3d *from, const float dir[3], float reach, BloodObjectHit *out, float *distance)
{
    Mtxf *toview = camGetWorldToScreenMtxf();
    Mtxf *toworld = currentPlayerGetViewToWorldMtxf();
    struct ShotData shot;
    PropRecord **pp;
    float len, best = reach;
    int i, k, found = 0;

    if (!toview || !toworld || !g_LastOnScreenProp) return 0;
    memset(&shot, 0, sizeof(shot));
    shot.viewOrigin = *from;
    mtx4TransformVecInPlace(toview, &shot.viewOrigin);
    for (k = 0; k < 3; k++) shot.viewDir.f[k] = dir[k];
    mtx4RotateVecInPlace(toview, &shot.viewDir);
    len = sqrtf(shot.viewDir.x * shot.viewDir.x + shot.viewDir.y * shot.viewDir.y + shot.viewDir.z * shot.viewDir.z);
    if (len <= 0.0f) return 0;
    for (k = 0; k < 3; k++) {
        shot.viewDir.f[k] /= len;
        shot.gunpos.f[k] = from->f[k];
        shot.dir.f[k] = dir[k];
    }
    shot.weapon = ITEM_WPPK;
    /* the depth test is from the eye, not from the spray's start */
    if (-shot.viewOrigin.z + reach <= 0.0f) return 0;

    for (pp = g_LastOnScreenProp; (--pp) >= g_OnScreenPropList;) {
        PropRecord *prop = *pp;
        int hits = 0;

        if (!prop || (prop->type != PROP_TYPE_OBJ && prop->type != PROP_TYPE_WEAPON && prop->type != PROP_TYPE_DOOR)) continue;
        memset(shot.hits, 0, sizeof(shot.hits));
        shot.maxdist = -shot.viewOrigin.z + reach;
        sub_GAME_7F04E9BC(prop, &shot);
        for (i = 0; i < (int)(sizeof(shot.hits) / sizeof(shot.hits[0])); i++) hits += shot.hits[i].prop != NULL;
        for (i = 0; i < (int)(sizeof(shot.hits) / sizeof(shot.hits[0])); i++) {
            BulletHit *h = &shot.hits[i];
            Model *model;
            coord3d at;
            float d = 0.0f, along = 0.0f;

            if (!h->prop || !h->prop->obj || !(model = h->prop->obj->model) || h->room < 0) continue;
            at = h->hit.hitpos;
            mtx4TransformVecInPlace(&model->render_pos[h->room].pos, &at);
            mtx4TransformVecInPlace(toworld, &at);
            for (k = 0; k < 3; k++) {
                const float delta = at.f[k] - from->f[k];
                d += delta * delta;
                along += delta * dir[k];
            }
            d = sqrtf(d);
            if (along > 0.0f && d < best) {
                best = d;
                out->hit = *h;
                /* the corners are this prop's only while it alone has been
                 * tested and gave the one hit (a child prop tests after it) */
                out->hastri = hits == 1 && h->hit.vtx0 && h->hit.vtx1 && h->hit.vtx2;
                if (out->hastri) {
                    const Vertex *v[3] = { h->hit.vtx0, h->hit.vtx1, h->hit.vtx2 };
                    for (k = 0; k < 3; k++) {
                        out->tri[k][0] = v[k]->coord.x;
                        out->tri[k][1] = v[k]->coord.y;
                        out->tri[k][2] = v[k]->coord.z;
                    }
                }
                found = 1;
            }
        }
    }
    if (found) *distance = best;
    return found;
}

/* The splat on an object, as its bullet hole is made (propobj.c objHit):
 * in the hit matrix's units, the sides divided by the matrix's scale. It is
 * whole at once: the ring is the bullet holes'. */
static void bloodDecalOnObject(const BloodObjectHit *found, float w, float h)
{
    const BulletHit *hit = &found->hit;
    ObjectRecord *obj = hit->prop->obj;
    ModelFileHeader *file = obj->model->obj;
    s8 clearflag;
    Mtxf *mtx = &obj->model->render_pos[hit->room].pos;
    float across[3], up[3], centre[3];
    coord3d sa, su;
    float la, lu;
    u8 rgba[4];
    int k;

    bloodSurfaceAxes(hit->hit.normal.f, (float)(bloodRandom() % 360), across, up);
    for (k = 0; k < 3; k++) {
        sa.f[k] = across[k];
        su.f[k] = up[k];
        centre[k] = hit->hit.hitpos.f[k];
    }
    mtx4RotateVecInPlace(mtx, &sa);
    mtx4RotateVecInPlace(mtx, &su);
    la = sqrtf(sa.x * sa.x + sa.y * sa.y + sa.z * sa.z);
    lu = sqrtf(su.x * su.x + su.y * su.y + su.z * su.z);
    if (la <= 0.0f || lu <= 0.0f) return;
    for (k = 0; k < 3; k++) {
        across[k] *= w / la;
        up[k] *= h / lu;
    }
    /* Kept on the face it hit: an object's faces are small (a door's
     * panels, a crate's sides), and the decal band rightly hides what hangs
     * past an edge, which cut big splats in two. Shrunk to the hit
     * triangle's box, as a smaller splat (Dab's Mod trims them instead). */
    if (found->hastri) {
        float fit = 1.0f;
        for (k = 0; k < 3; k++) {
            const float lo = fminf(found->tri[0][k], fminf(found->tri[1][k], found->tri[2][k]));
            const float hi = fmaxf(found->tri[0][k], fmaxf(found->tri[1][k], found->tri[2][k]));
            const float reach = fabsf(across[k]) + fabsf(up[k]);
            const float room = fminf(centre[k] - lo, hi - centre[k]);
            if (hi - lo < 1.0f || reach <= 0.0f) continue;   /* the face's own depth */
            if (room < reach) fit = fminf(fit, fmaxf(room, 0.0f) / reach);
        }
        if (fit < 0.2f) return;   /* at the very edge: no room for one */
        for (k = 0; k < 3; k++) {
            across[k] *= fit;
            up[k] *= fit;
        }
    }
    /* Cleared with a door's window or a camera's glass when it breaks, as
     * objHit's holes there are (explosionClearBulletImpactRoomByFlag 1). */
    clearflag = (file->Skeleton == &skeleton_door && hit->unk44 == file->Switches[3])
             || (file->Skeleton == &skeleton_cctv && hit->unk44 == file->Switches[1]);
    bloodColour(rgba, obj->type == PROPDEF_GLASS || obj->type == PROPDEF_TINTED_GLASS);
    gevrExplosionCreateDecalOnProp(hit->prop, (s8)hit->room, GEVR_BLOOD_IMPACT_TYPE, centre, across, up, rgba, clearflag);
    s_onobjects = 1;
}

/* dir turned by up to SPLAT_SPREAD_DEG about each axis (splatsCreate) */
static void bloodSpread(const float dir[3], float out[3])
{
    float a[3], v[3], t;
    int i;

    for (i = 0; i < 3; i++) a[i] = (bloodFrac() * 2.0f - 1.0f) * SPLAT_SPREAD_DEG * BLOOD_PI / 180.0f;
    memcpy(v, dir, sizeof(v));
    t = v[1] * cosf(a[0]) - v[2] * sinf(a[0]); v[2] = v[1] * sinf(a[0]) + v[2] * cosf(a[0]); v[1] = t;
    t = v[0] * cosf(a[1]) + v[2] * sinf(a[1]); v[2] = -v[0] * sinf(a[1]) + v[2] * cosf(a[1]); v[0] = t;
    t = v[0] * cosf(a[2]) - v[1] * sinf(a[2]); v[1] = v[0] * sinf(a[2]) + v[1] * cosf(a[2]); v[0] = t;
    t = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (i = 0; i < 3; i++) out[i] = t > 0.0f ? v[i] / t : dir[i];
}

/* splat0f149274 + splat0f14986c: one splat from from along dir. */
static int bloodSplat(const coord3d *from, const float dir[3], int room, int type)
{
    static const float scales[3] = { 0.8f, 1.1f, 0.3f };
    static const float timers[3] = { 50.0f, 150.0f, 80.0f };
    float spread[3], hit[3], normal[3], w, h, distance = 0.0f;
    coord3d to;
    int k, hitroom = 0;

    BloodObjectHit object;
    float objectdistance = SPLAT_MAX_DISTANCE;
    int onlevel;

    bloodSpread(dir, spread);
    for (k = 0; k < 3; k++) to.f[k] = from->f[k] + spread[k] * SPLAT_MAX_DISTANCE;
    onlevel = bloodTrace(from, &to, room, hit, normal, &hitroom);
    if (onlevel) {
        for (k = 0; k < 3; k++) distance += (hit[k] - from->f[k]) * (hit[k] - from->f[k]);
        distance = sqrtf(distance);
        onlevel = distance < SPLAT_MAX_DISTANCE;
    }
    /* only a shot's spray tests objects, as in PD (splat0f149274) */
    if (type == SPLAT_HIT && bloodObjectHit(from, spread, onlevel ? distance : SPLAT_MAX_DISTANCE, &object, &objectdistance)) {
        distance = objectdistance;
        onlevel = 0;
    } else if (!onlevel) {
        return 0;
    }
    {
        const unsigned r1 = bloodRandom();
        const float r2 = bloodFrac();
        const float r3 = bloodFrac();
        bloodSplatSize(distance, scales[type], r1, r2, r3, &w, &h);
    }
    if (onlevel) bloodDecalCreate(hit, normal, hitroom, w, h, timers[type]);
    else bloodDecalOnObject(&object, w, h);
    return 1;
}

static int bloodChrRoom(ChrRecord *chr)
{
    return chr->prop && chr->prop->stan ? getTileRoom(chr->prop->stan) : 0;
}

static BloodChr *bloodChrState(ChrRecord *chr, int create)
{
    const int slot = (int)(chr - g_ChrSlots);
    BloodChr *s;

    if (!g_ChrSlots || slot < 0 || slot >= g_NumChrSlots || slot >= BLOOD_CHRS) return NULL;
    s = &s_chrs[slot];
    if (s->chr != chr || s->chrnum != chr->chrnum || s->model != chr->model) {
        if (!create) return NULL;
        memset(s, 0, sizeof(*s));
        s->chr = chr;
        s->chrnum = chr->chrnum;
        s->model = chr->model;
    }
    return s;
}

static int bloodChrBleeds(ChrRecord *chr)
{
    return chr && chr->model && chr->prop
        && !(chr->chrflags & (CHRSTART_FORCENOBLOOD | CHRFLAG_HIDDEN));
}

/* splatsCreate for a puddle or a drop: straight down from above the feet */
static int bloodDrip(ChrRecord *chr, int type)
{
    static const float down[3] = { 0.0f, -1.0f, 0.0f };
    coord3d from = chr->prop->pos;

    from.y = chr->ground + SPLAT_DROP_HEIGHT;
    return bloodSplat(&from, down, bloodChrRoom(chr), type);
}

void gevrBloodChrHit(ChrRecord *chr, const coord3d *hitpos, const coord3d *shotdir)
{
    BloodChr *s;
    float dir[3], len;
    int i, qty, made = 0;

    if (!BLOOD_ON || !bloodChrBleeds(chr) || !hitpos || !shotdir) return;
    if (chr->prop->type != PROP_TYPE_CHR && chr->prop->type != PROP_TYPE_VIEWER) return;
    len = sqrtf(shotdir->x * shotdir->x + shotdir->y * shotdir->y + shotdir->z * shotdir->z);
    if (len <= 0.0f) return;
    for (i = 0; i < 3; i++) dir[i] = shotdir->f[i] / len;

    s = bloodChrState(chr, 1);
    if (s && s->bulletstaken < 7) s->bulletstaken++;
    qty = bloodRandom() % 3;
    for (i = 0; i < qty; i++) made += bloodSplat(hitpos, dir, bloodChrRoom(chr), SPLAT_HIT);
    if (made && s) {
        s->tickssincesplat = 0;
        s->lastdroppos = chr->prop->pos;
    }
}

/*
 * A blade's or a gun butt's blow (chrprop.c chraiFistAttackHandler, the
 * headset's gevrChopHit): neither GoldenEye nor PD bleeds for a melee hit, so
 * the Blood patch adds what a shot leaves. The stain goes on the struck
 * part's box, on its face toward the blow, as the "wound" cheat lays one
 * (bondview2.c gevrWoundGuards); the splats spray on from the part's middle
 * along the blow, as a shot's do. The chr is on screen (both look through the
 * on-screen list), so its matrices are this frame's. Fists stay clean.
 */
void gevrBloodMeleeHit(ChrRecord *chr, s32 hitpart, const coord3d *dir)
{
    extern ModelNode *gevrWoundChest(ModelNode *node, s32 part);   /* bondview2.c */
    ModelNode *root, *node;
    Mtxf *mtx, inv;
    coord3d vdir, local, world;
    const struct bbox *box;
    float lo[3], hi[3], d[3];
    int k, axis = 2;
    s32 part;

    if (!BLOOD_ON || !bloodChrBleeds(chr) || !dir || !chr->model->obj || !chr->model->render_pos) return;
    if (chr->prop->type != PROP_TYPE_CHR && chr->prop->type != PROP_TYPE_VIEWER) return;
    if (!(chr->prop->flags & PROPFLAG_ONSCREEN)) return;
    /* the crouched blows' general parts have no box of their own */
    part = hitpart == HIT_GENERALHALF ? HIT_PELVIS : hitpart == HIT_GENERAL ? HIT_CHEST : hitpart;
    root = chr->model->obj->RootNode;
    node = gevrWoundChest(root, part);
    if (!node) node = gevrWoundChest(root, part = HIT_CHEST);
    if (!node || !(mtx = modelFindNodeMtx(chr->model, node, 0))) return;

    /* the blow in the part's own axes: it lands on the face it comes from */
    vdir = *dir;
    mtx4RotateVecInPlace(camGetWorldToScreenMtxf(), &vdir);
    matrix_4x4_set_inverse_rotation_and_translation(mtx, &inv);
    mtx4RotateVecInPlace(&inv, &vdir);
    box = &node->Data->BoundingBox.Bounds;
    lo[0] = box->xmin; lo[1] = box->ymin; lo[2] = box->zmin;
    hi[0] = box->xmax; hi[1] = box->ymax; hi[2] = box->zmax;
    for (k = 0; k < 3; k++) d[k] = vdir.f[k];
    if (fabsf(d[0]) > fabsf(d[axis])) axis = 0;
    for (k = 0; k < 3; k++) local.f[k] = (lo[k] + hi[k]) * 0.5f;
    local.f[axis] = d[axis] < 0.0f ? hi[axis] : lo[axis];
    chrCreateBloodStain(chr->model, part, node, &local);

    for (k = 0; k < 3; k++) world.f[k] = mtx->m[3][k];
    mtx4TransformVecInPlace(currentPlayerGetViewToWorldMtxf(), &world);
    gevrBloodChrHit(chr, &world, dir);
}

static int bloodChrDownEnough(ChrRecord *chr)
{
    if (chr->actiontype == ACT_DEAD) return 1;
    /* chraction.c clears a thud frame once the body has hit the floor */
    return chr->actiontype == ACT_DIE && chr->act_die.thudframe1 < 0.0f && chr->act_die.thudframe2 < 0.0f;
}

/* splatTickChr */
static void bloodChrTick(ChrRecord *chr, int ticks)
{
    const int dying = chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD;
    BloodChr *s = bloodChrState(chr, dying);

    if (!s) return;
    if (!dying && s->died) {
        /* dead before, up now: Horde's next guard in the slot, maybe on the
         * same chrnum and Model, starts unwounded */
        memset(s, 0, sizeof(*s));
        s->chr = chr;
        s->chrnum = chr->chrnum;
        s->model = chr->model;
    }
    if (dying) {
        s->died = 1;
        if (gevrRagdollSuppress(chr)) return;   /* the ragdoll body bleeds instead */
        if (bloodChrDownEnough(chr) && s->tickssincesplat > 30 && s->deaddrops < BLOOD_POOLS
            && s->deadtries < BLOOD_POOL_TRIES) {
            s->deadtries++;
            if (bloodDrip(chr, SPLAT_PUDDLE)) s->deaddrops++;
            s->tickssincesplat = 0;   /* no floor below: try again in half a second */
        }
    } else if (s->bulletstaken) {
        if ((u32)s->bulletstaken * (u32)s->tickssincesplat > 240 && s->wounded < 40) {
            const float dx = chr->prop->pos.x - s->lastdroppos.x;
            const float dy = chr->prop->pos.y - s->lastdroppos.y;
            const float dz = chr->prop->pos.z - s->lastdroppos.z;
            int add = 0;

            if (dx * dx + dy * dy + dz * dz > 40.0f * 40.0f) {
                add = 1;
                s->droppedhere = 0;
            } else if (s->droppedhere < 8) {
                add = 1;
                s->droppedhere++;
            }
            if (add) {
                if (bloodDrip(chr, SPLAT_DROP)) s->wounded++;
                s->tickssincesplat = 0;
                s->lastdroppos = chr->prop->pos;
            }
        }
        s->deaddrops = 0;
    }
    s->tickssincesplat += ticks;
}

/* A Ragdolls body: its pool once it has come to rest, under its root. */
static void bloodBodyTick(int body, int ticks)
{
    static const float down[3] = { 0.0f, -1.0f, 0.0f };
    BloodBody *s = &s_bodies[body];
    coord3d root;
    int resting = 0;
    const unsigned serial = gevrRagdollBodyRest(body, &root, &resting);

    if (serial != s->serial) {
        memset(s, 0, sizeof(*s));
        s->serial = serial;
    }
    if (!serial || !resting || s->pools >= BLOOD_POOLS || s->tries >= BLOOD_POOL_TRIES) return;
    s->ticks += ticks;
    if (s->ticks > 30) {
        StandTile *tile;
        coord3d from = root;
        from.y += SPLAT_DROP_HEIGHT;
        s->tries++;
        tile = stanFindTileBelowPos(&from, NULL, NULL);
        if (tile && bloodSplat(&from, down, getTileRoom(tile), SPLAT_PUDDLE)) s->pools++;
        s->ticks = 0;
    }
}

void gevrBloodTick(int ticks)
{
    int i;

    if (!BLOOD_ON) {
        if (s_live || s_onobjects) {
            gevrBloodReset();
            gevrExplosionClearBlood();
            s_onobjects = 0;
        }
        return;
    }
    if (ticks <= 0) return;

    for (i = 0; i < BLOOD_DECALS; i++) {
        BloodDecal *d = &s_decals[i];
        if (d->room > 0 && d->timermax > 0.0f) {
            d->timer += SPLAT_TIMER_STEP * ticks;
            if (d->timer >= d->timermax) d->timermax = 0.0f;   /* grown: whole from now on */
            bloodWrite(d);
        }
    }

    if (g_ChrSlots) {
        for (i = 0; i < g_NumChrSlots && i < BLOOD_CHRS; i++) {
            ChrRecord *chr = &g_ChrSlots[i];
            if (bloodChrBleeds(chr) && chr->prop->type == PROP_TYPE_CHR) bloodChrTick(chr, ticks);
            else if (!chr->model) memset(&s_chrs[i], 0, sizeof(s_chrs[i]));   /* freed: its next guard starts clean */
        }
    }
    for (i = 0; i < gevrRagdollBodies() && i < BLOOD_BODIES; i++) bloodBodyTick(i, ticks);
}

void gevrBloodReset(void)
{
    memset(s_decals, 0, sizeof(s_decals));
    memset(s_chrs, 0, sizeof(s_chrs));
    memset(s_bodies, 0, sizeof(s_bodies));
    s_next = s_live = 0;
}

int gevrBloodDecals(void) { return BLOOD_ON ? BLOOD_DECALS : 0; }

const Vtx *gevrBloodDecal(int i, s32 *room)
{
    if (i < 0 || i >= BLOOD_DECALS || s_decals[i].room <= 0) return NULL;
    *room = s_decals[i].room;
    return s_decals[i].vtx;
}
#endif
