#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../vr/vr_openxr.h"
#include "net_player_sync.h"
#include "net_core.h"
#include "net_protocol.h"
#include "net_voice.h"
#include "system.h"
#include <ultra64.h>
#include <bondtypes.h>
#include "game/player.h"
#include "game/bondview.h"
#include "game/gun.h"
#include "game/propobj.h"
#include "game/objecthandler.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif

extern bool get_button_state(int controllerIndex, const char* buttonName);
extern enum PROP getPropForHeldItem(ITEM_IDS arg0);
static coord3d s_remote_render_pos[GEVR_MAX_PLAYERS];
static bool s_remote_render_valid[GEVR_MAX_PLAYERS];

void netPlayerSyncInit(void) {
}

void netPlayerSyncBeforeTick(s32 playernum) {
    if (!netIsActive()) return;
    
    int local_slot = netGetLocalSlot();
    if (playernum == local_slot) {
        return;
    }
    if (playernum < 0 || playernum >= GEVR_MAX_PLAYERS) return;
    s_remote_render_valid[playernum] = false;
    
    if (netIsRemotePlayerActive(playernum) && g_playerPointers[playernum]) {
        const struct netplayermove *m = netGetRemotePlayerMove(playernum);
        struct player *pl = g_playerPointers[playernum];
        if (!pl || !m) return;
        
        if (pl->prop) {
            /* A corpse stays at its death location until the reliable respawn
             * event resets the player. Late movement packets must not drag it. */
            if (pl->bonddead) {
                pl->speedforwards = 0.0f;
                pl->speedsideways = 0.0f;
                pl->hands[GUNRIGHT].field_87D = 0;
                return;
            }
            /* Position: check for huge delta or initial snap */
            float dx = m->pos.x - pl->prop->pos.x;
            float dy = m->pos.y - pl->prop->pos.y;
            float dz = m->pos.z - pl->prop->pos.z;
            float dist_sq = dx*dx + dy*dy + dz*dz;
            
            if (dist_sq > (512.0f * 512.0f) || dist_sq < 0.0001f || pl->deathanimfinished) {
                pl->prop->pos = m->pos;
                pl->deathanimfinished = 0;
            } else {
                float lerp = 0.35f;
                pl->prop->pos.x += dx * lerp;
                pl->prop->pos.y += dy * lerp;
                pl->prop->pos.z += dz * lerp;
            }
            
            pl->pos = pl->prop->pos;
            pl->field_488.collision_position = pl->prop->pos;
            pl->field_488.pos = pl->prop->pos;
            
            /* Forward movement speeds for third-person animations */
            pl->speedforwards = m->movespeed[0];
            pl->speedsideways = m->movespeed[1];
            
            /* Orientation */
            pl->vv_theta = m->angles[0];
            pl->vv_verta = m->angles[1];
            pl->vv_verta360 = m->angles[1];
            pl->vv_costheta = cosf(m->angles[0] * (M_PI_F / 180.0f));
            pl->vv_sintheta = sinf(m->angles[0] * (M_PI_F / 180.0f));
            
            /* Crouch / Stance */
            pl->crouchpos = m->crouchpos;
            
            /* Third-person character model animation */
            if (pl->prop->chr) {
                pl->prop->chr->aimendback = m->angles[1];
                pl->prop->chr->aimendsideback = 0.0f;
                /* Network position is at the eye; the model's ground is at
                 * the feet. Using eye height here causes vertical twitching. */
                pl->prop->chr->ground = pl->prop->pos.y - pl->eyeheight;

                /* Weapon synchronization on remote character */
                s8 cur_wep = -1;
                if (pl->prop->chr->weapons_held[GUNRIGHT] && pl->prop->chr->weapons_held[GUNRIGHT]->weapon) {
                    cur_wep = pl->prop->chr->weapons_held[GUNRIGHT]->weapon->weaponnum;
                }
                if (cur_wep != m->weaponnum) {
                    chrSetWeaponFlag4(pl->prop->chr, GUNRIGHT);
                    if (pl->prop->chr->weapons_held[GUNRIGHT] && pl->prop->chr->weapons_held[GUNRIGHT]->obj) {
                        objFreePermanently(pl->prop->chr->weapons_held[GUNRIGHT]->obj, 1);
                    }
                    if (m->weaponnum > ITEM_UNARMED && m->weaponnum < ITEM_IDS_MAX) {
                        enum PROP prop = getPropForHeldItem((ITEM_IDS)m->weaponnum);
                        if ((s32)prop >= 0) {
                            chrGiveWeapon(pl->prop->chr, prop, (ITEM_IDS)m->weaponnum, 0);
                        }
                    }
                    pl->hands[GUNRIGHT].weaponnum = (ITEM_IDS)m->weaponnum;
                }
            }
            
            /* Firing state */
            pl->hands[GUNRIGHT].field_87D = (m->ucmd & UCMD_FIRE) ? 1 : 0;

            /* Crucial: update player room list so prop renders across portal room boundaries */
            extern void bondviewUpdatePlayerRoom(struct player *player);
            bondviewUpdatePlayerRoom(pl);
            s_remote_render_pos[playernum] = pl->prop->pos;
            s_remote_render_valid[playernum] = true;
        }
    }
}

