#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "net_bot_ai.h"
#include "net_core.h"
#include "net_protocol.h"
#include "net_match.h"
#include "net_game.h"
#include "system.h"
#include <ultra64.h>
#include <bondtypes.h>
#include "game/player.h"
#include "game/bondview.h"
#include "game/gun.h"
#include "game/propobj.h"
#include "game/objecthandler.h"
#include "game/stan.h"

#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif

enum {
    BOT_STATE_ROAMING = 0,
    BOT_STATE_ENGAGING = 1,
    BOT_STATE_DEAD = 2
};

typedef struct {
    int state;
    coord3d target_pos;
    int target_enemy;
    uint64_t next_scan_us;
    uint64_t next_burst_us;
    uint64_t burst_end_us;
    uint64_t death_time_us;
    uint64_t next_strafe_toggle_us;
    float current_yaw;
    float current_pitch;
    float strafe_dir;
    int pad_target_idx;
} BotAiSlot;

static BotAiSlot s_bots[GEVR_MAX_PLAYERS];

extern void netHostBroadcastBotMove(int bot_slot, const struct netplayermove *input);
extern void netHostApplyBotHit(uint8_t bot_slot, uint8_t target_slot, uint8_t weapon_id, float damage, float hit_x, float hit_z);
extern void netHostRespawnBot(int slot);

void netBotAiInit(void) {
    memset(s_bots, 0, sizeof(s_bots));
    for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
        s_bots[i].target_enemy = -1;
        s_bots[i].strafe_dir = (i % 2 == 0) ? 1.0f : -1.0f;
    }
}

void netBotAiReset(void) {
    netBotAiInit();
}

