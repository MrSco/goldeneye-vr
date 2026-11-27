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
 * - Splats land on the level, not on doors or other objects (PD's objTestHit
 *   pass); a splat's own ring of 96 keeps them from pushing out bullet holes.
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
#include "bg.h"
#include "bgroomtrans.h"
#include "bondview.h"
#include "chr.h"
#include "matrixmath.h"
#include "stan.h"
#include "image_bank.h"
#include "gevr_blood.h"
#include "gevr_bean.h"
#include "gevr_ragdoll.h"
#include "system.h"

extern int VrBloodPatch;   /* vr_settings: Mods, the blood patch */
s32 bgGetConnectedRooms(s32 roomIndex, s32 *list, s32 max);   /* bg.c */
s32 chrpropRayIntersectsRoomBbox(s32 room, coord3d *start, coord3d *dir);   /* chrprop.c */

#define BLOOD_DECALS        96
#define BLOOD_CHRS          512        /* gevr_ragdoll.c keeps 512 chrs too */
#define BLOOD_BODIES        GEVR_BLOOD_BODIES

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
    s16 chrnum;
    u8 bulletstaken, deaddrops, droppedhere, wounded;
    s32 tickssincesplat;
    coord3d lastdroppos;
} BloodChr;

typedef struct BloodBody {
    unsigned serial;
    s32 ticks;
    u8 pools;
} BloodBody;

static BloodDecal s_decals[BLOOD_DECALS];
static int s_next, s_live;
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

    memset(d, 0, sizeof(*d));
    for (k = 0; k < 3; k++) {
        d->centre[k] = pos[k] * roomscale - roompos->f[k];
        d->across[k] = across[k] * w * roomscale;
        d->up[k] = up[k] * h * roomscale;
        const float reach = fabsf(d->centre[k]) + fabsf(d->across[k]) + fabsf(d->up[k]);
        if (reach > most) most = reach;
    }
    /* the corners in eighths of a room unit, as a bullet hole's (explosion.c, #269) */
    while (fine > 1 && most * fine > 32000.0f) fine >>= 1;
    if (most > 32000.0f) return;
    d->fine = (s16)fine;
    d->room = (s16)room;
    d->timermax = timermax;
    d->fade = 1.0f;
    /* gBloodColour, a little lighter or darker splat to splat */
    {
        const float shade = 0.85f + 0.3f * bloodFrac();
        for (k = 0; k < 3; k++) {
            const float c = (float)gBloodColour.rgba[k] * shade;
            d->rgb[k] = (u8)(c > 255.0f ? 255.0f : c);
        }
    }
    d->alpha = (u8)(0xc0 + bloodRandom() % 0x40);   /* wallhit.c's minalpha .. maxalpha */
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
        }
        nmore += bgGetConnectedRooms(room, more + nmore, 40);
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

    bloodSpread(dir, spread);
    for (k = 0; k < 3; k++) to.f[k] = from->f[k] + spread[k] * SPLAT_MAX_DISTANCE;
    if (!bloodTrace(from, &to, room, hit, normal, &hitroom)) return 0;
    for (k = 0; k < 3; k++) distance += (hit[k] - from->f[k]) * (hit[k] - from->f[k]);
    distance = sqrtf(distance);
    if (distance >= SPLAT_MAX_DISTANCE) return 0;
    {
        const unsigned r1 = bloodRandom();
        const float r2 = bloodFrac();
        const float r3 = bloodFrac();
        bloodSplatSize(distance, scales[type], r1, r2, r3, &w, &h);
    }
    bloodDecalCreate(hit, normal, hitroom, w, h, timers[type]);
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
    if (s->chr != chr || s->chrnum != chr->chrnum) {
        if (!create) return NULL;
        memset(s, 0, sizeof(*s));
        s->chr = chr;
        s->chrnum = chr->chrnum;
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

    if (!VrBloodPatch || !bloodChrBleeds(chr) || !hitpos || !shotdir) return;
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
    if (dying) {
        if (gevrRagdollSuppress(chr)) return;   /* the ragdoll body bleeds instead */
        if (bloodChrDownEnough(chr) && s->tickssincesplat > 30 && s->deaddrops < 6) {
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
    if (!serial || !resting || s->pools >= 6) return;
    s->ticks += ticks;
    if (s->ticks > 30) {
        StandTile *tile;
        coord3d from = root;
        from.y += SPLAT_DROP_HEIGHT;
        tile = stanFindTileBelowPos(&from, NULL, NULL);
        if (tile && bloodSplat(&from, down, getTileRoom(tile), SPLAT_PUDDLE)) s->pools++;
        s->ticks = 0;
    }
}

void gevrBloodTick(int ticks)
{
    int i;

    if (!VrBloodPatch) {
        if (s_live) gevrBloodReset();
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

int gevrBloodDecals(void) { return VrBloodPatch ? BLOOD_DECALS : 0; }

const Vtx *gevrBloodDecal(int i, s32 *room)
{
    if (i < 0 || i >= BLOOD_DECALS || s_decals[i].room <= 0) return NULL;
    *room = s_decals[i].room;
    return s_decals[i].vtx;
}
#endif
