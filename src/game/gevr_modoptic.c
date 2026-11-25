/*
 * A mod's optics (mod.attach.optic, docs/mod-system.md). An optic profile
 * from the pack (gevr_modoptic_profile.c) bound to an attachment's id: each
 * frame the attachment is drawn on a gun, its lens surface is built from the
 * pose it was just drawn at (C1's drawn frame) and emitted in the same hand
 * draw (G_GEVR_OPTIC_SURFACE), after every part of that gun, so the lens
 * moves with the housing when the headset moves a hand's draws to a newer
 * controller pose. The renderer (port/fast3d/gevr_modoptic_render.inc.h)
 * draws the world through the per-hand scope pass for the magnifying lens
 * most in line with the eye, and every lens's reticle.
 *
 * Per binding: the selected mode and zoom. The right stick, while a lens is
 * in focus, zooms (held, pushed up or down) and changes mode (a short
 * click), as the Tactical build this comes from. A mode marked folded (a
 * flip-up magnifier swung aside) draws the attachment's folded housing when
 * one is given.
 */
#include <ultra64.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bondtypes.h>
#include "system.h"
#include "dyn.h"
#include "matrixmath.h"
#include "model.h"
#include "gun.h"
#include "player.h"
#include "sched.h"
#include "gevr_modassets.h"
#include "gevr_modpack.h"
#include "gevr_modapi.h"
#include "gevr_modoptic.h"

extern s32 g_gevrStereo;
extern f32 D_800364CC;
extern s32 gevrStereoShot(s32 handnum, coord2d *spreadpos, struct coord3d *origin, struct coord3d *dir);
extern int gevrModAttachInfo(int id, int *on, int *where, int *node, int *shown, Mtxf *offset);
extern int gevrModAttachGunFrame(int id, Mtxf *pose, s32 *mirrored);
extern int gevrModAttachOpticHousing(int id, const char *folded);

typedef struct ProfileFile {
    char name[64];
    uint8_t content[32];
    const GevrOpticProfile *profile;
    struct ProfileFile *next;
} ProfileFile;

typedef struct {
    int id;                          /* the attachment's; 0 free */
    const GevrOpticProfile *profile;
    int selected;
    float zoom;
    int hasFolded;                   /* gevr_modattach.c holds the folded housing */
    s32 drawnItem;
    u32 drawnFrame;                  /* the last frame its lens was built */
    s32 drawnHand;
    GevrOpticSurface *surface;       /* this frame's, in the frame's dynamic memory */
} Binding;

/* Display lists and late eye redraws can outlive a mission reset. Keep
 * immutable profiles for the process and key them by the pack content. */
static ProfileFile *s_files;
static Binding s_bind[GEVR_MODAPI_ATTACH_MAX];
static int s_focus = -1;             /* the attachment id the stick controls */
static u32 s_focusFrame;