static float normalize_angle(float a) {
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

void netBotAiTick(void) {
    if (!netIsHost() || (netGetPhase() != NET_PHASE_WARMUP && netGetPhase() != NET_PHASE_IN_PROGRESS)) {
        return;
    }
    extern s32 g_StageNum;
    if (g_StageNum == 90 || netCoopActive()) {
        return;
    }

    uint64_t now = sysGetMicroseconds();
    int difficulty = gevrNetConfigGet(CFG_BOT_DIFFICULTY);
    if (difficulty < 0 || difficulty >= NET_BOT_DIFF_COUNT) difficulty = NET_BOT_DIFF_MEDIUM;

    float turn_speed = (difficulty == NET_BOT_DIFF_EASY) ? 4.0f : (difficulty == NET_BOT_DIFF_MEDIUM ? 8.0f : 16.0f);
    float aim_err_mag = (difficulty == NET_BOT_DIFF_EASY) ? 9.0f : (difficulty == NET_BOT_DIFF_MEDIUM) ? 4.5f : 1.2f;

    for (int slot = 0; slot < GEVR_MAX_PLAYERS; slot++) {
        if (!netIsBotSlot(slot)) continue;
        struct player *pl = g_playerPointers[slot];
        if (!pl || !pl->prop) continue;

        BotAiSlot *bot = &s_bots[slot];

        /* Check death and respawn */
        if (pl->bonddead) {
            if (bot->state != BOT_STATE_DEAD) {
                bot->state = BOT_STATE_DEAD;
                bot->death_time_us = now;
            } else if (now - bot->death_time_us > 2500000ull) {
                netHostRespawnBot(slot);
                bot->state = BOT_STATE_ROAMING;
                bot->target_enemy = -1;
                bot->current_yaw = pl->vv_theta;
                bot->death_time_us = 0;
            }
            continue;
        }

        /* Periodic enemy scan */
        if (now >= bot->next_scan_us) {
            bot->next_scan_us = now + 200000ull;
            int best_target = -1;
            float best_dist = 2500.0f; /* 25 meters detection range */

            for (int t = 0; t < GEVR_MAX_PLAYERS; t++) {
                if (t == slot || !netSlotOccupied(t) || netSlotIsSpectator(t)) continue;
                struct player *tpl = g_playerPointers[t];
                if (!tpl || !tpl->prop || tpl->bonddead) continue;

                /* Team check */
                if (netScenarioHasTeams(gevrNetConfigGet(CFG_SCENARIO))) {
                    if (netGetSlotTeam(slot) == netGetSlotTeam(t) && netGetSlotTeam(slot) != NET_TEAM_NONE) continue;
                }

                float dx = tpl->prop->pos.x - pl->prop->pos.x;
                float dy = tpl->prop->pos.y - pl->prop->pos.y;
                float dz = tpl->prop->pos.z - pl->prop->pos.z;
                float d = sqrtf(dx * dx + dy * dy + dz * dz);
                if (d < best_dist) {
                    best_dist = d;
                    best_target = t;
                }
            }
            bot->target_enemy = best_target;
            if (best_target >= 0) bot->state = BOT_STATE_ENGAGING;
            else if (bot->state == BOT_STATE_ENGAGING) bot->state = BOT_STATE_ROAMING;
        }

        /* Movement & aiming calculations */
        float fwd_speed = 0.0f;
        float side_speed = 0.0f;
        float desired_yaw = bot->current_yaw;
        float desired_pitch = bot->current_pitch;
        uint32_t ucmd = 0;

        if (bot->state == BOT_STATE_ENGAGING && bot->target_enemy >= 0) {
            struct player *tpl = g_playerPointers[bot->target_enemy];
            if (!tpl || !tpl->prop || tpl->bonddead) {
                bot->target_enemy = -1;
                bot->state = BOT_STATE_ROAMING;
            } else {
                float dx = tpl->prop->pos.x - pl->prop->pos.x;
                float dy = tpl->prop->pos.y - pl->prop->pos.y;
                float dz = tpl->prop->pos.z - pl->prop->pos.z;
                float dist2d = sqrtf(dx * dx + dz * dz);

                desired_yaw = atan2f(-dx, dz) * (180.0f / M_PI_F);
                desired_pitch = atan2f(dy, dist2d > 1.0f ? dist2d : 1.0f) * (180.0f / M_PI_F);

                /* Turn towards target */
                float diff_yaw = normalize_angle(desired_yaw - bot->current_yaw);
                if (fabsf(diff_yaw) > turn_speed) {
                    bot->current_yaw += (diff_yaw > 0 ? turn_speed : -turn_speed);
                } else {
                    bot->current_yaw = desired_yaw;
                }
                bot->current_pitch = desired_pitch;

                /* Movement relative to enemy */
                if (dist2d > 500.0f) fwd_speed = 0.75f;
                else if (dist2d < 180.0f) fwd_speed = -0.4f;

                /* Strafe toggle */
                if (now >= bot->next_strafe_toggle_us) {
                    bot->strafe_dir = -bot->strafe_dir;
                    bot->next_strafe_toggle_us = now + 1000000ull + (uint64_t)(rand() % 1000000);
                }
                if (difficulty >= NET_BOT_DIFF_MEDIUM) {
                    side_speed = (difficulty == NET_BOT_DIFF_HARD ? 0.6f : 0.35f) * bot->strafe_dir;
                }

                /* Firing burst logic */
                if (fabsf(diff_yaw) < 22.0f) {
                    if (now >= bot->next_burst_us && now >= bot->burst_end_us) {
                        uint64_t burst_len = (difficulty == NET_BOT_DIFF_EASY) ? 350000ull :
                                             (difficulty == NET_BOT_DIFF_MEDIUM ? 650000ull : 1100000ull);
                        uint64_t pause_len = (difficulty == NET_BOT_DIFF_EASY) ? 750000ull :
                                             (difficulty == NET_BOT_DIFF_MEDIUM ? 400000ull : 180000ull);
                        bot->burst_end_us = now + burst_len;
                        bot->next_burst_us = bot->burst_end_us + pause_len;
                    }

                    if (now < bot->burst_end_us) {
                        ucmd |= UCMD_FIRE | UCMD_AIMVALID;

                        /* Hit registration check (chance per 150ms) */
                        static uint64_t s_last_hit_check_us[GEVR_MAX_PLAYERS];
                        if (now - s_last_hit_check_us[slot] > 140000ull) {
                            s_last_hit_check_us[slot] = now;
                            int hit_rate = (difficulty == NET_BOT_DIFF_EASY) ? 28 :
                                           (difficulty == NET_BOT_DIFF_MEDIUM ? 52 : 78);
                            if ((rand() % 100) < hit_rate) {
                                int weapon = pl->hands[GUNRIGHT].weaponnum;
                                if (weapon <= ITEM_UNARMED || weapon >= ITEM_IDS_MAX) weapon = ITEM_AK47;
                                float dmg = 12.0f;
                                netHostApplyBotHit((uint8_t)slot, (uint8_t)bot->target_enemy, (uint8_t)weapon, dmg, tpl->prop->pos.x, tpl->prop->pos.z);
                            }
                        }
                    }
                }
            }
        } else {
            /* Roaming mode: navigate around start pads */
            extern s32 startpadcount;
            extern PadRecord *g_Startpad[16];
            if (startpadcount > 0) {
                float dx = bot->target_pos.x - pl->prop->pos.x;
                float dz = bot->target_pos.z - pl->prop->pos.z;
                float dist = sqrtf(dx * dx + dz * dz);

                if (dist < 100.0f || (bot->target_pos.x == 0.0f && bot->target_pos.z == 0.0f)) {
                    /* Pick new waypoint */
                    bot->pad_target_idx = (bot->pad_target_idx + 1 + (rand() % 3)) % startpadcount;
                    if (g_Startpad[bot->pad_target_idx]) {
                        bot->target_pos = g_Startpad[bot->pad_target_idx]->pos;
                    } else {
                        bot->target_pos.x = pl->prop->pos.x + ((rand() % 600) - 300);
                        bot->target_pos.z = pl->prop->pos.z + ((rand() % 600) - 300);
                    }
                }

                dx = bot->target_pos.x - pl->prop->pos.x;
                dz = bot->target_pos.z - pl->prop->pos.z;
                desired_yaw = atan2f(-dx, dz) * (180.0f / M_PI_F);
                float diff_yaw = normalize_angle(desired_yaw - bot->current_yaw);
                if (fabsf(diff_yaw) > turn_speed) {
                    bot->current_yaw += (diff_yaw > 0 ? turn_speed : -turn_speed);
                } else {
                    bot->current_yaw = desired_yaw;
                }
                fwd_speed = 0.8f;
            }
        }

        bot->current_yaw = normalize_angle(bot->current_yaw);

        /* Update bot world position using ground collision */
        float fwd = fwd_speed * 3.2f;
        float strafe = side_speed * 2.2f;
        float rad = bot->current_yaw * (M_PI_F / 180.0f);
        float nx = pl->prop->pos.x - sinf(rad) * fwd + cosf(rad) * strafe;
        float nz = pl->prop->pos.z + cosf(rad) * fwd + sinf(rad) * strafe;

        coord3d test_pos = { nx, pl->prop->pos.y, nz };
        f32 floory = 0.0f;
        s32 incentre = 0;
        f32 col_radius = pl->field_488.collision_radius > 5.0f ? pl->field_488.collision_radius : 18.0f;
        StandTile *tile = stanFindGroundAtCyl(&test_pos, col_radius, NULL, pl->field_488.current_tile_ptr, &floory, &incentre);

        if (tile && incentre && floory > -8000.0f && fabsf(floory - (pl->prop->pos.y - pl->eyeheight)) < 70.0f) {
            pl->prop->pos.x = nx;
            pl->prop->pos.z = nz;
            pl->prop->pos.y = floory + pl->eyeheight;
            pl->pos = pl->prop->pos;
            pl->field_488.collision_position = pl->prop->pos;
            pl->field_488.pos = pl->prop->pos;
            pl->field_488.current_tile_ptr = tile;
            pl->field_488.current_tile_ptr_for_portals = tile;
            pl->prop->stan = tile;
        }

        /* Orient character model */
        pl->vv_theta = bot->current_yaw;
        pl->vv_verta = bot->current_pitch;
        pl->speedforwards = fwd_speed;
        pl->speedsideways = side_speed;

        /* Prepare netplayermove for broadcast */
        struct netplayermove move;
        memset(&move, 0, sizeof(move));
        move.tick = (u32)(now / 1000);
        move.clock_us = now;
        move.ucmd = ucmd;
        move.movespeed[0] = fwd_speed;
        move.movespeed[1] = side_speed;
        move.angles[0] = bot->current_yaw;
        move.angles[1] = bot->current_pitch;
        move.weaponnum = (s8)getCurrentPlayerWeaponId(GUNRIGHT);
        if (move.weaponnum <= ITEM_UNARMED || move.weaponnum >= ITEM_IDS_MAX) {
            move.weaponnum = ITEM_AK47;
        }
        move.weaponnum_left = ITEM_UNARMED;
        move.crouchpos = 0;
        move.health = pl->bondhealth;
        move.armour = pl->bondarmour;
        move.dead = 0;
        move.pos = pl->prop->pos;

        /* Barrel origin & direction */
        float pitch_rad = bot->current_pitch * (M_PI_F / 180.0f);
        move.aimorigin = pl->prop->pos;
        move.aimorigin.y += 12.0f;
        move.aimdir.x = -sinf(rad) * cosf(pitch_rad);
        move.aimdir.y = sinf(pitch_rad);
        move.aimdir.z = cosf(rad) * cosf(pitch_rad);

        netHostBroadcastBotMove(slot, &move);
    }
}
