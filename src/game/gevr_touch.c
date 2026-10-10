/*
 * The watch's Controls page shows the Meta Quest Touch controllers in hand
 * instead of the N64 controller (options.c draw_watch_controller): the pair
 * for this headset (Quest 1, Quest 2, Touch Plus for Quest 3 and 3S, Touch
 * Pro; ini TouchControllers picks one by hand), each standing over a list of
 * its controls and what they do in the current play mode and handedness.
 * Like the N64 controller, the pair spins and tilts with the stick while the
 * page's controller row is selected; a trigger, grip, stick or button moves
 * on the drawing as it does in the hand, and lights with its line.
 *
 * The models are Meta's controller art (docs/touch-controllers.md), made
 * into port/src/gevr_touch_model_data.c by tools/gevr_touch_model_gen.py:
 * vertex colours from the textures, each part with its pivot. They are lit
 * here, per vertex, each frame (the generator's previews light them the
 * same way: keep the two in step), and drawn through display lists built
 * once: GoldenEye's 16-vertex G_VTX loads and G_TRI4s, one list a side.
 */

#include <ultra64.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "textrelated.h"
#include "dyn.h"
#include "player.h"
#include "gun.h"
#include "system.h"
#include "gevr_touch_model.h"
#include "gevr_touch.h"
#ifdef ANDROID
#include <sys/system_properties.h>
#endif

#ifndef G_TRI4
#define G_TRI4 0xB1   /* Rare's four-triangle command (fast3d gfx_sp_tri4) */
#endif

#define GEVR_TOUCH_MAXVERTS 2048

/* the label lists under the controllers, in the page's 320 x 240 */
#define GEVR_TOUCH_ROW_Y      133
#define GEVR_TOUCH_ROW_PITCH  13
#define GEVR_TOUCH_LEFT_X     36
#define GEVR_TOUCH_RIGHT_X    164
#define GEVR_TOUCH_NAME_GAP   7
#define GEVR_TOUCH_LEFT_EDGE  156   /* the left list ends short of the middle */
#define GEVR_TOUCH_RIGHT_EDGE 284   /* inside the N64 page's outermost labels (287) */
#define GEVR_TOUCH_MIN_X      24

extern int VrTouchModel, VrLeftHandedMode, VrSwapJoysticks, VrPlayMode, VrMotionThrowing;
extern int VrGestureHolster, VrGestureGripUse, VrGesturePickup, VrGestureMineGrab, VrBodySlots;
extern s32 gevrIsThrowable(s32 item);              /* port/src/input.c */
extern const char *gevrHmdName(void);              /* vr_openxr.cpp */
extern const char *gevrTouchProfileName(void);     /* vr_input.cpp */
extern int gevrTouchPadState(int phys, float *trigger, float *grip, float *stickx, float *sticky, int *buttons);
extern s32 gevrDualWielding(void);                 /* gunfire.c: the off hand holds something */
extern s32 gevrGexMineDetonates(void);             /* gun.c: GE-X's remote mines are out */
extern Gfx *draw_options_labels(Gfx *gdl, s32 x, s32 y, char *text, u32 colour, s32 outlined, u32 outlinecolour,
                                s32 centre, s32 drawbg, u32 bgcolour, s32 rightalign);

typedef struct {
    f32 trigger, grip, stickx, sticky;
    s32 buttons;   /* gevrTouchPadState's bits */
} GevrTouchPad;

typedef struct {
    f32 r[3][3];
    f32 piv[3];
    f32 t[3];
    s32 lit;
} GevrTouchPose;

static Vtx *s_vtx[GEVR_TOUCH_KINDS][2];
static Gfx *s_dl[GEVR_TOUCH_KINDS][2];
static s16 s_pos[GEVR_TOUCH_MAXVERTS][3];
static u8 s_col[GEVR_TOUCH_MAXVERTS][3];

/* ------------------------------------------------------------ which pair */

static s32 gevrTouchHas(const char *hay, const char *needle)
{
    size_t n = strlen(needle);

    for (; hay && *hay; hay++)
    {
        size_t i;

        for (i = 0; i < n && hay[i] && (hay[i] | 0x20) == (needle[i] | 0x20); i++)
        {
        }
        if (i == n)
        {
            return 1;
        }
    }
    return 0;
}

