#include <ultra64.h>
#include <memp.h>
#ifdef GEVR
#include <stdio.h>
#include <stdlib.h>
#endif
#include "sky.h"
#include "player.h"
#include "unk_092E50.h"
#include "bondview.h"
#include "lv.h"
#include "bgfog.h"
#include "bg.h"
#include "othermodemicrocode.h"
#include "fr.h"
#include "image_bank.h"
#ifdef GEVR
#include "dyn.h"
#endif

#define SKYABS(val) (val >= 0.0f ? (val) : -(val))

#ifdef GEVR
/* D227 (M-95): forward decls -- the two fan-drawing blocks that call these
 * (further down this file) come before the static definitions themselves.
 * See the comment above skyPortBeginFan()'s definition. */
static void skyPortBeginFan(SkyRelated38 *v, s32 n, bool allowShift);
static void skyPortEndFan(void);
/* D245 (M-142): same reason -- the water call site (further down) needs to
 * override the adaptive shift picked by skyPortBeginFan() before the
 * definition itself. */
static void skyPortForceFanShift(s32 kS, s32 kT);
/* D227 (M-100): same reason -- the texSelect call sites precede the definition. */
static void skyPortCaptureTile(Gfx *start, Gfx *end);
/* D227 (M-100): relocated up here (was just above skyPortPickShift, further
 * down) so the D245 water call site's GE_D245_FIXEDSHIFT clamp can use it --
 * same forward-declare reason as the functions above. */
#define SKY_TC_MAX_SHIFT 5
#endif

// bss
s32 g_SkyStageNum;

s32 dword_CODE_bss_80079E94;
Mtxf dword_CODE_bss_80079E98;
u32 dword_CODE_bss_80079ED8;



#ifdef GEVR
/*
 * Issue #21 (Statue's sky moved with your head): the environment's
 * "WaterConcavity" is really a screen-space drop for the whole sky - the
 * horizon sits that many pixels lower (Statue 30; Train, Streets, Depot 25;
 * Surface 2 20; Surface 7). A shift fixed on screen is fixed to the view, so
 * in stereo looking up, down or tilting the head dragged the sky along.
 * Stereo draws the sky where the camera maths puts it, fixed in the world.
 */
static f32 skyScreenDrop(void)
{
    extern s32 g_gevrStereo;

    return g_gevrStereo ? 0.0f : fogGetCurrentEnvironmentp()->WaterConcavity;
}
#else
#define skyScreenDrop() (fogGetCurrentEnvironmentp()->WaterConcavity)
#endif

/*
* Address: 0x7F093880
*/
void skyGetWorldPosFromScreenPos(f32 offset_x, f32 offset_y, coord3d* out) {
    Mtxf* player_mtxf;
    coord2d coords;
    f32 screen_top;

    player_mtxf = currentPlayerGetViewToWorldMtxf();
    coords.x = getPlayer_c_screenleft() + offset_x;
    screen_top = getPlayer_c_screentop();
    coords.y = skyScreenDrop() + (offset_y + screen_top);
    transformAndNormalizeByLength2Dto3D(&coords, out, 100.0f);
    mtx4RotateVecInPlace(player_mtxf, out->f);
}

/*
* Address: 0x7F0938FC
*/
bool skyIsScreenCornerInSky(coord3d *corner3dpos, coord3d *dstpos, f32 *dstfrac)
{
    coord3d *eye = bondviewGetCurrentPlayersPosition();
    f32 f12 = 2.0f * corner3dpos->y / sqrtf(corner3dpos->f[0] * corner3dpos->f[0] + corner3dpos->f[2] * corner3dpos->f[2] + 0.0001f);
    f32 sp2c;
    f32 f12_2;
    f32 sp24;
    u32 stack[2];
#ifdef DEBUG
    assert(eye[1] < fogGetCurrentEnvironmentp()->skyheight); //canonically bgFogGet()
#endif

    if (f12 > 1.0f)
    {
        f12 = 1.0f;
    }
#ifdef GEVR
    {
        /*
         * Stereo: the clouds' strength is keyed to how high each screen corner
         * points (2 tan of its elevation), which on the N64's view is full at
         * the top corners unless you look well down. The headset's corners
         * move with the head, so tilting it faded the Dam's white clouds to
         * the sky's blue and back, and looking up put a haze band on the
         * bottom edge: the sky seemed to move with the head (user, with or
         * without a texture pack). Full strength above the horizon keeps the
         * fade on the horizon itself, where the edge vertices (y = 0) give the
         * sky's colour, as the N64 shows it looking level.
         */
        extern s32 g_gevrStereo;

        if (g_gevrStereo && corner3dpos->y > 0.0f)
        {
            f12 = 1.0f;
        }
    }
#endif

    *dstfrac = 1.0f - f12;

    if (corner3dpos->y == 0.0f)
    {
        sp24 = 0.01f;
    }
    else
    {
        sp24 = corner3dpos->y;
    }

    if (sp24 > 0.0f)
    {
        sp2c = (fogGetCurrentEnvironmentp()->CloudRepeat - eye->y) / sp24;
        f12_2 = sqrtf(corner3dpos->f[0] * corner3dpos->f[0] + corner3dpos->f[2] * corner3dpos->f[2]) * sp2c;

        if (f12_2 > 300000)
        {
            sp2c *= 300000 / f12_2;
        }

        dstpos->x = eye->x + sp2c * corner3dpos->f[0];
        dstpos->y = eye->y + sp2c * sp24;
        dstpos->z = eye->z + sp2c * corner3dpos->f[2];
#ifdef GEVR
        {
            /*
             * Stereo: a point past the 300000 reach is pulled in along its
             * ray, which puts it below the cloud layer - a horizon point
             * (y = 0) at eye height. The clouds were then textured across a
             * surface sagging from the layer down to eye height at the
             * horizon, which the corners move as the head tilts: the texture
             * slid near the horizon (user; the N64's narrow view hid it).
             * Kept on the layer, capped in reach only, the texture is the
             * layer's; its edge sits ~1 degree over the horizon, and the fill
             * below reaches up to it (skyRender).
             */
            extern s32 g_gevrStereo;

            if (g_gevrStereo)
            {
                dstpos->y = fogGetCurrentEnvironmentp()->CloudRepeat;
            }
        }
#endif

        return TRUE;
    }

    return FALSE;
}


/*
* Address: 0x7F093A78
*/
bool skyIsCornerInWater(coord3d *corner3dpos, coord3d *dstpos, f32 *dstfrac)
{
    coord3d *eye = bondviewGetCurrentPlayersPosition();
    f32 f12 = -2.0f * corner3dpos->y / sqrtf(corner3dpos->f[0] * corner3dpos->f[0] + corner3dpos->f[2] * corner3dpos->f[2] + 0.0001f);
    f32 sp2c;
    f32 f12_2;
    f32 sp24;
    u32 stack[2];
#ifdef DEBUG
    assert(eye[1] < fogGetCurrentEnvironmentp()->seaheight);
#endif

    if (f12 > 1.0f)
    {
        f12 = 1.0f;
    }
#ifdef GEVR
    {
        /* stereo: as the clouds (skyIsScreenCornerInSky), the water's own
         * colour below the horizon however the head tilts */
        extern s32 g_gevrStereo;

        if (g_gevrStereo && corner3dpos->y < 0.0f)
        {
            f12 = 1.0f;
        }
    }
#endif

    *dstfrac = 1.0f - f12;

    if (corner3dpos->y == 0.0f)
    {
        sp24 = -0.01f;
    }
    else
    {
        sp24 = corner3dpos->y;
    }

    if (sp24 < 0.0f)
    {
        sp2c = (fogGetCurrentEnvironmentp()->WaterRepeat - eye->y) / sp24;
        f12_2 = sqrtf(corner3dpos->f[0] * corner3dpos->f[0] + corner3dpos->f[2] * corner3dpos->f[2]) * sp2c;

        if (f12_2 > 300000)
        {
            sp2c *= 300000 / f12_2;
        }

        dstpos->x = eye->x + sp2c * corner3dpos->f[0];
        dstpos->y = eye->y + sp2c * sp24;
        dstpos->z = eye->z + sp2c * corner3dpos->f[2];

        return TRUE;
    }

    return FALSE;
}

/*
* Address: 0x7F093BFC
*/
void skyCalculateEdgeVertex(coord3d *base, coord3d* ref, coord3d* out)
{
    f32 mult;

    mult = base->y / (base->y - ref->y);
    out->x = ((ref->x - base->x) * mult) + base->x;
    out->y = 0.0f;
    out->z = ((ref->z - base->z) * mult) + base->z;
}

f32 skyClamp(f32 a, f32 b, f32 c)
{
    if (a < b)
    {
        return b;
    }

    if (c < a)
    {
        return c;
    }

    return a;
}


f32 skyRound(f32 arg0)
{
    return (f32) (s32) (arg0 + 0.5f);
}


void skyChooseCloudVtxColour(SkyRelated18 *arg0, f32 arg1)
{
    f32 r = fogGetCurrentEnvironmentp()->Red;
    f32 g = fogGetCurrentEnvironmentp()->Green;
    f32 b = fogGetCurrentEnvironmentp()->Blue;
    u32 unused;

    arg0->r = r + fogGetCurrentEnvironmentp()->CloudRed   * (1.0f - r / 255.0f) * (1.0f - arg1);
    arg0->g = g + fogGetCurrentEnvironmentp()->CloudGreen * (1.0f - g / 255.0f) * (1.0f - arg1);
    arg0->b = b + fogGetCurrentEnvironmentp()->CloudBlue  * (1.0f - b / 255.0f) * (1.0f - arg1);

    arg0->a = 0xff;
}

/*
* Address: 0x7F093FA4
*/
void skyChooseWaterVtxColour(SkyRelated18 *arg0, f32 arg1)
{
    f32 r = fogGetCurrentEnvironmentp()->Red;
    f32 g = fogGetCurrentEnvironmentp()->Green;
    f32 b = fogGetCurrentEnvironmentp()->Blue;
    u32 unused;

    arg0->r = r + fogGetCurrentEnvironmentp()->WaterRed   * (1.0f - r / 255.0f) * (1.0f - arg1);
    arg0->g = g + fogGetCurrentEnvironmentp()->WaterGreen * (1.0f - g / 255.0f) * (1.0f - arg1);
    arg0->b = b + fogGetCurrentEnvironmentp()->WaterBlue  * (1.0f - b / 255.0f) * (1.0f - arg1);
    arg0->a = 0xff;
}

/*
* Address: 0x7F094298
* Has to do with converting a float to a 32-bits value that's later read in two 16-bits parts
* Possibly converting floats to fixed point
*/
u32 sub_GAME_7F094298(f32 arg0)
{
    u32 result;

    if (arg0 > 32767.9f) { arg0 = 32767.9f; }
    if (arg0 < -32767.9f) { arg0 = -32767.9f; }

    if (arg0 < 0)
    {
        result = arg0 * -65536;
        result = -result;
    }
    else
    {
        result = 65536 * arg0;
    }

    return result;
}


void skySetStageNum(s32 stagenum) {
  g_SkyStageNum = stagenum;
}


/*
* Address: 0x7F094438
*/
void skyTick(void)
{
    #if defined(VERSION_EU)
    g_SkyCloudOffset += g_GlobalTimerDelta;
    #else
    g_SkyCloudOffset += g_ClockTimer;
    #endif
    if ( g_SkyCloudOffset > 4096.0f)
    {
        g_SkyCloudOffset -= 4096.0f;
    }
}

Gfx* sub_GAME_7F09343C(Gfx*, s32);