static const GevrOpticProfile *loadProfile(const char *name)
{
    const gevrModInfo *mod = gevrModActive();
    if (!mod) return NULL;
    for (ProfileFile *entry = s_files; entry; entry = entry->next) {
        if (!strcmp(entry->name, name) && !memcmp(entry->content, mod->content, sizeof(entry->content)))
            return entry->profile;
    }

    FILE *f = gevrModAssetOpenOptic(name);
    if (!f) {
        sysLogPrintf(LOG_WARNING, "mods: optic %s: profile could not be opened", name);
        return NULL;
    }
    const long size = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    unsigned char *bytes = size > 0 && (unsigned long)size <= GEVR_OPTIC_MAX_FILE
        && fseek(f, 0, SEEK_SET) == 0 ? malloc((size_t)size) : NULL;
    const int read = bytes && fread(bytes, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    ProfileFile *entry = read ? malloc(sizeof(*entry)) : NULL;
    if (!entry) {
        sysLogPrintf(LOG_WARNING, "mods: optic %s: profile could not be read or allocated", name);
        free(bytes);
        return NULL;
    }
    const GevrOpticProfile *profile = gevrModOpticParse(bytes, (unsigned long)size, name);
    free(bytes);
    if (!profile) {
        free(entry);
        return NULL;
    }
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    memcpy(entry->content, mod->content, sizeof(entry->content));
    entry->profile = profile;
    entry->next = s_files;
    s_files = entry;
    sysLogPrintf(LOG_NOTE, "mods: optic %s: %d mode%s", name, profile->count, profile->count == 1 ? "" : "s");
    return profile;
}

static Binding *bindingFor(int id)
{
    if (id <= 0) return NULL;
    for (int i = 0; i < GEVR_MODAPI_ATTACH_MAX; ++i)
        if (s_bind[i].id == id) return &s_bind[i];
    return NULL;
}

static int bindOptic(int id, const GevrOpticProfile *profile, const char *folded)
{
    Binding *b = bindingFor(id);
    for (int i = 0; !b && i < GEVR_MODAPI_ATTACH_MAX; ++i)
        if (!s_bind[i].id) b = &s_bind[i];
    if (!b || !gevrModAttachOpticHousing(id, folded)) return 0;
    *b = (Binding){.id = id, .profile = profile, .zoom = profile->modes[0].lowZoom,
                   .hasFolded = folded != NULL};
    return 1;
}

void gevrModOpticUnbind(int id)
{
    Binding *b = bindingFor(id);
    if (b) memset(b, 0, sizeof(*b));
    if (s_focus == id) s_focus = -1;
}

/* A mission's start or a mod stopping: every binding goes; profiles stay
 * loaded for the run (they're the pack's, unchanged) */
void gevrModOpticReset(void)
{
    memset(s_bind, 0, sizeof(s_bind));
    s_focus = -1;
}

/* Retire a bound optic as soon as its carrier changes. An attachment
 * itself may be reused by Lua, which can explicitly bind a new optic. */
void gevrModOpticCheckCarrier(int id, s32 hand)
{
    const Binding *b = bindingFor(id);
    if (b && b->surface && g_CurrentPlayer && b->drawnItem != getCurrentPlayerWeaponId(hand)) {
        gevrModOpticUnbind(id);
        gevrModAttachOpticHousing(id, NULL);
    }
}

int gevrModOpticGet(int id, int *mode, float *zoom, int *count, int *kind, int *folded)
{
    const Binding *b = bindingFor(id);
    if (!b) return 0;
    gevrModOpticCheckCarrier(id, b->drawnHand);
    if (!b->id) return 0;
    const GevrOpticMode *m = &b->profile->modes[b->selected];
    if (mode) *mode = b->selected;
    if (zoom) *zoom = b->zoom;
    if (count) *count = b->profile->count;
    if (kind) *kind = (int)m->kind;
    if (folded) *folded = (int)m->folded;
    return 1;
}

/* mode from 0; zoom clamped to the mode's range (0 keeps the mode's lowest) */
int gevrModOpticSet(int id, int mode, float zoom)
{
    Binding *b = bindingFor(id);
    if (!b || mode < 0 || mode >= b->profile->count) return 0;
    const GevrOpticMode *m = &b->profile->modes[mode];
    b->selected = mode;
    b->zoom = zoom > 0.f ? fminf(m->highZoom, fmaxf(m->lowZoom, zoom)) : m->lowZoom;
    return 1;
}

int gevrModGameOpticBind(int id, const char *profile, const char *folded)
{
    if (profile == NULL) {
        gevrModOpticUnbind(id);
        gevrModAttachOpticHousing(id, NULL);
        return 1;
    }
    const GevrOpticProfile *loaded = loadProfile(profile);
    return loaded && bindOptic(id, loaded, folded);
}

int gevrModGameOpticGet(int id, int *mode, float *zoom, int *count, int *kind, int *folded)
{
    return gevrModOpticGet(id, mode, zoom, count, kind, folded);
}

int gevrModGameOpticSet(int id, int mode, float zoom)
{
    return gevrModOpticSet(id, mode, zoom);
}

/* gevr_modattach.c: draw the attachment's folded housing in place of its own */
int gevrModOpticFolded(int id)
{
    const Binding *b = bindingFor(id);
    return b && b->hasFolded && b->profile->modes[b->selected].folded;
}

static float axisOf(const Mtxf *pose, const float axis[3], float out[3])
{
    float length = 0.f;
    for (int i = 0; i < 3; ++i) {
        out[i] = axis[0] * pose->m[0][i] + axis[1] * pose->m[1][i] + axis[2] * pose->m[2][i];
        length += out[i] * out[i];
    }
    length = sqrtf(length);
    if (length > 1e-6f)
        for (int i = 0; i < 3; ++i) out[i] /= length;
    return length;
}

static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void cross3(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

/* The profile frame to the converted mesh's: (x, y, z) -> (-x, z, y), as
 * gevr_modmesh.c converts the housing's vertices (metres stay the 1000 below) */
static void converted(const float in[3], float out[3])
{
    out[0] = -in[0];
    out[1] = in[2];
    out[2] = in[1];
}

/* A lens at a drawn attachment pose (view space, the gun model's units) */
static void buildSurface(GevrOpticSurface *s, const GevrOpticMode *mode, float zoom, const Mtxf *pose)
{
    float right[3], up[3], forward[3], centre[3];
    converted(mode->right, right);
    converted(mode->up, up);
    converted(mode->forward, forward);
    converted(mode->centre, centre);
    *s = (GevrOpticSurface){.mode = mode, .zoom = zoom};
    s->half[0] = 1000.f * mode->half[0] * axisOf(pose, right, s->right);
    s->half[1] = 1000.f * mode->half[1] * axisOf(pose, up, s->up);
    axisOf(pose, forward, s->forward);
    memcpy(s->aim, s->forward, sizeof(s->aim));
    for (int i = 0; i < 3; ++i)
        s->centre[i] = pose->m[3][i] + 1000.f * (centre[0] * pose->m[0][i] + centre[1] * pose->m[1][i]
                                                 + centre[2] * pose->m[2][i]);
}

/* Every lens on a gun aims at the same nominal shot (2500 units ahead, the
 * Tactical build's reference); a lens that can't see it keeps its own axis */
static void zeroSurface(GevrOpticSurface *s, const float origin[3], const float shot[3], float distance)
{
    float to[3], length2 = 0.f, forwards = 0.f;
    for (int i = 0; i < 3; ++i) {
        to[i] = origin[i] + distance * shot[i] - s->centre[i];
        length2 += to[i] * to[i];
        forwards += to[i] * s->forward[i];
    }
    const float length = sqrtf(length2);
    if (isfinite(length) && length > 1e-4f && forwards > .65f * length) {
        for (int i = 0; i < 3; ++i) {
            s->zero[i] = to[i];
            s->aim[i] = to[i] / length;
        }
    } else {
        for (int i = 0; i < 3; ++i) {
            s->zero[i] = s->forward[i] * distance;
            s->aim[i] = s->forward[i];
        }
    }
}

/* gevr_modattach.c, as an attachment is drawn on a hand's gun: its lens */
void gevrModOpticDrawn(int id, s32 hand, const Mtxf *pose)
{
    Binding *b = bindingFor(id);
    if (!b) return;
    GevrOpticSurface *s = dynAllocate(sizeof(*s));
    if (!s) return;
    buildSurface(s, &b->profile->modes[b->selected], b->zoom, pose);
    const float distance = 2500.f * D_800364CC;
    struct coord3d origin, aim;
    if (g_gevrStereo && gevrStereoShot(hand, NULL, &origin, &aim)) zeroSurface(s, origin.f, aim.f, distance);
    else zeroSurface(s, s->centre, s->forward, distance);
    b->surface = s;
    b->drawnHand = hand;
    b->drawnItem = getCurrentPlayerWeaponId(hand);
    b->drawnFrame = os_scheduler.frameCount;
}

/* The end of a hand's gun draw: its lenses, the live one (the magnifying
 * lens most in line with the eye) first so its scene is ready before the
 * rest, as the Tactical build draws them; the one most in line the stick's */
Gfx *gevrModOpticEmit(Gfx *gdl, s32 hand)
{
    Binding *live = NULL;
    float best = 0.f, bestFocus = .966f;
    int any = 0;
    for (int i = 0; i < GEVR_MODAPI_ATTACH_MAX; ++i) {
        Binding *b = &s_bind[i];
        if (!b->id || !b->surface || b->drawnHand != hand || b->drawnFrame != os_scheduler.frameCount) continue;
        any = 1;
        const float *c = b->surface->centre;
        const float length = sqrtf(dot3(c, c));
        const float alignment = length > 1e-6f ? dot3(c, b->surface->forward) / length : 0.f;
        if (c[2] < 0.f && alignment > bestFocus) {
            s_focus = b->id;
            s_focusFrame = os_scheduler.frameCount;
            bestFocus = alignment;
        }
        const unsigned kind = b->surface->mode->kind;
        const float minAlignment = kind == GEVR_OPTIC_SCOPE ? .85f : .90f;
        if (kind != GEVR_OPTIC_COLLIMATOR && c[2] < 0.f && alignment > minAlignment && alignment > best) {
            live = b;
            best = alignment;
        }
    }
    if (!any) return gdl;
    if (live) {
        live->surface->live = TRUE;
        gDPParam(gdl++, G_GEVR_OPTIC_SURFACE, osVirtualToPhysical(live->surface));
    }
    for (int i = 0; i < GEVR_MODAPI_ATTACH_MAX; ++i) {
        Binding *b = &s_bind[i];
        if (b == live || !b->id || !b->surface || b->drawnHand != hand || b->drawnFrame != os_scheduler.frameCount)
            continue;
        gDPParam(gdl++, G_GEVR_OPTIC_SURFACE, osVirtualToPhysical(b->surface));
    }
    return gdl;
}

/* lv.c, before the world: keep it for a lens this frame when a magnifying
 * mode was on a gun last frame (the record costs a draw of the world) */
s32 gevrModOpticWantScope(void)
{
    if (!g_CurrentPlayer || g_CurrentPlayer->bonddead || g_CurrentPlayer->watch_animation_state) return FALSE;
    for (int i = 0; i < GEVR_MODAPI_ATTACH_MAX; ++i) {
        const Binding *b = &s_bind[i];
        if (b->id && b->profile->modes[b->selected].kind != GEVR_OPTIC_COLLIMATOR
            && gevrModAttachGunFrame(b->id, NULL, NULL))
            return TRUE;
    }
    return FALSE;
}

static int canAdjust(int id)
{
    const Binding *b = bindingFor(id);
    if (!b) return 0;
    const GevrOpticMode *m = &b->profile->modes[b->selected];
    return b->profile->count > 1 || m->highZoom > m->lowZoom;
}

static void cycle(int id)
{
    Binding *b = bindingFor(id);
    if (b && b->profile->count > 1) gevrModOpticSet(id, (b->selected + 1) % b->profile->count, 0.f);
}

static int adjustZoom(int id, float amount)
{
    Binding *b = bindingFor(id);
    if (!b) return 0;
    const GevrOpticMode *m = &b->profile->modes[b->selected];
    if (m->highZoom <= m->lowZoom) return 0;
    b->zoom = fminf(m->highZoom, fmaxf(m->lowZoom, b->zoom * expf(amount)));
    return 1;
}

/* input.c, each frame in VR: the right stick's click and its y, while the
 * eye is behind a lens. True when the optic took them from the game. A
 * short click changes mode; held, the stick zooms. */
int gevrModOpticInput(int enabled, int click, float y)
{
    static int active = -1, scrolled;
    static u32 started, lastAdjust;
    const int valid = enabled && g_gevrStereo && (u32)os_scheduler.frameCount - s_focusFrame <= 1u
        && bindingFor(s_focus) != NULL && gevrModAttachGunFrame(s_focus, NULL, NULL)
        && (active < 0 || gevrModAttachGunFrame(active, NULL, NULL));
    if (!valid) {
        active = -1;
        return FALSE;
    }
    const u32 now = os_scheduler.frameCount;
    if (active >= 0) {
        if (!click) {
            if (!scrolled && now - started < 40u) cycle(active);
            active = -1;
        } else if (fabsf(y) > .25f && lastAdjust != now) {
            scrolled |= adjustZoom(active, .026f * y);
            lastAdjust = now;
        }
        return TRUE;
    }
    if (!click || !canAdjust(s_focus)) return FALSE;
    active = s_focus;
    started = lastAdjust = now;
    scrolled = FALSE;
    return TRUE;
}

/*
 * A headset-free check (--input-script with --mod-optic-preview, flat):
 * the hand's gun turned so the eye looks down its first bound lens from
 * 16 units behind it, as the Tactical build's optic preview does, so a
 * desktop capture shows what an eye at the sight would see.
 */
void gevrModOpticPreviewAlign(s32 hand, Model *gun)
{
    if (g_gevrStereo || gun == NULL || gun->render_pos == NULL || gun->obj == NULL
        || !sysArgCheck("--input-script") || !sysArgCheck("--mod-optic-preview"))
        return;
    for (int i = 0; i < GEVR_MODAPI_ATTACH_MAX; ++i) {
        const Binding *b = &s_bind[i];
        int on, where, node, shown;
        Mtxf offset, pose;
        if (!b->id || !gevrModAttachInfo(b->id, &on, &where, &node, &shown, &offset)
            || on != GEVR_MODATTACH_GUN || where != hand || !shown || node >= gun->obj->numMatrices
            || (b->surface && b->drawnItem != getCurrentPlayerWeaponId(hand)))
            continue;
        matrix_4x4_multiply(&((Mtxf *)gun->render_pos)[node], &offset, &pose);
        GevrOpticSurface s;
        float right[3], up[3];
        buildSurface(&s, &b->profile->modes[b->selected], 1.f, &pose);
        cross3(s.forward, s.up, right);
        const float r = sqrtf(dot3(right, right));
        if (r <= 1e-6f) return;
        for (int n = 0; n < 3; ++n) right[n] /= r;
        cross3(right, s.forward, up);
        for (int m = 0; m < gun->obj->numMatrices; ++m) {
            Mtxf *joint = &((Mtxf *)gun->render_pos)[m];
            for (int row = 0; row < 4; ++row) {
                float p[3] = {joint->m[row][0], joint->m[row][1], joint->m[row][2]};
                if (row == 3)
                    for (int n = 0; n < 3; ++n) p[n] -= s.centre[n];
                joint->m[row][0] = dot3(p, right);
                joint->m[row][1] = dot3(p, up);
                joint->m[row][2] = -dot3(p, s.forward) - (row == 3 ? 16.f : 0.f);
            }
        }
        return;
    }
}