s32 gevrTouchKind(void)
{
    static char model[96];
    static s32 modelRead = 0;
    static s32 logged = -1;
    const char *hmd = gevrHmdName();
    const char *profile = gevrTouchProfileName();
    s32 kind;

    if (VrTouchModel >= 1 && VrTouchModel <= GEVR_TOUCH_KINDS)
    {
        return VrTouchModel - 1;
    }
    if (!modelRead)
    {
#ifdef ANDROID
        char value[PROP_VALUE_MAX] = "";
        __system_property_get("ro.product.model", value);
        strncpy(model, value, sizeof(model) - 1);
#endif
        modelRead = 1;
    }
    /* The runtime names the headset ("Oculus Quest", "Oculus Quest2", "Meta
     * Quest 3", "Meta Quest Pro"), as does Android's model ("Quest 3S");
     * a profile that names its controllers wins. Not a Quest: Touch Plus. */
    if (gevrTouchHas(profile, "touch_controller_pro"))
    {
        kind = GEVR_TOUCH_PRO;
    }
    else if (gevrTouchHas(profile, "touch_controller_plus"))
    {
        kind = GEVR_TOUCH_PLUS;
    }
    else if (gevrTouchHas(hmd, "pro") || gevrTouchHas(model, "pro"))
    {
        kind = GEVR_TOUCH_PRO;
    }
    else if (gevrTouchHas(hmd, "quest 3") || gevrTouchHas(hmd, "quest3") || gevrTouchHas(model, "quest 3") || gevrTouchHas(model, "quest3"))
    {
        kind = GEVR_TOUCH_PLUS;
    }
    else if (gevrTouchHas(hmd, "quest 2") || gevrTouchHas(hmd, "quest2") || gevrTouchHas(model, "quest 2") || gevrTouchHas(model, "quest2"))
    {
        kind = GEVR_TOUCH_QUEST2;
    }
    else if (gevrTouchHas(hmd, "quest") || gevrTouchHas(model, "quest"))
    {
        kind = GEVR_TOUCH_QUEST1;
    }
    else
    {
        kind = GEVR_TOUCH_PLUS;
    }
    if (kind != logged)
    {
        sysLogPrintf(LOG_NOTE, "touch: drawing %s controllers (headset \"%s\", model \"%s\", profile \"%s\")",
                     gevrTouchModels[kind].name, hmd ? hmd : "", model, profile ? profile : "");
        logged = kind;
    }
    return kind;
}

/* ------------------------------------------------------------ the parts */

static void gevrTouchReadPads(GevrTouchPad pads[2])
{
    s32 p;

    for (p = 0; p < 2; p++)
    {
        int buttons;

        gevrTouchPadState(p, &pads[p].trigger, &pads[p].grip, &pads[p].stickx, &pads[p].sticky, &buttons);
        pads[p].buttons = buttons;
    }
}

/* m = the turn by a round axis (unit), for column vectors */
static void gevrTouchRot(f32 m[3][3], const f32 axis[3], f32 a)
{
    f32 c = cosf(a), s = sinf(a), C = 1.0f - c;
    f32 x = axis[0], y = axis[1], z = axis[2];

    m[0][0] = c + x * x * C;     m[0][1] = x * y * C - z * s; m[0][2] = x * z * C + y * s;
    m[1][0] = y * x * C + z * s; m[1][1] = c + y * y * C;     m[1][2] = y * z * C - x * s;
    m[2][0] = z * x * C - y * s; m[2][1] = z * y * C + x * s; m[2][2] = c + z * z * C;
}

static void gevrTouchMul(f32 out[3][3], f32 a[3][3], f32 b[3][3])
{
    s32 i, j;

    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 3; j++)
        {
            out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
        }
    }
}