Gfx *skyRender(Gfx *gdl)
{
    coord3d sp6a4;
    coord3d sp698;
    coord3d sp68c;
    coord3d sp680;
    coord3d sp674;
    coord3d sp668;
    coord3d sp65c;
    coord3d sp650;
    coord3d sp644;
    coord3d sp638;
    coord3d sp62c;
    coord3d sp620;
    coord3d sp614;
    coord3d sp608;
    coord3d sp5fc;
    coord3d sp5f0;
    coord3d sp5e4;
    coord3d sp5d8;
    coord3d sp5cc;
    coord3d sp5c0;
    coord3d sp5b4;
    coord3d sp5a8;
    coord3d sp59c;
    coord3d sp590;
    f32 sp58c;
    f32 sp588;
    f32 sp584;
    f32 sp580;
    f32 sp57c;
    f32 sp578;
    f32 sp574;
    f32 sp570;
    f32 sp56c;
    f32 sp568;
    f32 sp564;
    f32 sp560;
    f32 sp55c;
    f32 sp558;
    f32 sp554;
    f32 sp550;
    f32 sp54c;
    f32 sp548;
    s32 s1;
    s32 j;
    s32 k;
    s32 sp538;
    s32 sp534;
    s32 sp530;
    s32 sp52c;
    SkyRelated18 sp4b4[5];
    SkyRelated18 sp43c[5];
    f32 tmp;
    f32 scale;
    bool sp430;
    struct CurrentEnvironmentRecord *env;

    scale = get_room_data_float1() / 30.0f;
    sp430 = FALSE;
    env = fogGetCurrentEnvironmentp();

#ifdef GEVR
    if (getenv("GE_D176")) {
        static int n = 0;
        if (n++ < 4)
            fprintf(stderr, "D176 sky: Clouds=%d RGB=%d,%d,%d SkyImageId=%d CloudRGB=%.1f,%.1f,%.1f "
                    "CloudRepeat=%.2f players=%d IsWater=%d "
                    /* D229: the water quad's shade comes from these. */
                    "WaterRGB=%.1f,%.1f,%.1f WaterImageId=%d WaterRepeat=%.2f WaterConcavity=%.1f\n",
                    env->Clouds, env->Red, env->Green, env->Blue, env->SkyImageId,
                    (double)env->CloudRed, (double)env->CloudGreen, (double)env->CloudBlue,
                    (double)env->CloudRepeat, getPlayerCount(), env->IsWater,
                    (double)env->WaterRed, (double)env->WaterGreen, (double)env->WaterBlue,
                    env->WaterImageId, (double)env->WaterRepeat,
                    (double)env->WaterConcavity);
    }
#endif

    if (!fogGetCurrentEnvironmentp()->Clouds)
    {
        if (getPlayerCount() == 1)
        {
            gDPSetCycleType(gdl++, G_CYC_FILL);

            gdl = viSetFillColor(gdl, env->Red, env->Green, env->Blue);

            gDPFillRectangle(gdl++, viGetViewLeft(), viGetViewTop(),
                    viGetViewLeft() + viGetViewWidth() - 1,
                    viGetViewTop() + viGetViewHeight() - 1);

            gDPPipeSync(gdl++);
            return gdl;
        }

        gDPPipeSync(gdl++);
        gDPSetCycleType(gdl++, G_CYC_FILL);

        gDPSetRenderMode(gdl++, G_RM_NOOP, G_RM_NOOP2);

        gDPFillRectangle(gdl++,
                g_CurrentPlayer->viewleft, g_CurrentPlayer->viewtop,
                (g_CurrentPlayer->viewleft + g_CurrentPlayer->viewx) - 1,
                (g_CurrentPlayer->viewtop + g_CurrentPlayer->viewy) - 1);

        gDPPipeSync(gdl++);
        return gdl;
    }

    gdl = viSetFillColor(gdl, env->Red, env->Green, env->Blue);

    if (&sp6a4);

    skyGetWorldPosFromScreenPos(0.0f, 0.0f, &sp6a4);
    skyGetWorldPosFromScreenPos(getPlayer_c_screenwidth() - 0.1f, 0.0f, &sp698);
    skyGetWorldPosFromScreenPos(0.0f, getPlayer_c_screenheight() - 0.1f, &sp68c);
    skyGetWorldPosFromScreenPos(getPlayer_c_screenwidth() - 0.1f, getPlayer_c_screenheight() - 0.1f, &sp680);

    sp538 = skyIsScreenCornerInSky(&sp6a4, &sp644, &sp58c);
    sp534 = skyIsScreenCornerInSky(&sp698, &sp638, &sp588);
    sp530 = skyIsScreenCornerInSky(&sp68c, &sp62c, &sp584);
    sp52c = skyIsScreenCornerInSky(&sp680, &sp620, &sp580);

#ifdef GEVR
    if (getenv("GE_D176")) {
        static int m = 0;
        if (m++ < 3) {
            coord3d *eye = bondviewGetCurrentPlayersPosition();
            fprintf(stderr, "D176 corners: WaterConcavity=%.3f eye=(%.1f,%.1f,%.1f)\n"
                "  c0 ray=(%.3f,%.3f,%.3f) inSky=%d\n  c1 ray=(%.3f,%.3f,%.3f) inSky=%d\n"
                "  c2 ray=(%.3f,%.3f,%.3f) inSky=%d\n  c3 ray=(%.3f,%.3f,%.3f) inSky=%d\n",
                (double)fogGetCurrentEnvironmentp()->WaterConcavity,
                (double)eye->x,(double)eye->y,(double)eye->z,
                (double)sp6a4.f[0],(double)sp6a4.f[1],(double)sp6a4.f[2], sp538,
                (double)sp698.f[0],(double)sp698.f[1],(double)sp698.f[2], sp534,
                (double)sp68c.f[0],(double)sp68c.f[1],(double)sp68c.f[2], sp530,
                (double)sp680.f[0],(double)sp680.f[1],(double)sp680.f[2], sp52c);
        }
    }
#endif

    skyIsCornerInWater(&sp6a4, &sp5e4, &sp56c);
    skyIsCornerInWater(&sp698, &sp5d8, &sp568);
    skyIsCornerInWater(&sp68c, &sp5cc, &sp564);
    skyIsCornerInWater(&sp680, &sp5c0, &sp560);

    if (sp538 != sp530)
    {
        sp54c = getPlayer_c_screentop() + getPlayer_c_screenheight() * (sp6a4.f[1] / (sp6a4.f[1] - sp68c.f[1]));

        skyGetWorldPosFromScreenPos(0.0f, sp54c, &sp65c);
        skyCalculateEdgeVertex(&sp6a4, &sp68c, &sp65c);
        skyIsScreenCornerInSky(&sp65c, &sp5fc, &sp574);
        skyIsCornerInWater(&sp65c, &sp59c, &sp554);
    }
    else
    {
        sp54c = 0.0f;
    }

    if (sp534 != sp52c)
    {
        sp548 = getPlayer_c_screentop() + getPlayer_c_screenheight() * (sp698.f[1] / (sp698.f[1] - sp680.f[1]));

        skyGetWorldPosFromScreenPos(getPlayer_c_screenwidth() - 0.1f, sp548, &sp650);
        skyCalculateEdgeVertex(&sp698, &sp680, &sp650);
        skyIsScreenCornerInSky(&sp650, &sp5f0, &sp570);
        skyIsCornerInWater(&sp650, &sp590, &sp550);
    }
    else
    {
        sp548 = 0.0f;
    }

    if (sp538 != sp534)
    {
        skyGetWorldPosFromScreenPos(getPlayer_c_screenleft() + getPlayer_c_screenwidth() * (sp6a4.f[1] / (sp6a4.f[1] - sp698.f[1])), 0.0f, &sp674);
        skyCalculateEdgeVertex(&sp6a4, &sp698, &sp674);
        skyIsScreenCornerInSky(&sp674, &sp614, &sp57c);
        skyIsCornerInWater(&sp674, &sp5b4, &sp55c);
    }

    if (sp530 != sp52c)
    {
        tmp = getPlayer_c_screenleft() + getPlayer_c_screenwidth() * (sp68c.f[1] / (sp68c.f[1] - sp680.f[1]));

        skyGetWorldPosFromScreenPos(tmp, getPlayer_c_screenheight() - 0.1f, &sp668);
        skyCalculateEdgeVertex(&sp68c, &sp680, &sp668);
        skyIsScreenCornerInSky(&sp668, &sp608, &sp578);
        skyIsCornerInWater(&sp668, &sp5a8, &sp558);
    }

    switch ((sp538 << 3) | (sp534 << 2) | (sp530 << 1) | sp52c)
    {
        case 15:
            s1 = 0;
            break;

        case 0:
            s1 = 4;
            sp43c[0].unk00 = sp5e4.f[0] * scale;
            sp43c[0].unk04 = sp5e4.f[1] * scale;
            sp43c[0].unk08 = sp5e4.f[2] * scale;
            sp43c[1].unk00 = sp5d8.f[0] * scale;
            sp43c[1].unk04 = sp5d8.f[1] * scale;
            sp43c[1].unk08 = sp5d8.f[2] * scale;
            sp43c[2].unk00 = sp5cc.f[0] * scale;
            sp43c[2].unk04 = sp5cc.f[1] * scale;
            sp43c[2].unk08 = sp5cc.f[2] * scale;
            sp43c[3].unk00 = sp5c0.f[0] * scale;
            sp43c[3].unk04 = sp5c0.f[1] * scale;
            sp43c[3].unk08 = sp5c0.f[2] * scale;
            sp43c[0].unk0c = sp5e4.f[0];
            sp43c[0].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5d8.f[0];
            sp43c[1].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5cc.f[0];
            sp43c[2].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp5c0.f[0];
            sp43c[3].unk10 = sp5c0.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp56c);
            skyChooseWaterVtxColour(&sp43c[1], sp568);
            skyChooseWaterVtxColour(&sp43c[2], sp564);
            skyChooseWaterVtxColour(&sp43c[3], sp560);
            break;

        case 3:
            s1 = 4;
            sp43c[0].unk00 = sp5e4.f[0] * scale;
            sp43c[0].unk04 = sp5e4.f[1] * scale;
            sp43c[0].unk08 = sp5e4.f[2] * scale;
            sp43c[1].unk00 = sp5d8.f[0] * scale;
            sp43c[1].unk04 = sp5d8.f[1] * scale;
            sp43c[1].unk08 = sp5d8.f[2] * scale;
            sp43c[2].unk00 = sp59c.f[0] * scale;
            sp43c[2].unk04 = sp59c.f[1] * scale;
            sp43c[2].unk08 = sp59c.f[2] * scale;
            sp43c[3].unk00 = sp590.f[0] * scale;
            sp43c[3].unk04 = sp590.f[1] * scale;
            sp43c[3].unk08 = sp590.f[2] * scale;
            sp43c[0].unk0c = sp5e4.f[0];
            sp43c[0].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5d8.f[0];
            sp43c[1].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp59c.f[0];
            sp43c[2].unk10 = sp59c.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp590.f[0];
            sp43c[3].unk10 = sp590.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp56c);
            skyChooseWaterVtxColour(&sp43c[1], sp568);
            skyChooseWaterVtxColour(&sp43c[2], sp554);
            skyChooseWaterVtxColour(&sp43c[3], sp550);
            break;

        case 12:
            s1 = 4;
            sp430 = TRUE;
            sp43c[0].unk00 = sp5c0.f[0] * scale;
            sp43c[0].unk04 = sp5c0.f[1] * scale;
            sp43c[0].unk08 = sp5c0.f[2] * scale;
            sp43c[1].unk00 = sp5cc.f[0] * scale;
            sp43c[1].unk04 = sp5cc.f[1] * scale;
            sp43c[1].unk08 = sp5cc.f[2] * scale;
            sp43c[2].unk00 = sp590.f[0] * scale;
            sp43c[2].unk04 = sp590.f[1] * scale;
            sp43c[2].unk08 = sp590.f[2] * scale;
            sp43c[3].unk00 = sp59c.f[0] * scale;
            sp43c[3].unk04 = sp59c.f[1] * scale;
            sp43c[3].unk08 = sp59c.f[2] * scale;
            sp43c[0].unk0c = sp5c0.f[0];
            sp43c[0].unk10 = sp5c0.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5cc.f[0];
            sp43c[1].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp590.f[0];
            sp43c[2].unk10 = sp590.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp59c.f[0];
            sp43c[3].unk10 = sp59c.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp560);
            skyChooseWaterVtxColour(&sp43c[1], sp564);
            skyChooseWaterVtxColour(&sp43c[2], sp550);
            skyChooseWaterVtxColour(&sp43c[3], sp554);
            break;

        case 10:
            s1 = 4;
            sp43c[0].unk00 = sp5d8.f[0] * scale;
            sp43c[0].unk04 = sp5d8.f[1] * scale;
            sp43c[0].unk08 = sp5d8.f[2] * scale;
            sp43c[1].unk00 = sp5c0.f[0] * scale;
            sp43c[1].unk04 = sp5c0.f[1] * scale;
            sp43c[1].unk08 = sp5c0.f[2] * scale;
            sp43c[2].unk00 = sp5b4.f[0] * scale;
            sp43c[2].unk04 = sp5b4.f[1] * scale;
            sp43c[2].unk08 = sp5b4.f[2] * scale;
            sp43c[3].unk00 = sp5a8.f[0] * scale;
            sp43c[3].unk04 = sp5a8.f[1] * scale;
            sp43c[3].unk08 = sp5a8.f[2] * scale;
            sp43c[0].unk0c = sp5d8.f[0];
            sp43c[0].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5c0.f[0];
            sp43c[1].unk10 = sp5c0.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5b4.f[0];
            sp43c[2].unk10 = sp5b4.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp5a8.f[0];
            sp43c[3].unk10 = sp5a8.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp568);
            skyChooseWaterVtxColour(&sp43c[1], sp560);
            skyChooseWaterVtxColour(&sp43c[2], sp55c);
            skyChooseWaterVtxColour(&sp43c[3], sp558);
            break;

        case 5:
            s1 = 4;
            sp43c[0].unk00 = sp5cc.f[0] * scale;
            sp43c[0].unk04 = sp5cc.f[1] * scale;
            sp43c[0].unk08 = sp5cc.f[2] * scale;
            sp43c[1].unk00 = sp5e4.f[0] * scale;
            sp43c[1].unk04 = sp5e4.f[1] * scale;
            sp43c[1].unk08 = sp5e4.f[2] * scale;
            sp43c[2].unk00 = sp5a8.f[0] * scale;
            sp43c[2].unk04 = sp5a8.f[1] * scale;
            sp43c[2].unk08 = sp5a8.f[2] * scale;
            sp43c[3].unk00 = sp5b4.f[0] * scale;
            sp43c[3].unk04 = sp5b4.f[1] * scale;
            sp43c[3].unk08 = sp5b4.f[2] * scale;
            sp43c[0].unk0c = sp5cc.f[0];
            sp43c[0].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5e4.f[0];
            sp43c[1].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5a8.f[0];
            sp43c[2].unk10 = sp5a8.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp5b4.f[0];
            sp43c[3].unk10 = sp5b4.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp564);
            skyChooseWaterVtxColour(&sp43c[1], sp56c);
            skyChooseWaterVtxColour(&sp43c[2], sp558);
            skyChooseWaterVtxColour(&sp43c[3], sp55c);
            break;

        case 14:
            s1 = 3;
            sp43c[0].unk00 = sp5c0.f[0] * scale;
            sp43c[0].unk04 = sp5c0.f[1] * scale;
            sp43c[0].unk08 = sp5c0.f[2] * scale;
            sp43c[1].unk00 = sp5a8.f[0] * scale;
            sp43c[1].unk04 = sp5a8.f[1] * scale;
            sp43c[1].unk08 = sp5a8.f[2] * scale;
            sp43c[2].unk00 = sp590.f[0] * scale;
            sp43c[2].unk04 = sp590.f[1] * scale;
            sp43c[2].unk08 = sp590.f[2] * scale;
            sp43c[0].unk0c = sp5c0.f[0];
            sp43c[0].unk10 = sp5c0.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5a8.f[0];
            sp43c[1].unk10 = sp5a8.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp590.f[0];
            sp43c[2].unk10 = sp590.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp560);
            skyChooseWaterVtxColour(&sp43c[1], sp558);
            skyChooseWaterVtxColour(&sp43c[2], sp550);
            break;

        case 13:
            s1 = 3;
            sp43c[0].unk00 = sp5cc.f[0] * scale;
            sp43c[0].unk04 = sp5cc.f[1] * scale;
            sp43c[0].unk08 = sp5cc.f[2] * scale;
            sp43c[1].unk00 = sp59c.f[0] * scale;
            sp43c[1].unk04 = sp59c.f[1] * scale;
            sp43c[1].unk08 = sp59c.f[2] * scale;
            sp43c[2].unk00 = sp5a8.f[0] * scale;
            sp43c[2].unk04 = sp5a8.f[1] * scale;
            sp43c[2].unk08 = sp5a8.f[2] * scale;
            sp43c[0].unk0c = sp5cc.f[0];
            sp43c[0].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp59c.f[0];
            sp43c[1].unk10 = sp59c.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5a8.f[0];
            sp43c[2].unk10 = sp5a8.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp564);
            skyChooseWaterVtxColour(&sp43c[1], sp554);
            skyChooseWaterVtxColour(&sp43c[2], sp558);
            break;

        case 11:
            s1 = 3;
            sp43c[0].unk00 = sp5d8.f[0] * scale;
            sp43c[0].unk04 = sp5d8.f[1] * scale;
            sp43c[0].unk08 = sp5d8.f[2] * scale;
            sp43c[1].unk00 = sp590.f[0] * scale;
            sp43c[1].unk04 = sp590.f[1] * scale;
            sp43c[1].unk08 = sp590.f[2] * scale;
            sp43c[2].unk00 = sp5b4.f[0] * scale;
            sp43c[2].unk04 = sp5b4.f[1] * scale;
            sp43c[2].unk08 = sp5b4.f[2] * scale;
            sp43c[0].unk0c = sp5d8.f[0];
            sp43c[0].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp590.f[0];
            sp43c[1].unk10 = sp590.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5b4.f[0];
            sp43c[2].unk10 = sp5b4.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp568);
            skyChooseWaterVtxColour(&sp43c[1], sp550);
            skyChooseWaterVtxColour(&sp43c[2], sp55c);
            break;

        case 7:
            s1 = 3;
            sp43c[0].unk00 = sp5e4.f[0] * scale;
            sp43c[0].unk04 = sp5e4.f[1] * scale;
            sp43c[0].unk08 = sp5e4.f[2] * scale;
            sp43c[1].unk00 = sp5b4.f[0] * scale;
            sp43c[1].unk04 = sp5b4.f[1] * scale;
            sp43c[1].unk08 = sp5b4.f[2] * scale;
            sp43c[2].unk00 = sp59c.f[0] * scale;
            sp43c[2].unk04 = sp59c.f[1] * scale;
            sp43c[2].unk08 = sp59c.f[2] * scale;
            sp43c[0].unk0c = sp5e4.f[0];
            sp43c[0].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5b4.f[0];
            sp43c[1].unk10 = sp5b4.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp59c.f[0];
            sp43c[2].unk10 = sp59c.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp56c);
            skyChooseWaterVtxColour(&sp43c[1], sp55c);
            skyChooseWaterVtxColour(&sp43c[2], sp554);
            break;

        case 1:
            s1 = 5;
            sp43c[0].unk00 = sp5cc.f[0] * scale;
            sp43c[0].unk04 = sp5cc.f[1] * scale;
            sp43c[0].unk08 = sp5cc.f[2] * scale;
            sp43c[1].unk00 = sp5e4.f[0] * scale;
            sp43c[1].unk04 = sp5e4.f[1] * scale;
            sp43c[1].unk08 = sp5e4.f[2] * scale;
            sp43c[2].unk00 = sp5d8.f[0] * scale;
            sp43c[2].unk04 = sp5d8.f[1] * scale;
            sp43c[2].unk08 = sp5d8.f[2] * scale;
            sp43c[3].unk00 = sp590.f[0] * scale;
            sp43c[3].unk04 = sp590.f[1] * scale;
            sp43c[3].unk08 = sp590.f[2] * scale;
            sp43c[4].unk00 = sp5a8.f[0] * scale;
            sp43c[4].unk04 = sp5a8.f[1] * scale;
            sp43c[4].unk08 = sp5a8.f[2] * scale;
            sp43c[0].unk0c = sp5cc.f[0];
            sp43c[0].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5e4.f[0];
            sp43c[1].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5d8.f[0];
            sp43c[2].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp590.f[0];
            sp43c[3].unk10 = sp590.f[2] + g_SkyCloudOffset;
            sp43c[4].unk0c = sp5a8.f[0];
            sp43c[4].unk10 = sp5a8.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp564);
            skyChooseWaterVtxColour(&sp43c[1], sp56c);
            skyChooseWaterVtxColour(&sp43c[2], sp568);
            skyChooseWaterVtxColour(&sp43c[3], sp550);
            skyChooseWaterVtxColour(&sp43c[4], sp558);
            break;

        case 2:
            s1 = 5;
            sp43c[0].unk00 = sp5e4.f[0] * scale;
            sp43c[0].unk04 = sp5e4.f[1] * scale;
            sp43c[0].unk08 = sp5e4.f[2] * scale;
            sp43c[1].unk00 = sp5d8.f[0] * scale;
            sp43c[1].unk04 = sp5d8.f[1] * scale;
            sp43c[1].unk08 = sp5d8.f[2] * scale;
            sp43c[2].unk00 = sp5c0.f[0] * scale;
            sp43c[2].unk04 = sp5c0.f[1] * scale;
            sp43c[2].unk08 = sp5c0.f[2] * scale;
            sp43c[3].unk00 = sp5a8.f[0] * scale;
            sp43c[3].unk04 = sp5a8.f[1] * scale;
            sp43c[3].unk08 = sp5a8.f[2] * scale;
            sp43c[4].unk00 = sp59c.f[0] * scale;
            sp43c[4].unk04 = sp59c.f[1] * scale;
            sp43c[4].unk08 = sp59c.f[2] * scale;
            sp43c[0].unk0c = sp5e4.f[0];
            sp43c[0].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5d8.f[0];
            sp43c[1].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5c0.f[0];
            sp43c[2].unk10 = sp5c0.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp5a8.f[0];
            sp43c[3].unk10 = sp5a8.f[2] + g_SkyCloudOffset;
            sp43c[4].unk0c = sp59c.f[0];
            sp43c[4].unk10 = sp59c.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp56c);
            skyChooseWaterVtxColour(&sp43c[1], sp568);
            skyChooseWaterVtxColour(&sp43c[2], sp560);
            skyChooseWaterVtxColour(&sp43c[3], sp558);
            skyChooseWaterVtxColour(&sp43c[4], sp554);
            break;

        case 4:
            s1 = 5;
            sp43c[0].unk00 = sp5c0.f[0] * scale;
            sp43c[0].unk04 = sp5c0.f[1] * scale;
            sp43c[0].unk08 = sp5c0.f[2] * scale;
            sp43c[1].unk00 = sp5cc.f[0] * scale;
            sp43c[1].unk04 = sp5cc.f[1] * scale;
            sp43c[1].unk08 = sp5cc.f[2] * scale;
            sp43c[2].unk00 = sp5e4.f[0] * scale;
            sp43c[2].unk04 = sp5e4.f[1] * scale;
            sp43c[2].unk08 = sp5e4.f[2] * scale;
            sp43c[3].unk00 = sp5b4.f[0] * scale;
            sp43c[3].unk04 = sp5b4.f[1] * scale;
            sp43c[3].unk08 = sp5b4.f[2] * scale;
            sp43c[4].unk00 = sp590.f[0] * scale;
            sp43c[4].unk04 = sp590.f[1] * scale;
            sp43c[4].unk08 = sp590.f[2] * scale;
            sp43c[0].unk0c = sp5c0.f[0];
            sp43c[0].unk10 = sp5c0.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5cc.f[0];
            sp43c[1].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5e4.f[0];
            sp43c[2].unk10 = sp5e4.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp5b4.f[0];
            sp43c[3].unk10 = sp5b4.f[2] + g_SkyCloudOffset;
            sp43c[4].unk0c = sp590.f[0];
            sp43c[4].unk10 = sp590.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp560);
            skyChooseWaterVtxColour(&sp43c[1], sp564);
            skyChooseWaterVtxColour(&sp43c[2], sp56c);
            skyChooseWaterVtxColour(&sp43c[3], sp55c);
            skyChooseWaterVtxColour(&sp43c[4], sp550);
            break;

        case 8:
            s1 = 5;
            sp43c[0].unk00 = sp5d8.f[0] * scale;
            sp43c[0].unk04 = sp5d8.f[1] * scale;
            sp43c[0].unk08 = sp5d8.f[2] * scale;
            sp43c[1].unk00 = sp5c0.f[0] * scale;
            sp43c[1].unk04 = sp5c0.f[1] * scale;
            sp43c[1].unk08 = sp5c0.f[2] * scale;
            sp43c[2].unk00 = sp5cc.f[0] * scale;
            sp43c[2].unk04 = sp5cc.f[1] * scale;
            sp43c[2].unk08 = sp5cc.f[2] * scale;
            sp43c[3].unk00 = sp59c.f[0] * scale;
            sp43c[3].unk04 = sp59c.f[1] * scale;
            sp43c[3].unk08 = sp59c.f[2] * scale;
            sp43c[4].unk00 = sp5b4.f[0] * scale;
            sp43c[4].unk04 = sp5b4.f[1] * scale;
            sp43c[4].unk08 = sp5b4.f[2] * scale;
            sp43c[0].unk0c = sp5d8.f[0];
            sp43c[0].unk10 = sp5d8.f[2] + g_SkyCloudOffset;
            sp43c[1].unk0c = sp5c0.f[0];
            sp43c[1].unk10 = sp5c0.f[2] + g_SkyCloudOffset;
            sp43c[2].unk0c = sp5cc.f[0];
            sp43c[2].unk10 = sp5cc.f[2] + g_SkyCloudOffset;
            sp43c[3].unk0c = sp59c.f[0];
            sp43c[3].unk10 = sp59c.f[2] + g_SkyCloudOffset;
            sp43c[4].unk0c = sp5b4.f[0];
            sp43c[4].unk10 = sp5b4.f[2] + g_SkyCloudOffset;

            skyChooseWaterVtxColour(&sp43c[0], sp568);
            skyChooseWaterVtxColour(&sp43c[1], sp560);
            skyChooseWaterVtxColour(&sp43c[2], sp564);
            skyChooseWaterVtxColour(&sp43c[3], sp554);
            skyChooseWaterVtxColour(&sp43c[4], sp55c);
            break;

        default:
            return gdl;
    }

    if (s1 > 0)
    {
        Mtxf sp3cc;
        Mtxf sp38c;
        SkyRelated38 sp274[5];
        s32 i;
        s32 unused[3];

        matrix_4x4_multiply(currentPlayerGetProjectionMatrixF(), camGetWorldToScreenMtxf(), &sp3cc);
        guScaleF(dword_CODE_bss_80079E98.m, 1.0f / scale, 1.0f / scale, 1.0f / scale);
        matrix_4x4_multiply(&sp3cc, &dword_CODE_bss_80079E98, &sp38c);

        for (i = 0; i < s1; i++)
        {
            sub_GAME_7F097388(&sp43c[i], &sp38c, 130, 65535.0f, 65535.0f, &sp274[i]);

            sp274[i].unk28 = skyClamp(sp274[i].unk28, getPlayer_c_screenleft() * 4.0f, (getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f - 1.0f);
            sp274[i].unk2c = skyClamp(sp274[i].unk2c, getPlayer_c_screentop() * 4.0f, (getPlayer_c_screentop() + getPlayer_c_screenheight()) * 4.0f - 1.0f);

            if (sp274[i].unk2c > getPlayer_c_screentop() * 4.0f + 4.0f
                    && sp274[i].unk2c < (getPlayer_c_screentop() + getPlayer_c_screenheight()) * 4.0f - 4.0f)
            {
                sp274[i].unk2c -= 4.0f;
            }
        }

        if (!fogGetCurrentEnvironmentp()->IsWater)
        {
            f32 f14 = 1279.0f;
            f32 f2 = 0.0f;
            f32 f16 = 959.0f;
            f32 f12 = 0.0f;

            for (j = 0; j < s1; j++)
            {
                if (sp274[j].unk28 < f14) { f14 = sp274[j].unk28; }
                if (sp274[j].unk28 > f2) { f2 = sp274[j].unk28; }

                if (sp274[j].unk2c < f16) { f16 = sp274[j].unk2c; }
                if (sp274[j].unk2c > f12) { f12 = sp274[j].unk2c; }
            }

#ifdef GEVR
            {
                /* stereo: up to the cloud layer's edge, ~1 degree over the
                 * horizon (skyIsScreenCornerInSky); the clouds draw over the rest */
                extern s32 g_gevrStereo;

                if (g_gevrStereo)
                {
                    f16 -= 16.0f;
                    if (f16 < getPlayer_c_screentop() * 4.0f)
                    {
                        f16 = getPlayer_c_screentop() * 4.0f;
                    }
                }
            }
#endif
            gDPPipeSync(gdl++);
            gDPSetCycleType(gdl++, G_CYC_FILL);
            gDPSetRenderMode(gdl++, G_RM_NOOP, G_RM_NOOP2);
            gDPSetTexturePersp(gdl++, G_TP_NONE);
            gDPFillRectangle(gdl++, (s32)(f14 * 0.25f), (s32)(f16 * 0.25f), (s32)(f2 * 0.25f), (s32)(f12 * 0.25f));
            gDPPipeSync(gdl++);
            gDPSetTexturePersp(gdl++, G_TP_PERSP);
        }
        else
        {
            gDPPipeSync(gdl++);

#ifdef GEVR
            /* D227 (M-100): remember the tiles this draw sets up so
             * skyPortEmitTileShift() can replay them with a shift. Captured
             * AFTER sub_GAME_7F09343C, which re-declares both tiles -- see the
             * comment above skyPortCaptureTile(). */
            {
                Gfx *skyTileFrom = gdl;
                texSelect(&gdl, &skywaterimages[fogGetCurrentEnvironmentp()->WaterImageId], 1, 0, 2);
                gdl = sub_GAME_7F09343C(gdl, 0); // ???
                skyPortCaptureTile(skyTileFrom, gdl);
            }
#else
            texSelect(&gdl, &skywaterimages[fogGetCurrentEnvironmentp()->WaterImageId], 1, 0, 2);
            gdl = sub_GAME_7F09343C(gdl, 0); // ???
#endif
            gDPSetRenderMode(gdl++, G_RM_OPA_SURF, G_RM_OPA_SURF2);

#ifdef GEVR
            /* D227 (M-95): one shared wScale for every triangle drawn from
             * this sp274[] fan -- see skyPortBeginFan() above. D245: allow
             * the tc-shift safety valve on the water quad too -- see the
             * comment above skyPortBeginFan() for why this doesn't flatten
             * the cross-fade. */
            skyPortBeginFan(sp274, s1, TRUE); /* water: see skyPortBeginFan */

            /* D245 (M-142): opt-in A/B candidate -- see skyPortForceFanShift's
             * comment. Off by default (unset env => identical to the current
             * allowShift=TRUE behaviour this row is still reopened against). */
            {
                const char *fs = getenv("GE_D245_FIXEDSHIFT");
                if (fs) {
                    s32 kFixed = atoi(fs);
                    if (kFixed < 0) kFixed = 0;
                    if (kFixed > SKY_TC_MAX_SHIFT) kFixed = SKY_TC_MAX_SHIFT;
                    skyPortForceFanShift(kFixed, kFixed);
                }
            }

            /* D245 (M-142, reopened): dumps the water quad's raw S/T span
             * every call, so a scripted multi-frame capture can show whether
             * the span crosses skyPortPickShift's ~30719-unit threshold (and
             * therefore the tc-shift k) as the camera rotates -- the
             * still-open question after allowShift=TRUE didn't fix the live
             * symptom. Recomputed locally rather than reading skyPortBeginFan's
             * statics (defined later in this TU, not forward-declared). */
            if (getenv("GE_D245V")) {
                static int callN = 0;
                f32 minS = sp274[0].unk20, maxS = sp274[0].unk20;
                f32 minT = sp274[0].unk24, maxT = sp274[0].unk24;
                s32 vi;
                for (vi = 1; vi < s1; vi++) {
                    if (sp274[vi].unk20 < minS) minS = sp274[vi].unk20;
                    if (sp274[vi].unk20 > maxS) maxS = sp274[vi].unk20;
                    if (sp274[vi].unk24 < minT) minT = sp274[vi].unk24;
                    if (sp274[vi].unk24 > maxT) maxT = sp274[vi].unk24;
                }
                fprintf(stderr, "D245V call=%d s1=%d minS=%.1f maxS=%.1f minT=%.1f maxT=%.1f "
                        "spanS=%.1f spanT=%.1f\n", callN++, s1,
                        (double) minS, (double) maxS, (double) minT, (double) maxT,
                        (double) (maxS - minS), (double) (maxT - minT));
            }
#endif
            if (s1 == 4)
            {
                gdl = skyRenderTri(gdl, &sp274[0], &sp274[1], &sp274[3], 130.0f, TRUE);

                if (sp430)
                {
                    sp274[0].unk2c++;
                    sp274[1].unk2c++;
                    sp274[2].unk2c++;
                    sp274[3].unk2c++;
                }

                gdl = skyRenderTri(gdl, &sp274[3], &sp274[2], &sp274[0], 130.0f, TRUE);
            }
            else if (s1 == 5)
            {
                gdl = skyRenderTri(gdl, &sp274[0], &sp274[1], &sp274[2], 130.0f, TRUE);
                gdl = skyRenderTri(gdl, &sp274[0], &sp274[2], &sp274[3], 130.0f, TRUE);
                gdl = skyRenderTri(gdl, &sp274[0], &sp274[3], &sp274[4], 130.0f, TRUE);
            }
            else if (s1 == 3)
            {
                gdl = skyRenderTri(gdl, &sp274[0], &sp274[1], &sp274[2], 130.0f, TRUE);
            }
#ifdef GEVR
            skyPortEndFan();
#endif
        }
    }

    switch ((sp538 << 3) | (sp534 << 2) | (sp530 << 1) | sp52c)
    {
        case 0:
            return gdl;

        case 15:
            s1 = 4;
            sp4b4[0].unk00 = sp644.f[0] * scale;
            sp4b4[0].unk04 = sp644.f[1] * scale;
            sp4b4[0].unk08 = sp644.f[2] * scale;
            sp4b4[1].unk00 = sp638.f[0] * scale;
            sp4b4[1].unk04 = sp638.f[1] * scale;
            sp4b4[1].unk08 = sp638.f[2] * scale;
            sp4b4[2].unk00 = sp62c.f[0] * scale;
            sp4b4[2].unk04 = sp62c.f[1] * scale;
            sp4b4[2].unk08 = sp62c.f[2] * scale;
            sp4b4[3].unk00 = sp620.f[0] * scale;
            sp4b4[3].unk04 = sp620.f[1] * scale;
            sp4b4[3].unk08 = sp620.f[2] * scale;
            sp4b4[0].unk0c = sp644.f[0] * 0.1f;
            sp4b4[0].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp638.f[0] * 0.1f;
            sp4b4[1].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[2].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp620.f[0] * 0.1f;
            sp4b4[3].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp58c);
            skyChooseCloudVtxColour(&sp4b4[1], sp588);
            skyChooseCloudVtxColour(&sp4b4[2], sp584);
            skyChooseCloudVtxColour(&sp4b4[3], sp580);
            break;

        case 12:
            s1 = 4;
            sp4b4[0].unk00 = sp644.f[0] * scale;
            sp4b4[0].unk04 = sp644.f[1] * scale;
            sp4b4[0].unk08 = sp644.f[2] * scale;
            sp4b4[1].unk00 = sp638.f[0] * scale;
            sp4b4[1].unk04 = sp638.f[1] * scale;
            sp4b4[1].unk08 = sp638.f[2] * scale;
            sp4b4[2].unk00 = sp5fc.f[0] * scale;
            sp4b4[2].unk04 = sp5fc.f[1] * scale;
            sp4b4[2].unk08 = sp5fc.f[2] * scale;
            sp4b4[3].unk00 = sp5f0.f[0] * scale;
            sp4b4[3].unk04 = sp5f0.f[1] * scale;
            sp4b4[3].unk08 = sp5f0.f[2] * scale;
            sp4b4[0].unk0c = sp644.f[0] * 0.1f;
            sp4b4[0].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp638.f[0] * 0.1f;
            sp4b4[1].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp5fc.f[0] * 0.1f;
            sp4b4[2].unk10 = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp5f0.f[0] * 0.1f;
            sp4b4[3].unk10 = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp58c);
            skyChooseCloudVtxColour(&sp4b4[1], sp588);
            skyChooseCloudVtxColour(&sp4b4[2], sp574);
            skyChooseCloudVtxColour(&sp4b4[3], sp570);
            break;

        case 3:
            s1 = 4;
            sp4b4[0].unk00 = sp620.f[0] * scale;
            sp4b4[0].unk04 = sp620.f[1] * scale;
            sp4b4[0].unk08 = sp620.f[2] * scale;
            sp4b4[1].unk00 = sp62c.f[0] * scale;
            sp4b4[1].unk04 = sp62c.f[1] * scale;
            sp4b4[1].unk08 = sp62c.f[2] * scale;
            sp4b4[2].unk00 = sp5f0.f[0] * scale;
            sp4b4[2].unk04 = sp5f0.f[1] * scale;
            sp4b4[2].unk08 = sp5f0.f[2] * scale;
            sp4b4[3].unk00 = sp5fc.f[0] * scale;
            sp4b4[3].unk04 = sp5fc.f[1] * scale;
            sp4b4[3].unk08 = sp5fc.f[2] * scale;
            sp4b4[0].unk0c = sp620.f[0] * 0.1f;
            sp4b4[0].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[1].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp5f0.f[0] * 0.1f;
            sp4b4[2].unk10 = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp5fc.f[0] * 0.1f;
            sp4b4[3].unk10 = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp580);
            skyChooseCloudVtxColour(&sp4b4[1], sp584);
            skyChooseCloudVtxColour(&sp4b4[2], sp570);
            skyChooseCloudVtxColour(&sp4b4[3], sp574);
            break;

        case 5:
            s1 = 4;
            sp4b4[0].unk00 = sp638.f[0] * scale;
            sp4b4[0].unk04 = sp638.f[1] * scale;
            sp4b4[0].unk08 = sp638.f[2] * scale;
            sp4b4[1].unk00 = sp620.f[0] * scale;
            sp4b4[1].unk04 = sp620.f[1] * scale;
            sp4b4[1].unk08 = sp620.f[2] * scale;
            sp4b4[2].unk00 = sp614.f[0] * scale;
            sp4b4[2].unk04 = sp614.f[1] * scale;
            sp4b4[2].unk08 = sp614.f[2] * scale;
            sp4b4[3].unk00 = sp608.f[0] * scale;
            sp4b4[3].unk04 = sp608.f[1] * scale;
            sp4b4[3].unk08 = sp608.f[2] * scale;
            sp4b4[0].unk0c = sp638.f[0] * 0.1f;
            sp4b4[0].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp620.f[0] * 0.1f;
            sp4b4[1].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp614.f[0] * 0.1f;
            sp4b4[2].unk10 = sp614.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp608.f[0] * 0.1f;
            sp4b4[3].unk10 = sp608.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp588);
            skyChooseCloudVtxColour(&sp4b4[1], sp580);
            skyChooseCloudVtxColour(&sp4b4[2], sp57c);
            skyChooseCloudVtxColour(&sp4b4[3], sp578);
            break;

        case 10:
            s1 = 4;
            sp4b4[0].unk00 = sp62c.f[0] * scale;
            sp4b4[0].unk04 = sp62c.f[1] * scale;
            sp4b4[0].unk08 = sp62c.f[2] * scale;
            sp4b4[1].unk00 = sp644.f[0] * scale;
            sp4b4[1].unk04 = sp644.f[1] * scale;
            sp4b4[1].unk08 = sp644.f[2] * scale;
            sp4b4[2].unk00 = sp608.f[0] * scale;
            sp4b4[2].unk04 = sp608.f[1] * scale;
            sp4b4[2].unk08 = sp608.f[2] * scale;
            sp4b4[3].unk00 = sp614.f[0] * scale;
            sp4b4[3].unk04 = sp614.f[1] * scale;
            sp4b4[3].unk08 = sp614.f[2] * scale;
            sp4b4[0].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[0].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp644.f[0] * 0.1f;
            sp4b4[1].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp608.f[0] * 0.1f;
            sp4b4[2].unk10 = sp608.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp614.f[0] * 0.1f;
            sp4b4[3].unk10 = sp614.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp584);
            skyChooseCloudVtxColour(&sp4b4[1], sp58c);
            skyChooseCloudVtxColour(&sp4b4[2], sp578);
            skyChooseCloudVtxColour(&sp4b4[3], sp57c);
            break;

        case 1:
            s1 = 3;
            sp4b4[0].unk00 = sp620.f[0] * scale;
            sp4b4[0].unk04 = sp620.f[1] * scale;
            sp4b4[0].unk08 = sp620.f[2] * scale;
            sp4b4[1].unk00 = sp608.f[0] * scale;
            sp4b4[1].unk04 = sp608.f[1] * scale;
            sp4b4[1].unk08 = sp608.f[2] * scale;
            sp4b4[2].unk00 = sp5f0.f[0] * scale;
            sp4b4[2].unk04 = sp5f0.f[1] * scale;
            sp4b4[2].unk08 = sp5f0.f[2] * scale;
            sp4b4[0].unk0c = sp620.f[0] * 0.1f;
            sp4b4[0].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp608.f[0] * 0.1f;
            sp4b4[1].unk10 = sp608.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp5f0.f[0] * 0.1f;
            sp4b4[2].unk10 = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp580);
            skyChooseCloudVtxColour(&sp4b4[1], sp578);
            skyChooseCloudVtxColour(&sp4b4[2], sp570);
            break;

        case 2:
            s1 = 3;
            sp4b4[0].unk00 = sp62c.f[0] * scale;
            sp4b4[0].unk04 = sp62c.f[1] * scale;
            sp4b4[0].unk08 = sp62c.f[2] * scale;
            sp4b4[1].unk00 = sp5fc.f[0] * scale;
            sp4b4[1].unk04 = sp5fc.f[1] * scale;
            sp4b4[1].unk08 = sp5fc.f[2] * scale;
            sp4b4[2].unk00 = sp608.f[0] * scale;
            sp4b4[2].unk04 = sp608.f[1] * scale;
            sp4b4[2].unk08 = sp608.f[2] * scale;
            sp4b4[0].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[0].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp5fc.f[0] * 0.1f;
            sp4b4[1].unk10 = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp608.f[0] * 0.1f;
            sp4b4[2].unk10 = sp608.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp584);
            skyChooseCloudVtxColour(&sp4b4[1], sp574);
            skyChooseCloudVtxColour(&sp4b4[2], sp578);
            break;

        case 4:
            s1 = 3;
            sp4b4[0].unk00 = sp638.f[0] * scale;
            sp4b4[0].unk04 = sp638.f[1] * scale;
            sp4b4[0].unk08 = sp638.f[2] * scale;
            sp4b4[1].unk00 = sp5f0.f[0] * scale;
            sp4b4[1].unk04 = sp5f0.f[1] * scale;
            sp4b4[1].unk08 = sp5f0.f[2] * scale;
            sp4b4[2].unk00 = sp614.f[0] * scale;
            sp4b4[2].unk04 = sp614.f[1] * scale;
            sp4b4[2].unk08 = sp614.f[2] * scale;
            sp4b4[0].unk0c = sp638.f[0] * 0.1f;
            sp4b4[0].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp5f0.f[0] * 0.1f;
            sp4b4[1].unk10 = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp614.f[0] * 0.1f;
            sp4b4[2].unk10 = sp614.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp588);
            skyChooseCloudVtxColour(&sp4b4[1], sp570);
            skyChooseCloudVtxColour(&sp4b4[2], sp57c);
            break;

        case 8:
            s1 = 3;
            sp4b4[0].unk00 = sp644.f[0] * scale;
            sp4b4[0].unk04 = sp644.f[1] * scale;
            sp4b4[0].unk08 = sp644.f[2] * scale;
            sp4b4[1].unk00 = sp614.f[0] * scale;
            sp4b4[1].unk04 = sp614.f[1] * scale;
            sp4b4[1].unk08 = sp614.f[2] * scale;
            sp4b4[2].unk00 = sp5fc.f[0] * scale;
            sp4b4[2].unk04 = sp5fc.f[1] * scale;
            sp4b4[2].unk08 = sp5fc.f[2] * scale;
            sp4b4[0].unk0c = sp644.f[0] * 0.1f;
            sp4b4[0].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp614.f[0] * 0.1f;
            sp4b4[1].unk10 = sp614.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp5fc.f[0] * 0.1f;
            sp4b4[2].unk10 = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp58c);
            skyChooseCloudVtxColour(&sp4b4[1], sp57c);
            skyChooseCloudVtxColour(&sp4b4[2], sp574);
            break;

        case 14:
            s1 = 5;
            sp4b4[0].unk00 = sp62c.f[0] * scale;
            sp4b4[0].unk04 = sp62c.f[1] * scale;
            sp4b4[0].unk08 = sp62c.f[2] * scale;
            sp4b4[1].unk00 = sp644.f[0] * scale;
            sp4b4[1].unk04 = sp644.f[1] * scale;
            sp4b4[1].unk08 = sp644.f[2] * scale;
            sp4b4[2].unk00 = sp638.f[0] * scale;
            sp4b4[2].unk04 = sp638.f[1] * scale;
            sp4b4[2].unk08 = sp638.f[2] * scale;
            sp4b4[3].unk00 = sp5f0.f[0] * scale;
            sp4b4[3].unk04 = sp5f0.f[1] * scale;
            sp4b4[3].unk08 = sp5f0.f[2] * scale;
            sp4b4[4].unk00 = sp608.f[0] * scale;
            sp4b4[4].unk04 = sp608.f[1] * scale;
            sp4b4[4].unk08 = sp608.f[2] * scale;
            sp4b4[0].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[0].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp644.f[0] * 0.1f;
            sp4b4[1].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp638.f[0] * 0.1f;
            sp4b4[2].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp5f0.f[0] * 0.1f;
            sp4b4[3].unk10 = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[4].unk0c = sp608.f[0] * 0.1f;
            sp4b4[4].unk10 = sp608.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp584);
            skyChooseCloudVtxColour(&sp4b4[1], sp58c);
            skyChooseCloudVtxColour(&sp4b4[2], sp588);
            skyChooseCloudVtxColour(&sp4b4[3], sp570);
            skyChooseCloudVtxColour(&sp4b4[4], sp578);
            break;

        case 13:
            s1 = 5;
            sp4b4[0].unk00 = sp644.f[0] * scale;
            sp4b4[0].unk04 = sp644.f[1] * scale;
            sp4b4[0].unk08 = sp644.f[2] * scale;
            sp4b4[1].unk00 = sp638.f[0] * scale;
            sp4b4[1].unk04 = sp638.f[1] * scale;
            sp4b4[1].unk08 = sp638.f[2] * scale;
            sp4b4[2].unk00 = sp620.f[0] * scale;
            sp4b4[2].unk04 = sp620.f[1] * scale;
            sp4b4[2].unk08 = sp620.f[2] * scale;
            sp4b4[3].unk00 = sp608.f[0] * scale;
            sp4b4[3].unk04 = sp608.f[1] * scale;
            sp4b4[3].unk08 = sp608.f[2] * scale;
            sp4b4[4].unk00 = sp5fc.f[0] * scale;
            sp4b4[4].unk04 = sp5fc.f[1] * scale;
            sp4b4[4].unk08 = sp5fc.f[2] * scale;
            sp4b4[0].unk0c = sp644.f[0] * 0.1f;
            sp4b4[0].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp638.f[0] * 0.1f;
            sp4b4[1].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp620.f[0] * 0.1f;
            sp4b4[2].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp608.f[0] * 0.1f;
            sp4b4[3].unk10 = sp608.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[4].unk0c = sp5fc.f[0] * 0.1f;
            sp4b4[4].unk10 = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp58c);
            skyChooseCloudVtxColour(&sp4b4[1], sp588);
            skyChooseCloudVtxColour(&sp4b4[2], sp580);
            skyChooseCloudVtxColour(&sp4b4[3], sp578);
            skyChooseCloudVtxColour(&sp4b4[4], sp574);
            break;

        case 11:
            s1 = 5;
            sp4b4[0].unk00 = sp620.f[0] * scale;
            sp4b4[0].unk04 = sp620.f[1] * scale;
            sp4b4[0].unk08 = sp620.f[2] * scale;
            sp4b4[1].unk00 = sp62c.f[0] * scale;
            sp4b4[1].unk04 = sp62c.f[1] * scale;
            sp4b4[1].unk08 = sp62c.f[2] * scale;
            sp4b4[2].unk00 = sp644.f[0] * scale;
            sp4b4[2].unk04 = sp644.f[1] * scale;
            sp4b4[2].unk08 = sp644.f[2] * scale;
            sp4b4[3].unk00 = sp614.f[0] * scale;
            sp4b4[3].unk04 = sp614.f[1] * scale;
            sp4b4[3].unk08 = sp614.f[2] * scale;
            sp4b4[4].unk00 = sp5f0.f[0] * scale;
            sp4b4[4].unk04 = sp5f0.f[1] * scale;
            sp4b4[4].unk08 = sp5f0.f[2] * scale;
            sp4b4[0].unk0c = sp620.f[0] * 0.1f;
            sp4b4[0].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[1].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp644.f[0] * 0.1f;
            sp4b4[2].unk10 = sp644.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp614.f[0] * 0.1f;
            sp4b4[3].unk10 = sp614.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[4].unk0c = sp5f0.f[0] * 0.1f;
            sp4b4[4].unk10 = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp580);
            skyChooseCloudVtxColour(&sp4b4[1], sp584);
            skyChooseCloudVtxColour(&sp4b4[2], sp58c);
            skyChooseCloudVtxColour(&sp4b4[3], sp57c);
            skyChooseCloudVtxColour(&sp4b4[4], sp570);
            break;

        case 7:
            s1 = 5;
            sp4b4[0].unk00 = sp638.f[0] * scale;
            sp4b4[0].unk04 = sp638.f[1] * scale;
            sp4b4[0].unk08 = sp638.f[2] * scale;
            sp4b4[1].unk00 = sp620.f[0] * scale;
            sp4b4[1].unk04 = sp620.f[1] * scale;
            sp4b4[1].unk08 = sp620.f[2] * scale;
            sp4b4[2].unk00 = sp62c.f[0] * scale;
            sp4b4[2].unk04 = sp62c.f[1] * scale;
            sp4b4[2].unk08 = sp62c.f[2] * scale;
            sp4b4[3].unk00 = sp5fc.f[0] * scale;
            sp4b4[3].unk04 = sp5fc.f[1] * scale;
            sp4b4[3].unk08 = sp5fc.f[2] * scale;
            sp4b4[4].unk00 = sp614.f[0] * scale;
            sp4b4[4].unk04 = sp614.f[1] * scale;
            sp4b4[4].unk08 = sp614.f[2] * scale;
            sp4b4[0].unk0c = sp638.f[0] * 0.1f;
            sp4b4[0].unk10 = sp638.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[1].unk0c = sp620.f[0] * 0.1f;
            sp4b4[1].unk10 = sp620.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[2].unk0c = sp62c.f[0] * 0.1f;
            sp4b4[2].unk10 = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[3].unk0c = sp5fc.f[0] * 0.1f;
            sp4b4[3].unk10 = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;
            sp4b4[4].unk0c = sp614.f[0] * 0.1f;
            sp4b4[4].unk10 = sp614.f[2] * 0.1f + g_SkyCloudOffset;

            skyChooseCloudVtxColour(&sp4b4[0], sp588);
            skyChooseCloudVtxColour(&sp4b4[1], sp580);
            skyChooseCloudVtxColour(&sp4b4[2], sp584);
            skyChooseCloudVtxColour(&sp4b4[3], sp574);
            skyChooseCloudVtxColour(&sp4b4[4], sp57c);
            break;

        default:
            return gdl;
    }

    gDPPipeSync(gdl++);