void netPlayerSyncAfterTick(s32 playernum) {
    if (!netIsActive()) return;
    /* This tick ran the level's players: their structs are valid for voice. */
    netVoicePlayersTick();
    
    int local_slot = netGetLocalSlot();
    if (playernum != local_slot) {
        /* The original per-player tick can move a remote prop after the
         * network correction above. Restore its received position and room
         * membership before the shared world render. */
        if (playernum >= 0 && playernum < GEVR_MAX_PLAYERS &&
            s_remote_render_valid[playernum] && g_playerPointers[playernum] &&
            g_playerPointers[playernum]->prop) {
            struct player *remote = g_playerPointers[playernum];
            remote->prop->pos = s_remote_render_pos[playernum];
            remote->pos = remote->prop->pos;
            remote->field_488.collision_position = remote->prop->pos;
            remote->field_488.pos = remote->prop->pos;
            if (remote->prop->chr)
                remote->prop->chr->ground = remote->prop->pos.y - remote->eyeheight;
            bondviewUpdatePlayerRoom(remote);
        }
        return;
    }
    if (!g_playerPointers[playernum]) return;

    {
        static int last_phase = -1;
        static unsigned last_connected = 0;
        unsigned connected = 0;
        const NetMsgLobbyState *lobby = netGetLobbyState();
        int phase = netGetPhase();
        for (int slot = 0; slot < GEVR_MAX_PLAYERS; slot++)
            if (lobby->slots[slot].connected) connected |= 1u << slot;
        if (last_phase >= 0) {
            if (last_phase != NET_PHASE_IN_PROGRESS && phase == NET_PHASE_IN_PROGRESS)
                hudmsgTopShow("MATCH STARTED");
            for (int slot = 0; slot < GEVR_MAX_PLAYERS; slot++) {
                if (slot != local_slot && (connected & (1u << slot)) &&
                    !(last_connected & (1u << slot))) {
                    char message[48];
                    snprintf(message, sizeof(message), "%s joined", lobby->slots[slot].name);
                    hudmsgTopShow(message);
                }
            }
        }
        last_phase = phase;
        last_connected = connected;
    }
    
    struct player *pl = g_playerPointers[playernum];
    if (!pl) return;
    
    struct netplayermove move;
    memset(&move, 0, sizeof(move));
    
    move.tick = (u32)(sysGetMicroseconds() / 1000);
    move.ucmd = 0;
    if (get_button_state(1, "trigger")) {
        move.ucmd |= UCMD_FIRE;
    }
    if (pl->crouchpos != CROUCH_STAND) {
        move.ucmd |= UCMD_DUCK;
    }
    
    move.movespeed[0] = pl->speedforwards;
    move.movespeed[1] = pl->speedsideways;
    
    /* Head (HMD) orientation */
    move.angles[0] = pl->vv_theta;
    move.angles[1] = pl->vv_verta;
    
    move.weaponnum = (s8)getCurrentPlayerWeaponId(GUNRIGHT);
    move.crouchpos = (s8)pl->crouchpos;
    
    if (pl->prop) {
        move.pos = pl->prop->pos;
    } else {
        move.pos = pl->pos;
    }
    
    /* Hand controller transform */
    move.handpos.x = gCtrlPos[1][0];
    move.handpos.y = gCtrlPos[1][1];
    move.handpos.z = gCtrlPos[1][2];
    
    /* Extract hand rotation euler angles from quaternion */
    float hqw = gCtrlQuat[1][0];
    float hqx = gCtrlQuat[1][1];
    float hqy = gCtrlQuat[1][2];
    float hqz = gCtrlQuat[1][3];
    
    float sin_p = 2.0f * (hqw * hqx - hqy * hqz);
    if (fabsf(sin_p) >= 1.0f) {
        move.handrot.x = copysignf(90.0f, sin_p);
    } else {
        move.handrot.x = asinf(sin_p) * (180.0f / (float)M_PI);
    }
    move.handrot.y = atan2f(2.0f * (hqw * hqy + hqx * hqz), 1.0f - 2.0f * (hqx * hqx + hqy * hqy)) * (180.0f / (float)M_PI);
    move.handrot.z = atan2f(2.0f * (hqw * hqz + hqx * hqy), 1.0f - 2.0f * (hqx * hqx + hqz * hqz)) * (180.0f / (float)M_PI);
    
    netSendLocalPlayerMove(&move);
}