static void gevrTouchPoses(const GevrTouchMesh *m, const GevrTouchPad *pad, GevrTouchPose pose[GEVR_TOUCH_PARTS])
{
    static const f32 none[3] = { 0.0f, 0.0f, 0.0f };
    f32 a[3][3], b[3][3];
    s32 i, j;

    for (i = 0; i < GEVR_TOUCH_PARTS; i++)
    {
        const GevrTouchPart *part = &m->parts[i];
        GevrTouchPose *pp = &pose[i];

        gevrTouchRot(pp->r, none, 0.0f);   /* no axis, no turn: the identity */
        for (j = 0; j < 3; j++)
        {
            pp->piv[j] = part->pivot[j];
            pp->t[j] = 0.0f;
        }
        pp->lit = 0;
        switch (i)
        {
            case GEVR_TOUCH_TRIGGER:   /* swings back toward the grip */
                gevrTouchRot(pp->r, part->axis, -pad->trigger * gevrTouchTriggerAngle);
                pp->lit = pad->trigger >= 0.5f;
                break;
            case GEVR_TOUCH_GRIP:
                for (j = 0; j < 3; j++)
                {
                    pp->t[j] = part->axis[j] * pad->grip * gevrTouchGripTravel;
                }
                pp->lit = pad->grip >= 0.5f;
                break;
            case GEVR_TOUCH_STICK:   /* forward tilts it toward the ring; a click pushes it in */
            {
                const f32 *r = part->axis, *f = part->axis2;
                f32 up[3];
                f32 in = (pad->buttons & 8) ? gevrTouchStickTravel : 0.0f;

                up[0] = r[1] * f[2] - r[2] * f[1];
                up[1] = r[2] * f[0] - r[0] * f[2];
                up[2] = r[0] * f[1] - r[1] * f[0];
                gevrTouchRot(a, f, pad->stickx * gevrTouchStickAngle);
                gevrTouchRot(b, r, -pad->sticky * gevrTouchStickAngle);
                gevrTouchMul(pp->r, a, b);
                for (j = 0; j < 3; j++)
                {
                    pp->t[j] = -up[j] * in;
                }
                pp->lit = (pad->buttons & 8) || pad->stickx * pad->stickx + pad->sticky * pad->sticky > 0.09f;
                break;
            }
            case GEVR_TOUCH_LOWER:
            case GEVR_TOUCH_UPPER:
            case GEVR_TOUCH_MENU:
            {
                s32 bit = i == GEVR_TOUCH_LOWER ? 1 : i == GEVR_TOUCH_UPPER ? 2 : 4;

                if (pad->buttons & bit)
                {
                    for (j = 0; j < 3; j++)
                    {
                        pp->t[j] = part->axis[j] * gevrTouchButtonTravel;
                    }
                    pp->lit = 1;
                }
                break;
            }
            default:
                break;
        }
    }
}

/* ------------------------------------------------------------ drawing */

/* the display list for one side: its loads in order, then the end */
static void gevrTouchBuild(const GevrTouchMesh *m, s32 kind, s32 side)
{
    const u16 *b = m->batches;
    s32 cmds = 1;
    s32 first = 0;
    s32 i, t, k;
    Gfx *g;

    for (i = 0; i < m->nbatches; i++)
    {
        cmds += 1 + (b[1] + 3) / 4;
        b += 2 + b[0] + b[1];
    }
    s_vtx[kind][side] = calloc(m->nstream, sizeof(Vtx));
    s_dl[kind][side] = g = calloc(cmds, sizeof(Gfx));
    if (!s_vtx[kind][side] || !g)
    {
        sysLogPrintf(LOG_ERROR, "touch: out of memory for the %s controllers", gevrTouchModels[kind].name);
        free(s_vtx[kind][side]);
        free(g);
        s_vtx[kind][side] = NULL;
        s_dl[kind][side] = NULL;
        return;
    }
    b = m->batches;
    for (i = 0; i < m->nbatches; i++)
    {
        s32 nv = b[0], nt = b[1];
        const u16 *tris = b + 2 + nv;

        gSPVertex(g++, osVirtualToPhysical(&s_vtx[kind][side][first]), nv, 0);
        for (t = 0; t < nt; t += 4)
        {
            u32 w0 = (u32) (G_TRI4 & 0xff) << 24, w1 = 0;

            for (k = 0; k < 4 && t + k < nt; k++)
            {
                u32 tri = tris[t + k];

                w0 |= ((tri >> 8) & 15) << (4 * k);
                w1 |= (tri & 0xff) << (8 * k);   /* the first two indices, a low and b high */
            }
            g->words.w0 = w0;
            g->words.w1 = w1;
            g++;
        }
        first += nv;
        b += 2 + nv + nt;
    }
    gSPEndDisplayList(g++);
}

/*
 * One side's vertices for this frame: each part where the hand has it, lit
 * in the page's eye space (base: the page's model view). Keep in step with
 * tools/gevr_touch_model_gen.py light().
 */