#ifdef GEVR
    /* D227 (M-100): see the matching capture in the IsWater block above. */
    {
        Gfx *skyTileFrom = gdl;
        texSelect(&gdl, &skywaterimages[fogGetCurrentEnvironmentp()->SkyImageId], 1, 0, 2);
        skyPortCaptureTile(skyTileFrom, gdl);
    }
#else
    texSelect(&gdl, &skywaterimages[fogGetCurrentEnvironmentp()->SkyImageId], 1, 0, 2);
#endif

    if (1);

    gDPSetEnvColor(gdl++, fogGetCurrentEnvironmentp()->Red, fogGetCurrentEnvironmentp()->Green, fogGetCurrentEnvironmentp()->Blue, 0xff);
    gDPSetCombineLERP(gdl++,
            SHADE, ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, SHADE,
            SHADE, ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, SHADE);

    {
        Mtxf sp1ec;
        Mtxf sp1ac;
        SkyRelated38 sp94[5];
        s32 i;
        s32 stack[2];

        matrix_4x4_multiply(currentPlayerGetProjectionMatrixF(), camGetWorldToScreenMtxf(), &sp1ec);
        guScaleF(dword_CODE_bss_80079E98.m, 1.0f / scale, 1.0f / scale, 1.0f / scale);
        matrix_4x4_multiply(&sp1ec, &dword_CODE_bss_80079E98, &sp1ac);

        for (i = 0; i < s1; i++)
        {
            sub_GAME_7F097388(&sp4b4[i], &sp1ac, 130, 65535.0f, 65535.0f, &sp94[i]);

            sp94[i].unk28 = skyClamp(sp94[i].unk28, getPlayer_c_screenleft() * 4.0f, (getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f - 1.0f);
            sp94[i].unk2c = skyClamp(sp94[i].unk2c, getPlayer_c_screentop() * 4.0f, (getPlayer_c_screentop() + getPlayer_c_screenheight()) * 4.0f - 1.0f);
        }

#ifdef GEVR
        {
            /*
             * Stereo, all four corners in the sky: a corner within a degree of
             * the horizon is kept on the cloud layer (skyIsScreenCornerInSky),
             * so it no longer projects onto its own corner; held there, as the
             * top corners are when the horizon shows, the layer still fills
             * the view.
             */
            extern s32 g_gevrStereo;

            if (g_gevrStereo && s1 == 4 && ((sp538 << 3) | (sp534 << 2) | (sp530 << 1) | sp52c) == 15)
            {
                f32 l4 = getPlayer_c_screenleft() * 4.0f;
                f32 t4 = getPlayer_c_screentop() * 4.0f;
                f32 r4 = (getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f - 1.0f;
                f32 b4 = (getPlayer_c_screentop() + getPlayer_c_screenheight()) * 4.0f - 1.0f;

                sp94[0].unk28 = l4; sp94[0].unk2c = t4;
                sp94[1].unk28 = r4; sp94[1].unk2c = t4;
                sp94[2].unk28 = l4; sp94[2].unk2c = b4;
                sp94[3].unk28 = r4; sp94[3].unk2c = b4;
            }
        }
        /* D227 (M-95): one shared wScale for every triangle drawn from this
         * sp94[] fan -- see skyPortBeginFan() above. Computed from unk0c,
         * which none of the position overrides below touch. */
        skyPortBeginFan(sp94, s1, TRUE);
#endif
        if (s1 == 4)
        {
            if (((sp538 << 3) | (sp534 << 2) | (sp530 << 1) | sp52c) == 12)
            {
                if (sp548 < sp54c)
                {
                    if (sp94[3].unk2c >= sp94[1].unk2c + 4.0f)
                    {
                        sp94[0].unk28 = getPlayer_c_screenleft() * 4.0f;
                        sp94[0].unk2c = getPlayer_c_screentop() * 4.0f;
                        sp94[1].unk28 = (getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f - 1.0f;
                        sp94[1].unk2c = getPlayer_c_screentop() * 4.0f;
                        sp94[2].unk28 = getPlayer_c_screenleft() * 4.0f;
                        sp94[3].unk28 = (getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f - 1.0f;

                        gdl = skyRenderFull(gdl, &sp94[0], &sp94[1], &sp94[2], &sp94[3], 130.0f);
                    }
                    else
                    {
                        gdl = skyRenderTri(gdl, &sp94[0], &sp94[1], &sp94[2], 130.0f, TRUE);
                    }
                }
                else if (sp94[2].unk2c >= sp94[0].unk2c + 4.0f)
                {
                    sp94[0].unk28 = getPlayer_c_screenleft() * 4.0f;
                    sp94[0].unk2c = getPlayer_c_screentop() * 4.0f;
                    sp94[1].unk28 = (getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f - 1.0f;
                    sp94[1].unk2c = getPlayer_c_screentop() * 4.0f;
                    sp94[2].unk28 = getPlayer_c_screenleft() * 4.0f;
                    sp94[3].unk28 = (getPlayer_c_screenleft() + getPlayer_c_screenwidth()) * 4.0f - 1.0f;

                    gdl = skyRenderFull(gdl, &sp94[1], &sp94[0], &sp94[3], &sp94[2], 130.0f);
                }
                else
                {
                    gdl = skyRenderTri(gdl, &sp94[1], &sp94[0], &sp94[3], 130.0f, TRUE);
                }
            }
            else
            {
                gdl = skyRenderTri(gdl, &sp94[0], &sp94[1], &sp94[3], 130.0f, TRUE);
                gdl = skyRenderTri(gdl, &sp94[3], &sp94[2], &sp94[0], 130.0f, TRUE);
            }
        }
        else if (s1 == 5)
        {
            gdl = skyRenderTri(gdl, &sp94[0], &sp94[1], &sp94[2], 130.0f, TRUE);
            gdl = skyRenderTri(gdl, &sp94[0], &sp94[2], &sp94[3], 130.0f, TRUE);
            gdl = skyRenderTri(gdl, &sp94[0], &sp94[3], &sp94[4], 130.0f, TRUE);
        }
        else if (s1 == 3)
        {
            gdl = skyRenderTri(gdl, &sp94[0], &sp94[1], &sp94[2], 130.0f, TRUE);
        }
#ifdef GEVR
        skyPortEndFan();
#endif
    }

    return gdl;
}


void sub_GAME_7F097388(SkyRelated18 *arg0, Mtxf *arg1, u16 arg2, f32 arg3, f32 arg4, SkyRelated38 *arg5)
{
    f32 sp68[4];
    f32 sp64;
    f32 sp60;
    f32 f22;
    f32 f0;
    f32 sp48[4];
    f32 sp38[4];
    f32 mult;

    mult = arg2 / 65536.0f;

    sp68[0] = (arg0->unk00 * arg1->m[0][0] + arg0->unk04 * arg1->m[1][0] + arg0->unk08 * arg1->m[2][0]) + arg1->m[3][0];
    sp68[1] = (arg0->unk00 * arg1->m[0][1] + arg0->unk04 * arg1->m[1][1] + arg0->unk08 * arg1->m[2][1]) + arg1->m[3][1];
    sp68[2] = (arg0->unk00 * arg1->m[0][2] + arg0->unk04 * arg1->m[1][2] + arg0->unk08 * arg1->m[2][2]) + arg1->m[3][2];
    sp68[3] = (arg0->unk00 * arg1->m[0][3] + arg0->unk04 * arg1->m[1][3] + arg0->unk08 * arg1->m[2][3]) + arg1->m[3][3];

    sp60 = arg0->unk0c * (arg3 / 65536.0f);
    sp64 = arg0->unk10 * (arg4 / 65536.0f);

    if (sp68[3] == 0.0f)
    {
        f22 = 32767.0f;
    }
    else
    {
        f22 = 1.0f / (sp68[3] * mult);
    }

    f0 = f22;
    if (f0 < 0.0f) { f0 = 32767.0f; }

    sp48[0] = sp68[0] * f0 * mult;
    sp48[1] = sp68[1] * f0 * mult;
    sp48[2] = sp68[2] * f0 * mult;
    sp48[3] = sp68[3] * f0 * mult;

    sp38[0] = sp48[0] * (getPlayer_c_screenwidth() * 2) + (getPlayer_c_screenwidth() * 2 + getPlayer_c_screenleft() * 4);
    sp38[1] = -sp48[1] * (getPlayer_c_screenheight() * 2) + (getPlayer_c_screenheight() * 2 + getPlayer_c_screentop() * 4);

    sp38[2] = sp48[2] * 511.0f + 511.0f;
    sp38[3] = sp48[3] * 0 + 0;

    sp38[0] = skyClamp(sp38[0], -4090.0f, 4090.0f);
    sp38[1] = skyClamp(sp38[1], -4090.0f, 4090.0f);
    sp38[2] = skyClamp(sp38[2], 0.0f, 32767.0f);
    sp38[3] = skyClamp(sp38[3], 0.0f, 32767.0f);

    arg5->unk00 = sp68[0];
    arg5->unk04 = sp68[1];
    arg5->unk08 = sp68[2];
    arg5->unk0c = sp68[3];
    arg5->unk20 = sp60;
    arg5->unk24 = sp64;
    arg5->unk28 = sp38[0];
    arg5->unk2c = sp38[1] - skyScreenDrop() * 4.0f;
    arg5->unk30 = sp38[2];
    arg5->unk34 = f22;

    arg5->r = arg0->r;
    arg5->g = arg0->g;
    arg5->b = arg0->b;
    arg5->a = arg0->a;
}

/*
* Address: 0x7F0977B4
*/
bool skyVerticesAreTheSame(SkyRelated38 *arg0, SkyRelated38 *arg1)
{
    f32 f0;
    f32 f1;

    f0 = arg0->unk28 - arg1->unk28;
    f1 = arg0->unk2c - arg1->unk2c;
    return sqrtf((f0 * f0) + (f1 * f1)) < 1.0f ? TRUE : FALSE;
}

#ifdef GEVR
/* D227 (M-95): the two fan-drawing blocks below (the sp274[]/sp94[] arrays
 * further down this file) each populate up to 5 SkyRelated38 verts once,
 * then issue 1-3 separate skyRenderTri/skyRenderFull calls that each draw a
 * different triangle from that SAME shared vertex pool. skyPortRenderPoly
 * used to compute its wScale locally from only the 2-4 verts of whichever
 * call it's currently servicing -- so two triangles sharing an edge/vertex
 * could each independently pick a different wScale for that shared vertex,
 * and since Vtx.ob[] is s16, quantize it to two slightly different values.
 * That crack at a shared triangle edge is the "two-halves seam" defect
 * (findings.md D227) -- confirmed by a headless capture showing a static-
 * frame crease, screenshot docs/img/bugs/d227-sky-seam-statue.png.
 *
 * Fix: the two call sites compute one shared wScale across the WHOLE fan
 * (every vertex about to be drawn this frame, not just one call's 2-4) via
 * skyPortBeginFan() right after populating their vertex array, before the
 * first skyRenderTri/skyRenderFull call; skyPortRenderPoly uses that shared
 * value instead of recomputing locally. skyPortEndFan() clears it so a
 * hypothetical future caller that forgets to call skyPortBeginFan() falls
 * back to the old per-call-local computation instead of reusing a stale
 * value.
 *
 * D227 (M-98): the same per-call-local mistake was still present one field
 * over -- skyPortRenderPoly's S/T fold (foldS/foldT, below) was computed
 * from only the current call's own 2-4 verts, same as wScale used to be.
 * M-95 only shared wScale; it never touched the fold, so the seam it was
 * meant to close persisted (confirmed by human playtest, M-96). Two
 * triangles in one fan sharing an edge vertex can each independently floor
 * that vertex's S/T to a different multiple of 64 texels; the *visual*
 * result is supposed to be identical (GL_REPEAT has period 64), but each
 * triangle bakes its own copy of the shared vertex into its own Vtx buffer
 * at `(S - fold) * 32`, and two folds differing by 64 texels produce tc
 * values differing by exactly 2048 (64*32) -- an s16-precision difference
 * big enough to read as a crack at the shared edge, not just any leftover
 * discontinuity. Fixed the same way as wScale: fold once across the whole
 * fan in skyPortBeginFan(), thread it through instead of recomputing. */
static f32 s_skyFanWScale = 0.0f; /* 0 = not set; per-call fallback */
static f32 s_skyFanFoldS = 0.0f;
static f32 s_skyFanFoldT = 0.0f;
static bool s_skyFanFoldSet = FALSE;

/*
 * D227 (M-100): the fix for the real defect, on top of the two above.
 *
 * THE UNIT BUG. M-82 ("D176(a) Defect 1") read unk20/unk24 as texel counts and
 * so baked tc as (S - fold) * 32, converting texels to tc's S10.5 units. They
 * are not texel counts. They are set in skyRender as worldpos * 0.1f (see the
 * sp4b4[].unk0c/unk10 assignments) and handed to the RDP's coefficient block
 * unscaled (skyRenderTri: S * unk34 * sp368), so they are already in the RDP's
 * native S10.5 1/32-texel units. The extra *32 made every sky texture
 * coordinate 32x too large, which caused three symptoms at once:
 *
 *   1. D227 itself. One quad's S/T then spanned ~30,000 *texels* where an s16
 *      tc holds +/-1024, so 7 of a quad's 8 coordinates overflowed and wrapped
 *      to arbitrary values (measured, GE_D227V probe, -level_41 frame 350).
 *      The rasteriser interpolated between garbage: that is what the "two sky
 *      processes" / starburst was. The quad splits on the 0-3 diagonal
 *      (gSP2Triangles below) and verts 0 and 3 are exactly the two that keep
 *      one coordinate in range, hence two differently-wrong screen halves.
 *   2. The sky tiling ~32x too finely (466 repeats of the 64-texel cloud
 *      across one quad instead of ~15).
 *   3. The sky scrolling ~32x too fast -- the long-standing "N64 sky is nearly
 *      static, ours races" complaint (M-96), measured at ~7 px/tick in M-98.
 *      g_SkyCloudOffset advances 1 unit/tick, which is 1/32 texel, not 1.
 *
 * Reading the units correctly fixes all three: tc = (S - fold), 1:1, and the
 * ~30,000-unit span fits an s16 unaided. That is also why the original N64 code
 * needs no tessellation here -- it never had a range problem to solve.
 *
 * M-95's shared wScale and M-98's shared fold stay: they were real bugs, and
 * per-fan consistency is still what keeps shared vertices bit-identical
 * between the fan's primitives.
 *
 * THE SAFETY VALVE (normally inactive, k == 0). A span can in principle still
 * exceed s16. The RDP's own answer to that is the tile descriptor's shift
 * field: coordinates shift right by `shift` for shift <= 10 and LEFT by
 * (16 - shift) for shift >= 11. So divide tc by 2^k and set the tile shift to
 * (16 - k) -- decoded coordinate unchanged, s16 reaches 2^k further.
 * port/fast3d implements this exactly (gfx_pc.cpp, gfx_dp_set_tile plus the
 * u/v shift block), so it is a native GBI mechanism, not a port-side hack: no
 * fast3d change, no tessellation, no extra vertices.
 *
 * k is chosen per FAN, not per primitive, for the same reason wScale and the
 * fold are: the decoded coordinate is identical for any k, but the granularity
 * is 2^k units, so two primitives sharing an edge under different k could round
 * a shared vertex apart.
 *
 * k = 5 is the cap, because shift (16 - 5) == 11 is the smallest value fast3d
 * still treats as a left shift. Beyond that a span would genuinely need
 * Defect-2 tessellation; skyPortPickShift clamps rather than overflowing, so
 * the failure mode stays graceful.
 */

/* One tile period expressed in unk20/unk24's units: 64 texels * 32 = 2048.
 * The fold must be a whole number of these or the rebase shifts the texture. */
#define SKY_TC_WRAP 2048

static s32 s_skyFanShiftS = 0;
static s32 s_skyFanShiftT = 0;

/*
 * Applying the shift means re-emitting the render tile's G_SETTILE with only
 * its two shift fields changed. Rather than rebuild that command -- its format,
 * depth, line and mask fields are derived inside texSelect() from the texture
 * pool, and duplicating that derivation here would rot the moment a sky image
 * had a different format -- capture the exact command texSelect() just emitted
 * and replay a patched copy. Exact by construction, and it stays correct if
 * texSelect's derivation ever changes.
 *
 * G_SETTILE word 1 layout (see port/fast3d/gfx_pc.cpp's G_SETTILE dispatch):
 *   tile = bits 24..26, shiftt = bits 10..13, shifts = bits 0..3.
 */
#define SKY_SETTILE_SHIFTT_SHIFT 10
#define SKY_SETTILE_FIELD_MASK   0xF

/* Tiles 0 and 1 are both captured. The IsWater path is a two-texunit draw
 * (sub_GAME_7F09343C binds tile 0 and tile 1 to the same TMEM and cross-fades
 * TEXEL0/TEXEL1 by an animated PRIM_LOD_FRAC), and both texunits read the same
 * vertex tc, so a shift applied to only one of them would desynchronise the
 * two layers. Capture must also run AFTER every tile-setup call for the draw,
 * not just after texSelect -- sub_GAME_7F09343C re-declares both tiles, so
 * capturing earlier would replay a stale declaration and clobber it. */
#define SKY_NUM_CAPTURED_TILES 2

static Gfx s_skyTileCmd[SKY_NUM_CAPTURED_TILES];
static bool s_skyTileCmdOk[SKY_NUM_CAPTURED_TILES];

/* Scan the display list just written for each tile's G_SETTILE. Takes the last
 * match per tile: the mipmapped path emits one per level, and a later setup
 * call may re-declare a tile the earlier one already set. */
static void skyPortCaptureTile(Gfx *start, Gfx *end)
{
    Gfx *p;
    s32 i;

    for (i = 0; i < SKY_NUM_CAPTURED_TILES; i++)
        s_skyTileCmdOk[i] = FALSE;

    for (p = start; p < end; p++)
    {
        u32 tile;

        if ((u32) (p->words.w0 >> 24) != (u32) (u8) G_SETTILE)
            continue;

        tile = (p->words.w1 >> 24) & 7;
        if (tile >= (u32) SKY_NUM_CAPTURED_TILES)
            continue;

        s_skyTileCmd[tile] = *p;
        s_skyTileCmdOk[tile] = TRUE;
    }
}

/* Re-emit every captured tile with shifts/shiftt replaced. k == 0 on both axes
 * restores the original commands, which is how the sky hands the tiles back
 * unshifted for whatever draws next. */
static Gfx *skyPortEmitTileShift(Gfx *gdl, s32 kS, s32 kT)
{
    s32 i;

    for (i = 0; i < SKY_NUM_CAPTURED_TILES; i++)
    {
        Gfx cmd;

        if (!s_skyTileCmdOk[i])
            continue;

        cmd = s_skyTileCmd[i];
        cmd.words.w1 &= ~((u32) SKY_SETTILE_FIELD_MASK << SKY_SETTILE_SHIFTT_SHIFT);
        cmd.words.w1 &= ~((u32) SKY_SETTILE_FIELD_MASK);
        if (kT > 0)
            cmd.words.w1 |= (u32) (16 - kT) << SKY_SETTILE_SHIFTT_SHIFT;
        if (kS > 0)
            cmd.words.w1 |= (u32) (16 - kS);

        *gdl++ = cmd;
    }

    return gdl;
}

/* Smallest k in [0, SKY_TC_MAX_SHIFT] for which the whole span still fits an
 * s16 once divided by 2^k. `span` is the fan's S or T extent in S10.5 units;
 * the +SKY_TC_WRAP covers the fold's rebase to a tile boundary. Normally
 * returns 0 -- see the unit note above, the coordinates fit unaided. */
static s32 skyPortPickShift(f32 span)
{
    s32 k;
    for (k = 0; k < SKY_TC_MAX_SHIFT; k++)
    {
        if ((span + (f32) SKY_TC_WRAP) / (f32) (1 << k) <= 32767.0f)
            break;
    }
    return k;
}

/*
 * `allowShift` gates the safety valve. The IsWater quad used to pass FALSE
 * (see D245) on the theory that a matched shift on both texunits would
 * flatten the cross-fade; that theory was wrong and D245 corrects it here.
 *
 * That quad is a two-texunit draw: sub_GAME_7F09343C binds tile 0 and tile 1
 * to the same TMEM and cross-fades TEXEL0/TEXEL1 with an animated
 * PRIM_LOD_FRAC (the water shimmer). Both texunits read the SAME vertex tc;
 * what makes tile 1 sample a different part of the texture is its own
 * SetTileSize uls/ult offset (90,150 in 1/4-texel units -- see
 * sub_GAME_7F09343C), applied by the RDP AFTER the tile's shift field
 * multiplies the (possibly pre-divided) coordinate back up. skyPortEmitTileShift
 * replays BOTH tiles' captured SETTILE commands with the identical k, so both
 * texunits' decoded S/T are rescaled by the same 2^k before their own
 * (unshifted, tile-native) uls/ult offset is applied -- the offset between
 * tile 0 and tile 1's sample points is preserved, not flattened. Confirmed by
 * a diagnostic build with allowShift forced TRUE on the water path: the
 * cross-fade shimmer still animates, no flattening. D245 (view-tracking
 * seam) was this quad's horizon vertex genuinely overflowing an s16 tc with
 * no shift applied -- same class of bug as D227 before M-100b's fix, just
 * never given the safety valve.
 *
 * The water on IsWater levels was separately and visibly broken (D229, green
 * / pulsating, now FIXED) and that was NOT caused by any of this -- it
 * reproduced identically on main with none of the D227 work present.
 */
static void skyPortBeginFan(SkyRelated38 *v, s32 n, bool allowShift)
{
    f32 maxAbsW = 0.0f;
    f32 minS, minT, maxS, maxT;
    s32 i;
    for (i = 0; i < n; i++)
    {
        f32 aw = SKYABS(v[i].unk0c);
        if (aw > maxAbsW) maxAbsW = aw;
    }
    s_skyFanWScale = (maxAbsW > 30000.0f) ? (maxAbsW / 30000.0f) : 1.0f;

    minS = maxS = v[0].unk20;
    minT = maxT = v[0].unk24;
    for (i = 1; i < n; i++)
    {
        if (v[i].unk20 < minS) minS = v[i].unk20;
        if (v[i].unk24 < minT) minT = v[i].unk24;
        if (v[i].unk20 > maxS) maxS = v[i].unk20;
        if (v[i].unk24 > maxT) maxT = v[i].unk24;
    }
    s_skyFanFoldS = (f32) ((s32) floorf(minS / (f32) SKY_TC_WRAP) * SKY_TC_WRAP);
    s_skyFanFoldT = (f32) ((s32) floorf(minT / (f32) SKY_TC_WRAP) * SKY_TC_WRAP);
    s_skyFanFoldSet = TRUE;

    s_skyFanShiftS = allowShift ? skyPortPickShift(maxS - minS) : 0;
    s_skyFanShiftT = allowShift ? skyPortPickShift(maxT - minT) : 0;
}

static void skyPortEndFan(void)
{
    s_skyFanWScale = 0.0f;
    s_skyFanFoldSet = FALSE;
    s_skyFanShiftS = 0;
    s_skyFanShiftT = 0;
}

/* D245 (M-142): overrides the shift skyPortBeginFan() just picked. A GE_D245V
 * capture (scripted camera turn, Frigate, headless) proved the water quad's
 * raw S/T span sits in the ~290k-390k unit range -- roughly 10x sky's typical
 * span -- and, critically, that skyPortPickShift's adaptive k genuinely
 * CHANGES during ordinary view rotation (observed kT flipping 4->3 as spanT
 * crossed skyPortPickShift's per-k breakpoint mid-turn). That contradicts the
 * "normally k==0, this is a rare safety valve" assumption the mechanism was
 * verified against (a single static frame, one camera angle) -- every k
 * transition re-quantizes the shared vertex tc at a different granularity,
 * which is a plausible mechanism for "glitches, goes back and forth... changes
 * depending on your view" even with both texunits' relative offset preserved
 * (D245's already-verified allowShift=TRUE reasoning). Untested candidate,
 * NOT the default: forcing a single fixed k for the whole water draw removes
 * the transition, at the cost of coarser (whole-k5-step) tc quantization
 * always, which could itself look worse -- needs a live A/B, not another
 * blind ship (see the finding's own "don't repeat the same mistake" note).
 * GE_D245_FIXEDSHIFT=<k> (0..SKY_TC_MAX_SHIFT) opts in; unset changes nothing. */
static void skyPortForceFanShift(s32 kS, s32 kT)
{
    s_skyFanShiftS = kS;
    s_skyFanShiftT = kT;
}

/*
 * D176(a) Path B (M-46) -- black-sky fix. Full rationale in
 * docs/dev/findings.md section F, "D176(a)".
 *
 * skyRenderTri / skyRenderFull normally build their geometry as a verbatim
 * N64 RDP triangle command, chopped into 32-bit halves and carried as
 * gImmp1(G_RDPHALF_1 / _CONT / _2, word) pairs for GE's modified RSP ucode
 * (rsp/graphics/gmain.s) to reassemble and DMA to the RDP as an edge-walked
 * G_TRI_FILL / G_TRI_SHADE_TXTR. The PC software RSP (port/fast3d) has no RDP
 * triangle rasteriser and deliberately drops G_RDPHALF_* (gfx_pc.cpp), so
 * every cloud-sky level rendered a solid black upper half.
 *
 * The SkyRelated38 verts handed to these functions are already fully
 * screen-space projected by sub_GAME_7F097388:
 *   unk28 = screenX * 4, unk2c = screenY * 4, unk30 = screenZ (0..0x7fff),
 *   unk34 = 1/w, unk20 / unk24 = S / T, r/g/b/a = per-vertex colour.
 * Under PORT we skip the RDPHALF encode entirely and re-emit the same
 * triangle(s) as an ordinary gSPVertex + gSP*Triangle batch under a
 * pixel-space ortho projection the software RSP can consume. The combiner
 * (SHADE,ENV,TEXEL0,ENV), env colour and texSelect(&skywaterimages[...])
 * are all already emitted by the caller. This is a pure N64-RSP-idiom
 * substitution (same class as the G_TRI4 / dynamic-lighting PORT branches
 * elsewhere in src/): the N64 (#else) path below is byte-for-byte unchanged.
 */
static Gfx *skyPortRenderPoly(Gfx *gdl, SkyRelated38 **v, s32 nverts)
{
    Vtx *vtx = dynAllocateVertices(nverts < 3 ? 3 : nverts);
    Mtxf projf;
    Mtx *proj = dynAllocateMatrix();
    Mtx *mv = dynAllocateMatrix();
    f32 l = getPlayer_c_screenleft();
    f32 t = getPlayer_c_screentop();
    f32 r = l + getPlayer_c_screenwidth();
    f32 b = t + getPlayer_c_screenheight();
    f32 maxAbsW, wScale;
    s32 i;
    /* D227 (M-100). Fan-wide when skyPortBeginFan() ran (the only callers
     * today); a lone primitive falls back to sizing the shift off its own
     * span, which is self-consistent because k never changes the decoded
     * coordinate -- only its granularity. */
    s32 tcShiftS = s_skyFanShiftS;
    s32 tcShiftT = s_skyFanShiftT;

    /* D176(a) Defect 1 (M-82), as corrected by D227 (M-100): unk20/unk24 are
     * S/T for the 64x64 cloud tile (s_skywaterimages[0]) in Vtx.tc's own S10.5
     * 1/32-texel units -- NOT texels, which is what M-82 assumed when it baked
     * tc as S * 32. They still reach tens of thousands near the horizon, so the
     * fold is still needed: GL_REPEAT has period tex_width (64 texels =
     * SKY_TC_WRAP units), so folding every vertex of the fan by the SAME
     * multiple of that drops magnitudes into s16 range while preserving
     * inter-vertex deltas (hence the interpolated texture) exactly. With the
     * units read correctly the spans fit unaided and no Defect-2 tessellation
     * is required; skyPortPickShift is the safety valve if one ever doesn't. */
    f32 foldS, foldT;
    /* D227 (M-98): prefer the whole-fan fold set by skyPortBeginFan() so
     * every triangle in a multi-call fan bakes its shared vertices against
     * the same S/T origin (see the comment above skyPortBeginFan()). Falls
     * back to the old per-call-local computation if unset. */
    if (s_skyFanFoldSet)
    {
        foldS = s_skyFanFoldS;
        foldT = s_skyFanFoldT;
    }
    else
    {
        f32 maxS, maxT;

        foldS = maxS = v[0]->unk20;
        foldT = maxT = v[0]->unk24;
        for (i = 1; i < nverts; i++)
        {
            if (v[i]->unk20 < foldS) foldS = v[i]->unk20;
            if (v[i]->unk24 < foldT) foldT = v[i]->unk24;
            if (v[i]->unk20 > maxS) maxS = v[i]->unk20;
            if (v[i]->unk24 > maxT) maxT = v[i]->unk24;
        }
        tcShiftS = skyPortPickShift(maxS - foldS);
        tcShiftT = skyPortPickShift(maxT - foldT);
        foldS = (f32) ((s32) floorf(foldS / (f32) SKY_TC_WRAP) * SKY_TC_WRAP);
        foldT = (f32) ((s32) floorf(foldT / (f32) SKY_TC_WRAP) * SKY_TC_WRAP);
    }

    /* D176(a) "M-90" fix: skyPortRenderPoly used to place these verts under a
     * plain pixel-space ORTHO projection, i.e. every vertex got w=1 and the
     * GPU/software rasteriser interpolated tc *linearly in screen space*
     * across the primitive. But unk20/unk24 (S/T) are texel coords on the
     * world-space cloud plane, computed upstream (sub_GAME_7F097388) WITHOUT
     * a perspective divide -- exactly like the RDP's own G_TRI_SHADE_TXTR
     * coefficient block, which carries S*w'/T*w'/w' triples (see
     * docs/dev/D176a-SKY-NOTES.md) and relies on the RDP's normal
     * perspective-correct texture unit to divide back out per pixel. Linear
     * interpolation instead of perspective-correct interpolation is only
     * exact when every vertex shares the same w -- here the near (top of
     * screen) and horizon-grazing (bottom) verts of one quad can differ by
     * >20x in camera-space w (unk0c), so the S/T delta was smeared evenly
     * across every screen pixel instead of being weighted toward the near
     * vertices, producing a uniformly dense, aliased "hatching" over the
     * whole sky and reading as "scrolls too fast / tiles too visibly" --
     * the true per-pixel gradient should stay small until right at the
     * horizon edge. Fix: build a real perspective (w != 1) transform so the
     * existing fast3d vertex/triangle pipeline (same one every other
     * textured draw in the game already relies on) does the perspective
     * divide for us, exactly like the RDP would.
     *
     * ob[] is s16 so we can't store camera-space w (unk0c, up to ~2e5 near
     * the horizon) directly; instead each vertex's NDC xy is pre-multiplied
     * by its own w/wScale (wScale keeps the product in s16 range) and a
     * custom projection matrix multiplies that by wScale again for clip.x/y
     * and routes ob[2] (=w/wScale) through to clip.w -- see the matrix
     * comment below. Z is unused for the sky (draws first, into a cleared
     * buffer -- M-46) so clip.z is left 0.
     */
    /* D227 (M-95): prefer the whole-fan wScale set by skyPortBeginFan() so
     * every triangle in a multi-call fan quantizes its shared vertices
     * against the same scale (see the comment above skyPortBeginFan()).
     * Falls back to the old per-call-local computation if unset. */
    if (s_skyFanWScale > 0.0f)
    {
        wScale = s_skyFanWScale;
    }
    else
    {
        maxAbsW = 0.0f;
        for (i = 0; i < nverts; i++)
        {
            f32 aw = SKYABS(v[i]->unk0c);
            if (aw > maxAbsW) maxAbsW = aw;
        }
        wScale = (maxAbsW > 30000.0f) ? (maxAbsW / 30000.0f) : 1.0f;
    }

    guMtxIdentF(projf.m);
    /* row = input axis (matches this codebase's row-vector * M convention,
     * e.g. sub_GAME_7F097388 / gfx_pc.cpp's own MP_matrix use):
     *   clip.x = ob[0]*wScale                = (ndcX*w/wScale)*wScale = ndcX*w
     *   clip.y = ob[1]*wScale                = ndcY*w
     *   clip.z = 0
     *   clip.w = ob[2]*wScale                = (w/wScale)*wScale     = w
     * so the GPU's normal perspective divide recovers ndcX/ndcY at each
     * vertex exactly (screen position unchanged) while interpolating
     * everything else -- tc included -- with real 1/w weighting. */
    projf.m[0][0] = wScale;
    projf.m[1][1] = wScale;
    projf.m[2][2] = 0.0f;
    projf.m[2][3] = wScale;
    projf.m[3][3] = 0.0f;
    guMtxF2L(projf.m, proj);
    guMtxIdent(mv);

    for (i = 0; i < nverts; i++)
    {
        f32 screenX = v[i]->unk28 * 0.25f;
        f32 screenY = v[i]->unk2c * 0.25f;
        f32 ndcX = 2.0f * ((screenX - l) / (r - l)) - 1.0f;
        f32 ndcY = 1.0f - 2.0f * ((screenY - t) / (b - t));
        f32 wv = v[i]->unk0c / wScale;

        vtx[i].v.ob[0] = (s16) (ndcX * wv);
        vtx[i].v.ob[1] = (s16) (ndcY * wv);
        vtx[i].v.ob[2] = (s16) wv;
        vtx[i].v.flag  = 0;
        /* D227 (M-100): unk20/unk24 are ALREADY in tc's own S10.5 units, so
         * they go through 1:1 -- see the unit note above skyPortBeginFan().
         * The 2^k divide is the overflow safety valve, normally k == 0; the
         * tile shift emitted below multiplies it back out. */
        vtx[i].v.tc[0] = (s16) ((v[i]->unk20 - foldS) / (f32) (1 << tcShiftS));
        vtx[i].v.tc[1] = (s16) ((v[i]->unk24 - foldT) / (f32) (1 << tcShiftT));
        vtx[i].v.cn[0] = (u8) v[i]->r;
        vtx[i].v.cn[1] = (u8) v[i]->g;
        vtx[i].v.cn[2] = (u8) v[i]->b;
        vtx[i].v.cn[3] = (u8) v[i]->a;

        /* D227 (M-100) probe: dumps the per-vertex S/T/w actually handed to
         * the rasteriser. This is what proved the seam is s16 tc overflow --
         * one sky quad spans ~30,000 texels in S and T, but tc is S10.5, so
         * (S - foldS) * 32 only holds +/-1024 texels. Same env-gate style as
         * the GE_D176 probes above. */
        if (getenv("GE_D227V")) {
            static int n = 0;
            if (n++ < 200)
                fprintf(stderr, "D227V poly nv=%d i=%d w=%.1f 1/w=%.6g sx=%.1f sy=%.1f S=%.1f T=%.1f "
                        "kS=%d kT=%d tc=%d,%d tileOk=%d\n",
                        nverts, i, (double) v[i]->unk0c, (double) v[i]->unk34,
                        (double) screenX, (double) screenY,
                        (double) v[i]->unk20, (double) v[i]->unk24,
                        tcShiftS, tcShiftT, vtx[i].v.tc[0], vtx[i].v.tc[1],
                        (s32) s_skyTileCmdOk[0]);
        }
    }

    gSPMatrix(gdl++, osVirtualToPhysical(proj), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(gdl++, osVirtualToPhysical(mv), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPClearGeometryMode(gdl++, G_LIGHTING | G_CULL_BOTH | G_FOG);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
    gdl = skyPortEmitTileShift(gdl, tcShiftS, tcShiftT);
    gSPVertex(gdl++, osVirtualToPhysical(vtx), nverts, 0);

    if (nverts >= 4)
    {
        gSP2Triangles(gdl++, 0, 1, 3, 0, 3, 2, 0, 0);
    }
    else
    {
        gSP1Triangle(gdl++, 0, 1, 2, 0);
    }

    /* Hand the tile back unshifted so nothing downstream inherits it. */
    if (tcShiftS > 0 || tcShiftT > 0)
        gdl = skyPortEmitTileShift(gdl, 0, 0);

    return gdl;
}
#endif

/*
* Address: 0x7F097818
*/
Gfx *skyRenderTri(Gfx *gdl, SkyRelated38 *arg1, SkyRelated38 *arg2, SkyRelated38 *arg3, f32 arg4, bool textured)
{
    SkyRelated38 *sp484;
    SkyRelated38 *sp480;
    SkyRelated38 *sp47c;
    s32 i;
    f32 sp474;
    f32 sp470;
    f32 sp46c;
    f32 sp468;
    f32 sp464;
    f32 sp460;

    f32 sp45c[1];
    f32 sp458[1];
    f32 sp454[1];
    f32 sp450[1];
    f32 sp44c[1];
    f32 sp448[1];

    f32 sp444;
    f32 sp440;

    f32 sp43c[1];
    f32 sp438[1];
    f32 sp434[1];
    f32 sp430[1];
    f32 sp42c[1];
    f32 sp428[1];
    f32 sp424[1];
    f32 sp420[1];

    f32 sp41c;
    f32 sp418;
    f32 sp414;
    f32 sp410;
    f32 sp40c;
    f32 sp408;
    f32 sp404;
    f32 sp400;

    f32 sp3fc[1];
    f32 sp3f8[1];
    f32 sp3f4[1];
    f32 sp3f0[1];
    f32 sp3ec[1];
    f32 sp3e8[1];
    f32 sp3e4[1];
    f32 sp3e0[1];

    f32 sp3dc[1];
    f32 sp3d8[1];
    f32 sp3d4[1];
    f32 sp3d0[1];
    f32 sp3cc[1];
    f32 sp3c8[1];
    f32 sp3c4[1];
    f32 sp3c0[1];

    f32 sp3bc[1];
    f32 sp3b8[1];
    f32 sp3b4[1];
    f32 sp3b0[1];
    f32 sp3ac[1];
    f32 sp3a8[1];
    f32 sp3a4[1];
    f32 sp3a0[1];
    f32 sp39c[1];
    f32 sp398[1];
    f32 sp394[1];
    f32 sp390[1];
    f32 sp38c[1];
    f32 sp388[1];
    f32 sp384[1];
    f32 sp380[1];
    f32 sp37c;
    f32 sp378;
    f32 sp374[1];
    f32 sp370[1];
    f32 sp36c[1];
    f32 sp368;
    f32 sp364[1];
    f32 sp360[1];
    f32 sp35c[1];
    f32 sp358[1];
    f32 sp354[1];
    f32 sp350[1];
    f32 sp34c[1];
    f32 sp348[1];
    f32 sp344[1];
    f32 sp340[1];
    f32 sp33c[1];
    f32 sp338[1];
    f32 sp334[1];
    f32 sp330[1];
    f32 sp310[8];
    f32 sp2f0[8];
    f32 sp2d0[8];
    f32 sp2b0[8];
    f32 sp290[8];
    f32 sp270[8];
    f32 sp250[8];
    f32 sp230[8];
    f32 sp210[8];
    f32 f2;
    f32 sp208[1];
    f32 sp204[1];
    f32 sp200[1];
    u32 stack[4];
    f32 sp1d0[8];
    f32 sp1b0[8];
    u32 stack2;
    f32 sp1a8[1];
    f32 sp1a4[1];
    f32 sp1a0[1];
    SkyRelated38 *swap1;
    SkyRelated38 *swap2;
    SkyRelated38 *swap3;
    f32 sp190[1];
    u32 stack3;

    if (skyVerticesAreTheSame(arg1, arg2) || skyVerticesAreTheSame(arg2, arg3) || skyVerticesAreTheSame(arg3, arg1))
    {
        return gdl;
    }

    sp378 = arg4 / 65536.0f;

    sp474 = arg2->unk28 - arg1->unk28;
    sp470 = arg2->unk2c - arg1->unk2c;
    sp46c = arg3->unk28 - arg1->unk28;
    sp468 = arg3->unk2c - arg1->unk2c;

    sp444 = ((sp46c * sp470) - (sp474 * sp468)) / 65536.0f;

    if (sp444 == 0.0f)
    {
        return gdl;
    }

    sp440 = 1.0f / sp444;

#ifdef GEVR
    {
        /* D176(a) Path B: re-emit as screen-space geometry (see skyPortRenderPoly).
         * skyRenderTri is only ever called textured in this file; `textured` is
         * honoured only in that the caller has set the textured combiner. */
        SkyRelated38 *portv[3];
        portv[0] = arg1;
        portv[1] = arg2;
        portv[2] = arg3;
        return skyPortRenderPoly(gdl, portv, 3);
    }
#else
    sp484 = arg1;
    sp480 = arg2;
    sp47c = arg3;

    if (sp480->unk2c < sp484->unk2c)
    {
        swap1 = sp480;
        sp480 = sp484;
        sp484 = swap1;

        sp444 *= -1.0f;
        sp440 *= -1.0f;
    }

    if (sp47c->unk2c < sp480->unk2c)
    {
        swap2 = sp480;
        sp480 = sp47c;
        sp47c = swap2;

        sp444 *= -1.0f;
        sp440 *= -1.0f;
    }

    if (sp480->unk2c < sp484->unk2c)
    {
        swap3 = sp480;
        sp480 = sp484;
        sp484 = swap3;

        sp444 *= -1.0f;
        sp440 *= -1.0f;
    }

    sp420[0] = sp480->unk28 * 0.25f;
    sp424[0] = 0.0f;
    sp428[0] = sp484->unk28 * 0.25f;
    sp42c[0] = 0.0f;
    sp430[0] = sp484->unk28 * 0.25f;
    sp434[0] = 0.0f;
    sp43c[0] = 0.0f;
    sp438[0] = 0.0f;

    sp448[0] = sp47c->unk28;
    sp44c[0] = sp47c->unk2c;
    sp450[0] = sp480->unk28;
    sp454[0] = sp480->unk2c;
    sp458[0] = sp484->unk28;
    sp45c[0] = sp484->unk2c;

    sp474 = sp450[0] - sp458[0];
    sp470 = sp454[0] - sp45c[0];
    sp46c = sp448[0] - sp458[0];
    sp468 = sp44c[0] - sp45c[0];
    sp464 = sp448[0] - sp450[0];
    sp460 = sp44c[0] - sp454[0];

    sp3fc[0] = 0.0f;
    sp3f8[0] = 0.0f;
    sp3dc[0] = 0.0f;
    sp3d8[0] = 0.0f;
    sp3a0[0] = 0.0f;
    sp3a8[0] = 0.0f;
    sp3b0[0] = 0.0f;
    sp3bc[0] = 0.0f;
    sp3b8[0] = 0.0f;
    sp380[0] = 0.0f;
    sp388[0] = 0.0f;
    sp390[0] = 0.0f;
    sp39c[0] = 0.0f;
    sp398[0] = 0.0f;

    sp3e0[0] = sp464 / 4.0f;
    sp3e4[0] = sp460 / 4.0f;
    sp3e8[0] = sp474 / 4.0f;
    sp3ec[0] = sp470 / 4.0f;
    sp3f0[0] = sp46c / 4.0f;
    sp3f4[0] = sp468 / 4.0f;

    sp3c0[0] = sp464 * 4.0f;
    sp3c4[0] = sp460 * 4.0f;
    sp3c8[0] = sp474 * 4.0f;
    sp3cc[0] = sp470 * 4.0f;
    sp3d0[0] = sp46c * 4.0f;
    sp3d4[0] = sp468 * 4.0f;

    sp3a4[0] = 4.0f / sp460;
    sp3ac[0] = 4.0f / sp470;
    sp3b4[0] = 4.0f / sp468;

    sp384[0] = sp464 / sp460;
    sp38c[0] = sp474 / sp470;
    sp394[0] = sp46c / sp468;

    sp384[0] = skyClamp(sp384[0], -1878.0f, 1877.0f);
    sp38c[0] = skyClamp(sp38c[0], -1878.0f, 1877.0f);
    sp394[0] = skyClamp(sp394[0], -1878.0f, 1877.0f);

    f2 = (sp484->unk2c * 0.25f);
    sp37c = f2 - (s32) f2;
    sp408 = sp428[0] - skyRound(sp38c[0] * 8192.0f) * (1.0f / 8192.0f) * sp37c;
    sp410 = sp430[0] - skyRound(sp394[0] * 8192.0f) * (1.0f / 8192.0f) * sp37c;

    gImmp1(gdl++, G_RDPHALF_1, (textured ? (G_TRI_SHADE_TXTR << 24) : (G_TRI_FILL << 24))
            | (sp444 < 0.0f ? 0x00800000 : 0)
            | (s32) sp47c->unk2c);
    gImmp1(gdl++, G_RDPHALF_CONT, (s32) sp480->unk2c << 16 | (s32) sp484->unk2c);

    gImmp1(gdl++, G_RDPHALF_1, sub_GAME_7F094298(sp480->unk28 * 0.25f));
    gImmp1(gdl++, G_RDPHALF_CONT, sub_GAME_7F094298(sp384[0]));

    gImmp1(gdl++, G_RDPHALF_1, sub_GAME_7F094298(sp410));
    gImmp1(gdl++, G_RDPHALF_CONT, sub_GAME_7F094298(sp394[0]));

    gImmp1(gdl++, G_RDPHALF_1, sub_GAME_7F094298(sp408));
    gImmp1(gdl++, G_RDPHALF_CONT, sub_GAME_7F094298(sp38c[0]));

    if (!textured)
    {
        return gdl;
    }

    sp36c[0] = sp484->unk0c * sp378;
    sp370[0] = sp480->unk0c * sp378;
    sp374[0] = sp47c->unk0c * sp378;

    sp368 = sp36c[0];

    if (sp370[0] < sp368) { sp368 = sp370[0]; }
    if (sp374[0] < sp368) { sp368 = sp374[0]; }

    sp368 *= 0.5f;

    sp35c[0] = sp484->unk34 * sp368;
    sp360[0] = sp480->unk34 * sp368;
    sp364[0] = sp47c->unk34 * sp368;

    sp338[0] = sp35c[0] * sp484->unk20;
    sp33c[0] = sp35c[0] * sp484->unk24;
    sp340[0] = sp35c[0] * 32767.0f;
    sp344[0] = sp360[0] * sp480->unk20;
    sp348[0] = sp360[0] * sp480->unk24;
    sp34c[0] = sp360[0] * 32767.0f;
    sp350[0] = sp364[0] * sp47c->unk20;
    sp354[0] = sp364[0] * sp47c->unk24;
    sp358[0] = sp364[0] * 32767.0f;

    sp330[0] = SKYABS(sp338[0]);
    sp334[0] = SKYABS(sp33c[0]);

    if (sp330[0] < SKYABS(sp344[0])) { sp330[0] = SKYABS(sp344[0]); }
    if (sp334[0] < SKYABS(sp348[0])) { sp334[0] = SKYABS(sp348[0]); }
    if (sp330[0] < SKYABS(sp350[0])) { sp330[0] = SKYABS(sp350[0]); }
    if (sp334[0] < SKYABS(sp354[0])) { sp334[0] = SKYABS(sp354[0]); }

    sp310[0] = sp484->r + 0.5f;
    sp310[1] = sp484->g + 0.5f;
    sp310[2] = sp484->b + 0.5f;
    sp310[3] = sp484->a + 0.5f;

    sp2f0[0] = sp480->r + 0.5f;
    sp2f0[1] = sp480->g + 0.5f;
    sp2f0[2] = sp480->b + 0.5f;
    sp2f0[3] = sp480->a + 0.5f;

    sp2d0[0] = sp47c->r + 0.5f;
    sp2d0[1] = sp47c->g + 0.5f;
    sp2d0[2] = sp47c->b + 0.5f;
    sp2d0[3] = sp47c->a + 0.5f;

    sp310[4] = sp338[0]; sp310[5] = sp33c[0]; sp310[6] = sp340[0];
    sp2f0[4] = sp344[0]; sp2f0[5] = sp348[0]; sp2f0[6] = sp34c[0];
    sp2d0[4] = sp350[0]; sp2d0[5] = sp354[0]; sp2d0[6] = sp358[0];

    sp310[7] = sp484->unk30;
    sp2f0[7] = sp480->unk30;
    sp2d0[7] = sp47c->unk30;

    for (i = 0; i < 8; i++)
    {
        sp2b0[i] = sp2f0[i] - sp310[i];
        sp290[i] = sp2d0[i] - sp310[i];
    }

    for (i = 0; i < 8; i++)
    {
        sp250[i] = (sp290[i] * sp3cc[0] - sp2b0[i] * sp3d4[0]) / 65536.0f;
        sp270[i] = (sp2b0[i] * sp3d0[0] - sp290[i] * sp3c8[0]) / 65536.0f;
        sp290[i] = sp250[i] * sp440;
        sp2b0[i] = sp270[i] * sp440;
        sp230[i] = sp2b0[i] + sp290[i] * sp394[0];
        sp210[i] = sp310[i] - sp230[i] * sp37c;
    }

    {
        u32 sp168;
        u32 sp164;
        u32 sp160;
        u32 sp15c;
        u32 sp158;
        u32 sp154;
        u32 sp150;
        u32 sp14c;
        u32 sp148;
        u32 sp144;
        u32 sp140;
        u32 sp13c;
        u32 sp138;
        u32 sp134;
        u32 sp130;
        u32 sp12c;

        sp168 = sub_GAME_7F094298(sp210[0]);
        sp164 = sub_GAME_7F094298(sp210[1]);
        sp160 = sub_GAME_7F094298(sp210[2]);
        sp15c = sub_GAME_7F094298(sp210[3]);

        sp158 = sub_GAME_7F094298(sp290[0]);
        sp154 = sub_GAME_7F094298(sp290[1]);
        sp150 = sub_GAME_7F094298(sp290[2]);
        sp14c = sub_GAME_7F094298(sp290[3]);

        sp138 = sub_GAME_7F094298(sp2b0[0]);
        sp134 = sub_GAME_7F094298(sp2b0[1]);
        sp130 = sub_GAME_7F094298(sp2b0[2]);
        sp12c = sub_GAME_7F094298(sp2b0[3]);

        sp148 = sub_GAME_7F094298(sp230[0]);
        sp144 = sub_GAME_7F094298(sp230[1]);
        sp140 = sub_GAME_7F094298(sp230[2]);
        sp13c = sub_GAME_7F094298(sp230[3]);

        gImmp1(gdl++, G_RDPHALF_1, (sp168 & 0xffff0000) | (sp164 & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (sp160 & 0xffff0000) | (sp15c & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (sp158 & 0xffff0000) | (sp154 & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (sp150 & 0xffff0000) | (sp14c & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (sp168 & 0x0000ffff) << 16 | (sp164 & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (sp160 & 0x0000ffff) << 16 | (sp15c & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (sp158 & 0x0000ffff) << 16 | (sp154 & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (sp150 & 0x0000ffff) << 16 | (sp14c & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (sp148 & 0xffff0000) | (sp144 & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (sp140 & 0xffff0000) | (sp13c & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (sp138 & 0xffff0000) | (sp134 & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (sp130 & 0xffff0000) | (sp12c & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (sp148 & 0x0000ffff) << 16 | (sp144 & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (sp140 & 0x0000ffff) << 16 | (sp13c & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (sp138 & 0x0000ffff) << 16 | (sp134 & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (sp130 & 0x0000ffff) << 16 | (sp12c & 0x0000ffff));
    }

    sp200[0] = sp330[0] / 32.0f;
    sp204[0] = sp334[0] / 32.0f;
    sp208[0] = sp368 / 32.0f;

    for (i = 0; i < 8; i++)
    {
        sp1d0[i] = SKYABS(sp290[i]) / 32.0f;
        sp1b0[i] = SKYABS(sp2b0[i]) / 32.0f;
    }

    sp1a0[0] = sp200[0] + (2 * sp1d0[4]) + sp1b0[4];
    sp1a4[0] = sp204[0] + (2 * sp1d0[5]) + sp1b0[5];
    sp1a8[0] = sp208[0] + (2 * sp1d0[6]) + sp1b0[6];

    if (sp1a0[0] < sp1a4[0]) { sp1a0[0] = sp1a4[0]; }
    if (sp1a0[0] < sp1a8[0]) { sp1a0[0] = sp1a8[0]; }

    sp1a0[0] /= 1024.0f;

    if (sp1a0[0] > 1.0f)
    {
        sp190[0] = 1.0f / sp1a0[0];
    }
    else
    {
        sp190[0] = 1.0f;
    }

    {
        u32 spe8;
        u32 spe4;
        u32 spe0;
        u32 spdc;
        u32 spd8;
        u32 spd4;
        u32 spd0;
        u32 spcc;
        u32 spc8;
        u32 spc4;
        u32 spc0;
        u32 spbc;
        u32 spb8;
        u32 spb4;
        u32 spb0;
        u32 spac;

        spe8 = sub_GAME_7F094298(sp210[4] * sp190[0]);
        spe4 = sub_GAME_7F094298(sp210[5] * sp190[0]);
        spe0 = sub_GAME_7F094298(sp210[6] * sp190[0]);
        spdc = 0;

        spd8 = sub_GAME_7F094298(sp290[4] * sp190[0]);
        spd4 = sub_GAME_7F094298(sp290[5] * sp190[0]);
        spd0 = sub_GAME_7F094298(sp290[6] * sp190[0]);
        spcc = 0;

        spb8 = sub_GAME_7F094298(sp2b0[4] * sp190[0]);
        spb4 = sub_GAME_7F094298(sp2b0[5] * sp190[0]);
        spb0 = sub_GAME_7F094298(sp2b0[6] * sp190[0]);
        spac = 0;

        spc8 = sub_GAME_7F094298(sp230[4] * sp190[0]);
        spc4 = sub_GAME_7F094298(sp230[5] * sp190[0]);
        spc0 = sub_GAME_7F094298(sp230[6] * sp190[0]);
        spbc = 0;

        gImmp1(gdl++, G_RDPHALF_1, (spe8 & 0xffff0000) | (spe4 & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (spe0 & 0xffff0000) | (spdc & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (spd8 & 0xffff0000) | (spd4 & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (spd0 & 0xffff0000) | (spcc & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (spe8 & 0x0000ffff) << 16 | (spe4 & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (spe0 & 0x0000ffff) << 16 | (spdc & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (spd8 & 0x0000ffff) << 16 | (spd4 & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (spd0 & 0x0000ffff) << 16 | (spcc & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (spc8 & 0xffff0000) | (spc4 & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (spc0 & 0xffff0000) | (spbc & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (spb8 & 0xffff0000) | (spb4 & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (spb0 & 0xffff0000) | (spac & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (spc8 & 0x0000ffff) << 16 | (spc4 & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (spc0 & 0x0000ffff) << 16 | (spbc & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (spb8 & 0x0000ffff) << 16 | (spb4 & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_2, (spb0 & 0x0000ffff) << 16 | (spac & 0x0000ffff));
    }
#endif

    return gdl;
}

/*
* Address: 0x7F098A2C
*/
Gfx *skyRenderFull(Gfx *gdl, SkyRelated38 *arg1, SkyRelated38 *arg2, SkyRelated38 *arg3, SkyRelated38 *arg4, f32 arg5)
{
    SkyRelated38 *sp4cc;
    SkyRelated38 *sp4c8;
    SkyRelated38 *sp4c4;
    s32 i;
    u32 stack;
    f32 sp4b8;
    f32 sp4b4;
    f32 sp4b0;
    f32 sp4ac;
    f32 sp4a8;
    f32 sp4a4;
    f32 sp4a0[1];
    f32 sp49c[1];
    f32 sp498[1];
    f32 sp494[1];
    f32 sp490[1];
    f32 sp48c[1];
    f32 sp488;
    f32 sp484;
    f32 sp480[1];
    f32 sp47c[1];
    f32 sp478[1];
    f32 sp474[1];
    f32 sp470[1];
    f32 sp46c[1];
    f32 sp468[1];
    f32 sp464[1];
    SkyRelated38 *swap1;
    SkyRelated38 *swap2;
    SkyRelated38 *swap3;
    f32 sp454[1];
    u32 stack07;
    f32 sp44c[1];
    u32 stack08;
    u32 stack09;
    f32 sp440[1];
    f32 sp43c[1];
    f32 sp438[1];
    f32 sp434[1];
    f32 sp430[1];
    f32 sp42c[1];
    f32 sp428[1];
    f32 sp424[1];
    f32 sp420[1];
    f32 sp41c[1];
    f32 sp418[1];
    f32 sp414[1];
    f32 sp410[1];
    f32 sp40c[1];
    f32 sp408[1];
    f32 sp404[1];
    f32 sp400[1];
    f32 sp3fc[1];
    f32 sp3f8[1];
    f32 sp3f4[1];
    f32 sp3f0[1];
    f32 sp3ec[1];
    f32 sp3e8[1];
    f32 sp3e4[1];
    f32 sp3e0[1];
    f32 sp3dc[1];
    f32 sp3d8[1];
    f32 sp3d4[1];
    f32 sp3d0[1];
    f32 sp3cc[1];
    f32 sp3c8[1];
    f32 sp3c4[1];
    f32 sp3c0;
    f32 sp3bc[1];
    f32 sp3b8[1];
    f32 sp3b4[1];
    f32 sp3b0[1];
    f32 sp3ac;
    f32 sp3a8[1];
    f32 sp3a4[1];
    f32 sp3a0[1];
    f32 sp39c[1];
    f32 sp398[1];
    f32 sp394[1];
    f32 sp390[1];
    f32 sp38c[1];
    f32 sp388[1];
    f32 sp384[1];
    f32 sp380[1];
    f32 sp37c[1];
    f32 sp378[1];
    f32 sp374[1];
    f32 sp370[1];
    f32 sp36c[1];
    f32 sp368[1];
    f32 sp364[1];
    f32 sp354[4];
    f32 sp334[8];
    f32 sp314[8];
    f32 sp2f4[8];
    f32 sp2d4[8];
    f32 sp2b4[8];
    f32 sp294[8];
    f32 sp274[8];
    f32 sp254[8];
    u32 stack10;
    u32 stack11;
    u32 stack12;
    u32 stack13;
    u32 stack00;
    f32 sp23c[1];
    f32 sp238[1];
    f32 sp234[1];
    f32 sp214[8];
    f32 sp1f4[8];
    u32 stack03;
    u32 stack04;
    u32 stack05;
    u32 stack06;
    u32 stack15;
    f32 sp1dc[1];
    f32 sp1d8[1];
    f32 sp1d4[1];
    u32 stack01;
    u32 stack02;
    u32 stack14;
    f32 sp1c4[1];
    u32 stack16;

    if (skyVerticesAreTheSame(arg1, arg2)
            || skyVerticesAreTheSame(arg2, arg3)
            || skyVerticesAreTheSame(arg3, arg1)
            || skyVerticesAreTheSame(arg4, arg1)
            || skyVerticesAreTheSame(arg4, arg2)
            || skyVerticesAreTheSame(arg4, arg3))
    {
        return gdl;
    }

    sp3c0 = arg5 / 65536.0f;

    sp4b8 = arg2->unk28 - arg1->unk28;
    sp4b4 = arg2->unk2c - arg1->unk2c;
    sp4b0 = arg3->unk28 - arg1->unk28;
    sp4ac = arg3->unk2c - arg1->unk2c;

    sp488 = ((sp4b0 * sp4b4) - (sp4b8 * sp4ac)) / 65536.0f;

    sp484 = 1.0f / sp488;

#ifdef GEVR
    {
        /* D176(a) Path B: re-emit the sky quad as screen-space geometry
         * (see skyPortRenderPoly). arg1..arg4 are TL,TR,BL,BR corners. */
        SkyRelated38 *portv[4];
        portv[0] = arg1;
        portv[1] = arg2;
        portv[2] = arg3;
        portv[3] = arg4;
        return skyPortRenderPoly(gdl, portv, 4);
    }
#else
    sp4cc = arg1;
    sp4c8 = arg2;
    sp4c4 = arg3;

    if (sp4c8->unk2c < sp4cc->unk2c)
    {
        swap1 = sp4c8;
        sp4c8 = sp4cc;
        sp4cc = swap1;

        sp488 *= -1.0f;
        sp484 *= -1.0f;
    }

    if (sp4c4->unk2c < sp4c8->unk2c)
    {
        swap2 = sp4c8;
        sp4c8 = sp4c4;
        sp4c4 = swap2;

        sp488 *= -1.0f;
        sp484 *= -1.0f;
    }

    if (sp4c8->unk2c < sp4cc->unk2c)
    {
        swap3 = sp4c8;
        sp4c8 = sp4cc;
        sp4cc = swap3;

        sp488 *= -1.0f;
        sp484 *= -1.0f;
    }

    sp464[0] = sp4c8->unk28 * 0.25f;
    sp468[0] = 0.0f;
    sp46c[0] = sp4cc->unk28 * 0.25f;
    sp470[0] = 0.0f;
    sp474[0] = sp4cc->unk28 * 0.25f;
    sp478[0] = 0.0f;
    sp480[0] = 0.0f;
    sp47c[0] = 0.0f;

    sp48c[0] = sp4c4->unk28;
    sp490[0] = sp4c4->unk2c;
    sp494[0] = sp4c8->unk28;
    sp498[0] = sp4c8->unk2c;
    sp49c[0] = sp4cc->unk28;
    sp4a0[0] = sp4cc->unk2c;

    sp4b8 = sp494[0] - sp49c[0];
    sp4b4 = sp498[0] - sp4a0[0];
    sp4b0 = sp48c[0] - sp49c[0];
    sp4ac = sp490[0] - sp4a0[0];
    sp4a8 = sp48c[0] - sp494[0];
    sp4a4 = sp490[0] - sp498[0];

    sp440[0] = 0.0f;
    sp43c[0] = 0.0f;
    sp420[0] = 0.0f;
    sp41c[0] = 0.0f;
    sp3e4[0] = 0.0f;
    sp3ec[0] = 0.0f;
    sp3f4[0] = 0.0f;
    sp400[0] = 0.0f;
    sp3fc[0] = 0.0f;
    sp3c4[0] = 0.0f;
    sp3cc[0] = 0.0f;
    sp3d4[0] = 0.0f;
    sp3e0[0] = 0.0f;
    sp3dc[0] = 0.0f;

    sp424[0] = sp4a8 / 4.0f;
    sp428[0] = sp4a4 / 4.0f;
    sp42c[0] = sp4b8 / 4.0f;
    sp430[0] = sp4b4 / 4.0f;
    sp434[0] = sp4b0 / 4.0f;
    sp438[0] = sp4ac / 4.0f;

    sp404[0] = sp4a8 * 4.0f;
    sp408[0] = sp4a4 * 4.0f;
    sp40c[0] = sp4b8 * 4.0f;
    sp410[0] = sp4b4 * 4.0f;
    sp414[0] = sp4b0 * 4.0f;
    sp418[0] = sp4ac * 4.0f;

    sp3e8[0] = 4.0f / sp4a4;
    sp3f0[0] = 4.0f / sp4b4;
    sp3f8[0] = 4.0f / sp4ac;

    sp3c8[0] = sp4a8 / sp4a4;
    sp3d0[0] = sp4b8 / sp4b4;
    sp3d8[0] = sp4b0 / sp4ac;

    sp3c8[0] = skyClamp(sp3c8[0], -1878.0f, 1877.0f);
    sp3d0[0] = skyClamp(sp3d0[0], -1878.0f, 1877.0f);
    sp3d8[0] = skyClamp(sp3d8[0], -1878.0f, 1877.0f);

    sp44c[0] = sp46c[0];
    sp454[0] = sp474[0];

    if (arg1->unk28 < arg2->unk28)
    {
        f32 sp1bc;

        if (arg3->unk2c - arg4->unk2c < 1.0f)
        {
            sp1bc = -1878.0f;
        }
        else
        {
            sp1bc = -(getPlayer_c_screenwidth() - 0.25f) / ((arg3->unk2c - arg4->unk2c) / 4.0f);
        }

        gImmp1(gdl++, G_RDPHALF_1, (G_TRI_SHADE_TXTR << 24) | 0x00800000 | (s32) arg3->unk2c);
        gImmp1(gdl++, G_RDPHALF_CONT, (s32) arg4->unk2c << 16 | (s32) arg1->unk2c);

        gImmp1(gdl++, G_RDPHALF_1, sub_GAME_7F094298(getPlayer_c_screenleft() + getPlayer_c_screenwidth() - 0.25f));
        gImmp1(gdl++, G_RDPHALF_CONT, sub_GAME_7F094298(sp1bc));

        gImmp1(gdl++, G_RDPHALF_1, sub_GAME_7F094298(getPlayer_c_screenleft()));
        gImmp1(gdl++, G_RDPHALF_CONT, sub_GAME_7F094298(0.0f));

        gImmp1(gdl++, G_RDPHALF_1, sub_GAME_7F094298(getPlayer_c_screenleft() + getPlayer_c_screenwidth() - 0.25f));
        gImmp1(gdl++, G_RDPHALF_CONT, sub_GAME_7F094298(0.0f));
    }
    else
    {
        f32 sp198;

        if (arg3->unk2c - arg4->unk2c < 1.0f)
        {
            sp198 = 1877.0f;
        }
        else
        {
            sp198 = (getPlayer_c_screenwidth() - 0.25f) / ((arg3->unk2c - arg4->unk2c) / 4.0f);
        }

        gImmp1(gdl++, G_RDPHALF_1, 0xce000000 | (s32) arg3->unk2c);
        gImmp1(gdl++, G_RDPHALF_CONT, (s32) arg4->unk2c << 16 | (s32) arg1->unk2c);

        gImmp1(gdl++, G_RDPHALF_1, sub_GAME_7F094298(getPlayer_c_screenleft()));
        gImmp1(gdl++, G_RDPHALF_CONT, sub_GAME_7F094298(sp198));

        gImmp1(gdl++, G_RDPHALF_1, sub_GAME_7F094298(getPlayer_c_screenleft() + getPlayer_c_screenwidth() - 0.25f));
        gImmp1(gdl++, G_RDPHALF_CONT, sub_GAME_7F094298(0.0f));

        gImmp1(gdl++, G_RDPHALF_1, sub_GAME_7F094298(getPlayer_c_screenleft()));
        gImmp1(gdl++, G_RDPHALF_CONT, sub_GAME_7F094298(0.0f));
    }

    sp3b0[0] = sp4cc->unk0c * sp3c0;
    sp3b4[0] = sp4c8->unk0c * sp3c0;
    sp3b8[0] = sp4c4->unk0c * sp3c0;
    sp3bc[0] = arg4->unk0c * sp3c0;

    sp3ac = sp3b0[0];

    if (sp3b4[0] < sp3ac) { sp3ac = sp3b4[0]; }
    if (sp3b8[0] < sp3ac) { sp3ac = sp3b8[0]; }
    if (sp3bc[0] < sp3ac) { sp3ac = sp3bc[0]; }

    sp3ac *= 0.5f;

    sp39c[0] = sp4cc->unk34 * sp3ac;
    sp3a0[0] = sp4c8->unk34 * sp3ac;
    sp3a4[0] = sp4c4->unk34 * sp3ac;
    sp3a8[0] = arg4->unk34 * sp3ac;

    sp36c[0] = sp39c[0] * sp4cc->unk20;
    sp370[0] = sp39c[0] * sp4cc->unk24;
    sp374[0] = sp39c[0] * 32767.0f;
    sp378[0] = sp3a0[0] * sp4c8->unk20;
    sp37c[0] = sp3a0[0] * sp4c8->unk24;
    sp380[0] = sp3a0[0] * 32767.0f;
    sp384[0] = sp3a4[0] * sp4c4->unk20;
    sp388[0] = sp3a4[0] * sp4c4->unk24;
    sp38c[0] = sp3a4[0] * 32767.0f;
    sp390[0] = sp3a8[0] * arg4->unk20;
    sp394[0] = sp3a8[0] * arg4->unk24;
    sp398[0] = sp3a8[0] * 32767.0f;

    sp364[0] = SKYABS(sp36c[0]);
    sp368[0] = SKYABS(sp370[0]);

    if (sp364[0] < SKYABS(sp378[0])) { sp364[0] = SKYABS(sp378[0]); }
    if (sp368[0] < SKYABS(sp37c[0])) { sp368[0] = SKYABS(sp37c[0]); }
    if (sp364[0] < SKYABS(sp384[0])) { sp364[0] = SKYABS(sp384[0]); }
    if (sp368[0] < SKYABS(sp388[0])) { sp368[0] = SKYABS(sp388[0]); }
    if (sp364[0] < SKYABS(sp390[0])) { sp364[0] = SKYABS(sp390[0]); }
    if (sp368[0] < SKYABS(sp394[0])) { sp368[0] = SKYABS(sp394[0]); }

    sp354[0] = sp36c[0]; sp354[1] = sp370[0]; sp354[2] = sp374[0];
    sp334[0] = sp378[0]; sp334[1] = sp37c[0]; sp334[2] = sp380[0];
    sp314[0] = sp384[0]; sp314[1] = sp388[0]; sp314[2] = sp38c[0];

    sp354[3] = sp4cc->unk30;
    sp334[3] = sp4c8->unk30;
    sp314[3] = sp4c4->unk30;

    for (i = 0; i < 4; i++)
    {
        sp2f4[i] = sp334[i] - sp354[i];
        sp2d4[i] = sp314[i] - sp354[i];
    }

    for (i = 0; i < 4; i++)
    {
        sp294[i] = ((sp2d4[i] * sp410[0]) - (sp2f4[i] * sp418[0])) / 65536.0f;
        sp2b4[i] = ((sp2f4[i] * sp414[0]) - (sp2d4[i] * sp40c[0])) / 65536.0f;
        sp2d4[i] = sp294[i] * sp484;
        sp2f4[i] = sp2b4[i] * sp484;
        sp274[i] = sp2b4[i] * sp484;
        sp254[i] = sp354[i];
    }

    {
        f32 mult = arg4->unk2c / arg3->unk2c;

        f32 sp170 = arg4->r + ((arg1->r - arg3->r) * mult);
        f32 sp16c = arg4->g + ((arg1->g - arg3->g) * mult);
        f32 sp168 = arg4->b + ((arg1->b - arg3->b) * mult);
        f32 sp164 = arg4->a + ((arg1->a - arg3->a) * mult);

        u32 sp160 = arg1->r * 65536.0f;
        u32 sp15c = arg1->g * 65536.0f;
        u32 sp158 = arg1->b * 65536.0f;
        u32 sp154 = arg1->a * 65536.0f;

        u32 sp150 = sub_GAME_7F094298((sp170 - arg1->r) / ((arg2->unk28 - arg1->unk28) * 0.25f));
        u32 sp14c = sub_GAME_7F094298((sp16c - arg1->g) / ((arg2->unk28 - arg1->unk28) * 0.25f));
        u32 sp148 = sub_GAME_7F094298((sp168 - arg1->b) / ((arg2->unk28 - arg1->unk28) * 0.25f));
        u32 sp144 = sub_GAME_7F094298((sp164 - arg1->a) / ((arg2->unk28 - arg1->unk28) * 0.25f));

        u32 sp140;
        u32 sp13c;
        u32 sp138;
        u32 sp134;
        u32 sp130;
        u32 sp12c;
        u32 sp128;
        u32 sp124;

        sp140 = sp130 = sub_GAME_7F094298((arg3->r - arg1->r) / ((arg3->unk2c - arg1->unk2c) * 0.25f));
        sp13c = sp12c = sub_GAME_7F094298((arg3->g - arg1->g) / ((arg3->unk2c - arg1->unk2c) * 0.25f));
        sp138 = sp128 = sub_GAME_7F094298((arg3->b - arg1->b) / ((arg3->unk2c - arg1->unk2c) * 0.25f));
        sp124 = sp134 = sub_GAME_7F094298((arg3->a - arg1->a) / ((arg3->unk2c - arg1->unk2c) * 0.25f));

        gImmp1(gdl++, G_RDPHALF_1, (sp160 & 0xffff0000) | (sp15c & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (sp158 & 0xffff0000) | (sp154 & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (sp150 & 0xffff0000) | (sp14c & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (sp148 & 0xffff0000) | (sp144 & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (sp160 & 0x0000ffff) << 16 | (sp15c & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (sp158 & 0x0000ffff) << 16 | (sp154 & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (sp150 & 0x0000ffff) << 16 | (sp14c & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (sp148 & 0x0000ffff) << 16 | (sp144 & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (sp140 & 0xffff0000) | (sp13c & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (sp138 & 0xffff0000) | (sp134 & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (sp130 & 0xffff0000) | (sp12c & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (sp128 & 0xffff0000) | (sp124 & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (sp140 & 0x0000ffff) << 16 | (sp13c & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (sp138 & 0x0000ffff) << 16 | (sp134 & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (sp130 & 0x0000ffff) << 16 | (sp12c & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (sp128 & 0x0000ffff) << 16 | (sp124 & 0x0000ffff));
    }

    sp234[0] = sp364[0] / 32.0f;
    sp238[0] = sp368[0] / 32.0f;
    sp23c[0] = sp3ac / 32.0f;

    for (i = 0; i < 4; i++)
    {
        sp214[i] = SKYABS(sp2d4[i]) / 32.0f;
        sp1f4[i] = SKYABS(sp2f4[i]) / 32.0f;
    }

    sp1d4[0] = sp234[0] + (2 * sp214[0]) + sp1f4[0];
    sp1d8[0] = sp238[0] + (2 * sp214[1]) + sp1f4[1];
    sp1dc[0] = sp23c[0] + (2 * sp214[2]) + sp1f4[2];

    if (sp1d4[0] < sp1d8[0]) { sp1d4[0] = sp1d8[0]; }
    if (sp1d4[0] < sp1dc[0]) { sp1d4[0] = sp1dc[0]; }

    sp1d4[0] *= (1.0f / 1024.0f);

    if (sp1d4[0] > 1.0f)
    {
        sp1c4[0] = 1.0f / sp1d4[0];
    }
    else
    {
        sp1c4[0] = 1.0f;
    }

    {
        u32 spe0;
        u32 spdc;
        u32 spd8;
        u32 spd4;
        u32 spd0;
        u32 spcc;
        u32 spc8;
        u32 spc4;
        u32 spc0;
        u32 spbc;
        u32 spb8;
        u32 spb4;
        u32 spb0;
        u32 spac;
        u32 spa8;
        u32 spa4;

        spe0 = sub_GAME_7F094298(sp254[0] * sp1c4[0]);
        spdc = sub_GAME_7F094298(sp254[1] * sp1c4[0]);
        spd8 = sub_GAME_7F094298(sp254[2] * sp1c4[0]);
        spd4 = sub_GAME_7F094298(sp254[3] * sp1c4[0]);

        spd0 = sub_GAME_7F094298(sp2d4[0] * sp1c4[0]);
        spcc = sub_GAME_7F094298(sp2d4[1] * sp1c4[0]);
        spc8 = sub_GAME_7F094298(sp2d4[2] * sp1c4[0]);
        spc4 = sub_GAME_7F094298(sp2d4[3] * sp1c4[0]);

        spb0 = sub_GAME_7F094298(sp2f4[0] * sp1c4[0]);
        spac = sub_GAME_7F094298(sp2f4[1] * sp1c4[0]);
        spa8 = sub_GAME_7F094298(sp2f4[2] * sp1c4[0]);
        spa4 = sub_GAME_7F094298(sp2f4[3] * sp1c4[0]);

        spc0 = sub_GAME_7F094298(sp274[0] * sp1c4[0]);
        spbc = sub_GAME_7F094298(sp274[1] * sp1c4[0]);
        spb8 = sub_GAME_7F094298(sp274[2] * sp1c4[0]);
        spb4 = sub_GAME_7F094298(sp274[3] * sp1c4[0]);

        gImmp1(gdl++, G_RDPHALF_1, (spe0 & 0xffff0000) | (spdc & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (spd8 & 0xffff0000) | (spd4 & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (spd0 & 0xffff0000) | (spcc & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (spc8 & 0xffff0000) | (spc4 & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (spe0 & 0x0000ffff) << 16 | (spdc & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (spd8 & 0x0000ffff) << 16 | (spd4 & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (spd0 & 0x0000ffff) << 16 | (spcc & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (spc8 & 0x0000ffff) << 16 | (spc4 & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (spc0 & 0xffff0000) | (spbc & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (spb8 & 0xffff0000) | (spb4 & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (spb0 & 0xffff0000) | (spac & 0xffff0000) >> 16);
        gImmp1(gdl++, G_RDPHALF_CONT, (spa8 & 0xffff0000) | (spa4 & 0xffff0000) >> 16);

        gImmp1(gdl++, G_RDPHALF_1, (spc0 & 0x0000ffff) << 16 | (spbc & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_CONT, (spb8 & 0x0000ffff) << 16 | (spb4 & 0x0000ffff));

        gImmp1(gdl++, G_RDPHALF_1, (spb0 & 0x0000ffff) << 16 | (spac & 0x0000ffff));
        gImmp1(gdl++, G_RDPHALF_2, (spa8 & 0x0000ffff) << 16 | (spa4 & 0x0000ffff));
    }
#endif

    return gdl;
}
