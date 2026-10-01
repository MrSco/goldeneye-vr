#ifdef GEVR
#include "net_game.h"
#endif
#include <ultra64.h>
#include "math_atan2f.h"
#include "textrelated.h"
#include "front.h"
#include "fr.h"
#include "player.h"
#include "othermodemicrocode.h"
#include "image_bank.h"
#ifdef GEVR
extern bool netIsActive(void);
#endif


/**
 * NTSC address 0x7F0C6090.
 * PAL address 0x7F0C55A0.
*/

Gfx *display_red_blue_on_radar(Gfx *DL)
{
    s32 padding[4];
    s32 player_count;
    s32 cur_playernum;
    enum MPSCENARIOS current_scenario;
    
    s32 start_left;
    s32 start_top;
    s32 dl_color_1;
    s32 i;
    PropRecord *other_player_prop;
    PropRecord *player_prop;

    f32 temp_f20;
    f32 temp_f22;
    s32 temp_f24;
    f32 temp_f28;
    f32 temp_f2;
    s32 dl_color_2;
    s32 loop_start_left;
    s32 loop_start_top;
    f32 temp_f16;
    s32 radar_scale = 1;

#if defined(VERSION_EU)
    #define RADAR_TOP_OFFSET 0x1d
    #define RADAR_RECT1_OFFSET 0x13
    #define RADAR_RECT1_D 0x355
    #define RADAR_VERT_SCALE 1.2f
#else
    #define RADAR_TOP_OFFSET 0x1a
    #define RADAR_RECT1_OFFSET 0x10
    #define RADAR_RECT1_D 0x400
    #define RADAR_VERT_SCALE 1.0f
#endif
    
    current_scenario = get_scenario();
    cur_playernum = get_cur_playernum();
    player_count = getPlayerCount();
    
    if (player_count == 1)
    {
        return DL;
    }
    
    if ((g_CurrentPlayer->mpmenuon != FALSE) || (g_CurrentPlayer->bonddead != FALSE))
    {
        return DL;
    }
    
    if (cheatIsActive(CHEAT_NO_RADAR_MP) != 0)
    {
        return DL;
    }

    start_left = (viGetViewLeft() + viGetViewWidth()) - 0x29;
    start_top = viGetViewTop() + RADAR_TOP_OFFSET;
#ifdef GEVR
    {
        extern s32 g_gevrStereo;
        if (g_gevrStereo)
        {
            /* Keep the radar within the comfortable centre of the HUD quad. */
            start_left -= 32;
            start_top += 20;
            radar_scale = 2;
        }
    }
#endif
    if ((player_count >= 3) && !(cur_playernum & 1))
    {
#ifdef GEVR
        extern s32 g_gevrStereo;
        if (!g_gevrStereo)
#endif
        start_left += 0xF;
    }
    
    texSelect(&DL, mpradarimages, 2, 0, 2);

    DL = microcode_constructor(DL);

    gDPSetCombineLERP(DL++, 0, 0, 0, PRIMITIVE, PRIMITIVE, 0, TEXEL0, 0, 0, 0, 0, PRIMITIVE, PRIMITIVE, 0, TEXEL0, 0);
    gDPSetPrimColor(DL++, 0, 0, 0x00, 0x00, 0x00, 0xA0);

    gSPTextureRectangle(
        DL++,
        (start_left - 0x10 * radar_scale) << 2,
        (start_top - RADAR_RECT1_OFFSET * radar_scale) << 2,
        (start_left + 0x10 * radar_scale) << 2,
        (start_top + RADAR_RECT1_OFFSET * radar_scale) << 2,
        G_TX_RENDERTILE,
        0x10,
        0x10,
        0x400 / radar_scale,
        RADAR_RECT1_D / radar_scale);
    
    DL = microcode_constructor_related_to_menus(DL, start_left - 2 * radar_scale, start_top - 2 * radar_scale,
                                                  start_left + 2 * radar_scale, start_top + 2 * radar_scale, 0x40);
    
    if ((current_scenario == SCENARIO_2v2)
        || (current_scenario == SCENARIO_3v1)
        || (current_scenario == SCENARIO_2v1)
        || (current_scenario == SCENARIO_TLD)
        || (current_scenario == SCENARIO_MWTGG))
    {
        if (g_playerPlayerData[cur_playernum].have_token_or_goldengun == 0)
        {
            dl_color_1 = 0xFF7777FF;
        }
        else
        {
            dl_color_1 = 0x8888FFFF;
        }
        
        DL = microcode_constructor_related_to_menus(DL, start_left - radar_scale, start_top - radar_scale,
                                                      start_left + radar_scale, start_top + radar_scale, dl_color_1);
    }
    else
    {
        DL = microcode_constructor_related_to_menus(DL, start_left - radar_scale, start_top - radar_scale,
                                                      start_left + radar_scale, start_top + radar_scale, -0x60);
    }

    for (i = 0; i < player_count; i++)
    {
#ifdef GEVR
        {
            extern bool netIsActive(void);
            extern bool netSlotOccupied(int slot);
            extern bool netIsRemotePlayerActive(int slot);
            if (netIsActive() && (i == netSpectatorTarget() || !netSlotOccupied(i) ||
                (i != cur_playernum && !netIsRemotePlayerActive(i)))) continue;
        }
#endif
        if (i != cur_playernum)
        {
            if (g_playerPointers[i] && g_playerPointers[i]->prop &&
                g_playerPointers[i]->bonddead == FALSE)
            {
                f32 tt1;
                other_player_prop = g_playerPointers[i]->prop;
                player_prop = g_CurrentPlayer->prop;

                temp_f20 = other_player_prop->pos.f[0] - player_prop->pos.f[0];
                temp_f22 = other_player_prop->pos.f[2] - player_prop->pos.f[2];
                
                temp_f28 = ((atan2f(temp_f20, temp_f22) * 180.0f) / M_PI_F) + g_CurrentPlayer->vv_theta + 180.0f;
                
                temp_f24 = 16 * radar_scale;
#ifdef GEVR
                temp_f16 = NET_RADAR_BRIGHT_RANGE;
#else
                temp_f16 = 4000;
#endif

                tt1 = (temp_f24 / temp_f16);
                temp_f2 = sqrtf((temp_f20 * temp_f20) + (temp_f22 * temp_f22)) * tt1;
            
                if ((current_scenario == SCENARIO_2v2)
                    || (current_scenario == SCENARIO_3v1)
                    || (current_scenario == SCENARIO_2v1)
                    || (current_scenario == SCENARIO_TLD)
                    || (current_scenario == SCENARIO_MWTGG))
                {
                    if (temp_f2 < temp_f24)
                    {
                        if (g_playerPlayerData[i].have_token_or_goldengun == 0)
                        {
                            dl_color_2 = 0xFF0000A0;
                        }
                        else
                        {
                            dl_color_2 = 0x2828FFFF;
                        }
                    }
                    else
                    {
                        temp_f2 = temp_f24;
                        if (g_playerPlayerData[i].have_token_or_goldengun == 0)
                        {
                            dl_color_2 = 0xFF000060;
                        }
                        else
                        {
                            dl_color_2 = 0x2828FFB0;
                        }
                    }
                }
                else
                {
                    dl_color_2 = 0xFFFF0060;
                    if (temp_f2 < temp_f24)
                    {
                        dl_color_2 = 0xFFFF00A0;
                    }
                    else
                    {
                        temp_f2 = temp_f24;
                    }
                }

                // 0.017453292f = DegToRad(1)
                loop_start_left = (s32) (sinf(temp_f28 * 0.017453292f) * temp_f2) + start_left;
                loop_start_top = (s32) (cosf(temp_f28 * 0.017453292f) * temp_f2 * RADAR_VERT_SCALE) + start_top;
                
                DL = microcode_constructor_related_to_menus(DL, loop_start_left - 2 * radar_scale,
                    loop_start_top - 2 * radar_scale, loop_start_left + 2 * radar_scale,
                    loop_start_top + 2 * radar_scale, 0x40);
                DL = microcode_constructor_related_to_menus(DL, loop_start_left - radar_scale,
                    loop_start_top - radar_scale, loop_start_left + radar_scale,
                    loop_start_top + radar_scale, dl_color_2);
            }
        }
    }

#ifdef GEVR
    if (netIsActive()) {
        extern Gfx *gevrRenderRadarGauges(Gfx *, s32, s32, s32);
        DL = gevrRenderRadarGauges(DL, start_left, start_top, 16 * radar_scale);
    }
#endif
    return combiner_bayer_lod_perspective(DL);

    #undef RADAR_TOP_OFFSET
    #undef RADAR_RECT1_OFFSET
    #undef RADAR_RECT1_D
    #undef RADAR_VERT_SCALE
}