static void gevrTouchLightSide(const GevrTouchMesh *m, const GevrTouchPose *pose, Mtxf *base, s32 kind, s32 side, s32 alpha)
{
    static const f32 L[3] = { -0.34727f, 0.54571f, 0.76264f };   /* normalised (-0.35, 0.55, 0.76) */
    f32 H[3];
    const u16 *b;
    Vtx *o = s_vtx[kind][side];
    f32 hl;
    s32 i, j;

    H[0] = L[0];
    H[1] = L[1];
    H[2] = L[2] + 1.0f;
    hl = 1.0f / sqrtf(H[0] * H[0] + H[1] * H[1] + H[2] * H[2]);
    H[0] *= hl;
    H[1] *= hl;
    H[2] *= hl;

    for (i = 0; i < m->nverts; i++)
    {
        const GevrTouchVert *v = &m->verts[i];
        const GevrTouchPose *pp = &pose[v->part < GEVR_TOUCH_PARTS ? v->part : 0];
        f32 p[3], n[3], q[3], e[3], len, d, h, rim, c[3];

        p[0] = v->x - pp->piv[0];
        p[1] = v->y - pp->piv[1];
        p[2] = v->z - pp->piv[2];
        n[0] = v->nx / 127.0f;
        n[1] = v->ny / 127.0f;
        n[2] = v->nz / 127.0f;
        for (j = 0; j < 3; j++)
        {
            q[j] = pp->r[j][0] * p[0] + pp->r[j][1] * p[1] + pp->r[j][2] * p[2] + pp->piv[j] + pp->t[j];
        }
        for (j = 0; j < 3; j++)
        {
            p[j] = pp->r[j][0] * n[0] + pp->r[j][1] * n[1] + pp->r[j][2] * n[2];
        }
        for (j = 0; j < 3; j++)
        {
            e[j] = base->m[0][j] * p[0] + base->m[1][j] * p[1] + base->m[2][j] * p[2];
        }
        len = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
        if (len > 0.0f)
        {
            e[0] /= len;
            e[1] /= len;
            e[2] /= len;
        }
        d = e[0] * L[0] + e[1] * L[1] + e[2] * L[2];
        d = d > 0.0f ? d : 0.0f;
        h = e[0] * H[0] + e[1] * H[1] + e[2] * H[2];
        h = h > 0.0f ? h : 0.0f;
        h *= h;
        h *= h;
        h *= h;
        h *= h;   /* ^16 */
        rim = 1.0f - (e[2] > 0.0f ? e[2] : 0.0f);
        rim = rim * rim * rim;
        c[0] = (v->r / 255.0f) * (0.40f + 0.78f * d) + 0.30f * h + 0.30f * rim * 0.55f;
        c[1] = (v->g / 255.0f) * (0.40f + 0.78f * d) + 0.30f * h + 0.30f * rim * 0.75f;
        c[2] = (v->b / 255.0f) * (0.40f + 0.78f * d) + 0.30f * h + 0.30f * rim * 0.55f;
        if (pp->lit)
        {
            /* the watch's own highlight green (options.c set_page_rectangle_colors) */
            c[0] = c[0] * 0.45f + (0x50 / 255.0f) * 0.55f;
            c[1] = c[1] * 0.45f + (0xf0 / 255.0f) * 0.55f;
            c[2] = c[2] * 0.45f + (0x50 / 255.0f) * 0.55f;
        }
        for (j = 0; j < 3; j++)
        {
            f32 x = q[j] + (q[j] >= 0.0f ? 0.5f : -0.5f);

            s_pos[i][j] = (s16) (x > 32767.0f ? 32767.0f : x < -32768.0f ? -32768.0f : x);
            s_col[i][j] = (u8) (c[j] >= 1.0f ? 255 : c[j] <= 0.0f ? 0 : (s32) (c[j] * 255.0f + 0.5f));
        }
    }

    b = m->batches;
    for (i = 0; i < m->nbatches; i++)
    {
        s32 nv = b[0], nt = b[1];

        for (j = 0; j < nv; j++, o++)
        {
            u16 idx = b[2 + j];

            o->v.ob[0] = s_pos[idx][0];
            o->v.ob[1] = s_pos[idx][1];
            o->v.ob[2] = s_pos[idx][2];
            o->v.flag = 0;
            o->v.tc[0] = o->v.tc[1] = 0;
            o->v.cn[0] = s_col[idx][0];
            o->v.cn[1] = s_col[idx][1];
            o->v.cn[2] = s_col[idx][2];
            o->v.cn[3] = alpha;
        }
        b += 2 + nv + nt;
    }
}

Gfx *gevrTouchRender(Gfx *gdl, Mtxf *basemtx, s32 alpha)
{
    const s32 kind = gevrTouchKind();
    const GevrTouchModel *model = &gevrTouchModels[kind];
    GevrTouchPad pads[2];
    GevrTouchPose pose[GEVR_TOUCH_PARTS];
    Mtx *mv;
    s32 side;

    alpha = alpha < 0 ? 0 : alpha > 0xff ? 0xff : alpha;
    gevrTouchReadPads(pads);

    mv = dynAllocateMatrix();
    guMtxF2L(basemtx->m, mv);
    gSPMatrix(gdl++, osVirtualToPhysical(mv), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gDPPipeSync(gdl++);
    gSPClearGeometryMode(gdl++, G_LIGHTING | G_FOG | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(gdl++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK);
    gSPTexture(gdl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);   /* the vertex alpha fades them with the page */
    if (alpha >= 0xff)
    {
        gDPSetRenderMode(gdl++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    }
    else
    {
        gDPSetRenderMode(gdl++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
    }

    for (side = 0; side < 2; side++)
    {
        const GevrTouchMesh *m = &model->side[side];

        if (m->nverts > GEVR_TOUCH_MAXVERTS)
        {
            sysLogPrintf(LOG_ERROR, "touch: %s has %d vertices a side, room for %d", model->name, m->nverts, GEVR_TOUCH_MAXVERTS);
            continue;
        }
        if (!s_dl[kind][side])
        {
            gevrTouchBuild(m, kind, side);
            if (!s_dl[kind][side])
            {
                continue;
            }
        }
        gevrTouchPoses(m, &pads[side], pose);
        gevrTouchLightSide(m, pose, basemtx, kind, side, alpha);
        gSPDisplayList(gdl++, osVirtualToPhysical(s_dl[kind][side]));
    }
    gDPPipeSync(gdl++);
    return gdl;
}

/* ------------------------------------------------------------ the lists */

typedef struct {
    const char *name;
    const char *action;
    s32 lit;
} GevrTouchRow;

/*
 * Everything a row can say a control does. The lists are laid out for the
 * widest of these, so no state runs a line off the page.
 */
static const char *const s_gevrTouchActions[] = {
    "Fire", "Detonate", "-", "Aim", "Aim / use", "Throw", "Sight", "Grab", "Hold gun",
    "Move", "Crouch", "Turn", "Action", "Weapon", "Hand item", "Pause",
};

/*
 * What each control of a physical controller does now, from port/src/input.c
 * and the VR settings, so a change on the watch's VR page shows here at once:
 *  - triggers fire; in stereo the off hand's fires what that hand holds and
 *    does nothing with it empty, and GoldenEye X's remote mines out take it;
 *  - the gun hand's (the right, or the left when left-handed) grip aims, and
 *    with Grip use on uses doors and switches it is at; in stereo the off
 *    hand's shows its gun's sight while it holds one (issue #37), and empty
 *    takes the gestures that are on (holster, use, pickup, mine re-grab, body
 *    slots) and holds the gun two-handed - only that with them all off. With
 *    Motion throwing a grip throws what its hand holds that throws. On the
 *    screen both grips aim;
 *  - A/X: weapons (in stereo the off hand's lower button picks that hand's
 *    item), B/Y: action. The move stick is the off hand's unless Swap
 *    sticks; Menu is always the left controller's.
 */
static s32 gevrTouchRows(s32 phys, const GevrTouchPad *pad, GevrTouchRow rows[6])
{
    const s32 stereo = VrPlayMode != 0;
    const s32 gun = phys == (VrLeftHandedMode ? 0 : 1);
    const s32 moveStick = phys == (((VrSwapJoysticks != 0) != (VrLeftHandedMode != 0)) ? 1 : 0);
    const s32 click = (pad->buttons & 8) != 0;
    const s32 throws = g_CurrentPlayer != NULL && VrMotionThrowing
        && gevrIsThrowable(getCurrentPlayerWeaponId(gun ? GUNRIGHT : GUNLEFT));
    const s32 gestures = VrGestureHolster || VrGestureGripUse || VrGesturePickup || VrGestureMineGrab || VrBodySlots;
    s32 n = 0;

    rows[n].name = "Trigger";
    rows[n].action = gun ? "Fire" : gevrGexMineDetonates() ? "Detonate" : !stereo || gevrDualWielding() ? "Fire" : "-";
    rows[n++].lit = pad->trigger >= 0.5f;
    rows[n].name = "Grip";
    if (!stereo)
    {
        rows[n].action = "Aim";
    }
    else if (throws)
    {
        rows[n].action = "Throw";
    }
    else if (gun)
    {
        rows[n].action = VrGestureGripUse ? "Aim / use" : "Aim";
    }
    else
    {
        rows[n].action = gevrDualWielding() ? "Sight" : gestures ? "Grab" : "Hold gun";
    }
    rows[n++].lit = pad->grip >= 0.5f;
    rows[n].name = "Stick";
    rows[n].action = !moveStick ? "Turn" : click && stereo ? "Crouch" : "Move";
    rows[n++].lit = click || pad->stickx * pad->stickx + pad->sticky * pad->sticky > 0.09f;
    rows[n].name = phys ? "B" : "Y";
    rows[n].action = "Action";
    rows[n++].lit = (pad->buttons & 2) != 0;
    rows[n].name = phys ? "A" : "X";
    rows[n].action = gun || !stereo ? "Weapon" : "Hand item";
    rows[n++].lit = (pad->buttons & 1) != 0;
    if (phys == 0)
    {
        rows[n].name = "Menu";
        rows[n].action = "Pause";
        rows[n++].lit = (pad->buttons & 4) != 0;
    }
    return n;
}

static s32 gevrTouchTextWidth(const char *text)
{
    s32 height, width;

    textMeasure(&height, &width, (char *) text, ptrFontBankGothicChars, ptrFontBankGothic, 10);
    return width;
}

Gfx *gevrTouchDrawLabels(Gfx *gdl)
{
    GevrTouchPad pads[2];
    GevrTouchRow rows[6];
    s32 actionw = 0;
    s32 phys, i, n;

    gevrTouchReadPads(pads);
    gdl = microcode_constructor(gdl);
    for (i = 0; i < (s32) ARRAYCOUNT(s_gevrTouchActions); i++)
    {
        s32 w = gevrTouchTextWidth(s_gevrTouchActions[i]);

        actionw = w > actionw ? w : actionw;
    }
    for (phys = 0; phys < 2; phys++)
    {
        s32 namew = 0;
        s32 x, width;

        n = gevrTouchRows(phys, &pads[phys], rows);
        for (i = 0; i < n; i++)
        {
            s32 w = gevrTouchTextWidth(rows[i].name);

            namew = w > namew ? w : namew;
        }
        /* each list where it stands under its controller, unless its widest
         * line would cross the middle (left) or the page's edge (right) */
        width = namew + GEVR_TOUCH_NAME_GAP + actionw;
        if (phys == 0)
        {
            x = GEVR_TOUCH_LEFT_X + width > GEVR_TOUCH_LEFT_EDGE ? GEVR_TOUCH_LEFT_EDGE - width : GEVR_TOUCH_LEFT_X;
            x = x < GEVR_TOUCH_MIN_X ? GEVR_TOUCH_MIN_X : x;
        }
        else
        {
            x = GEVR_TOUCH_RIGHT_X + width > GEVR_TOUCH_RIGHT_EDGE ? GEVR_TOUCH_RIGHT_EDGE - width : GEVR_TOUCH_RIGHT_X;
        }
        for (i = 0; i < n; i++)
        {
            const s32 y = GEVR_TOUCH_ROW_Y + i * GEVR_TOUCH_ROW_PITCH;

            /* held: white and outlined, as the N64 controller's labels light */
            if (rows[i].lit)
            {
                gdl = draw_options_labels(gdl, x, y, (char *) rows[i].name, -1, 1, 0x7000A0, 0, 0, 0x3000B0, 0);
                gdl = draw_options_labels(gdl, x + namew + GEVR_TOUCH_NAME_GAP, y, (char *) rows[i].action, -1, 1, 0x7000A0, 0, 0, 0x3000B0, 0);
            }
            else
            {
                /* the control in the N64 labels' green, what it does brighter */
                gdl = draw_options_labels(gdl, x, y, (char *) rows[i].name, 0x00AA00B0, 0, -1, 0, 0, 0x3000B0, 0);
                gdl = draw_options_labels(gdl, x + namew + GEVR_TOUCH_NAME_GAP, y, (char *) rows[i].action, 0x00FF00B0, 0, -1, 0, 0, 0x3000B0, 0);
            }
        }
    }
    return gdl;
}
