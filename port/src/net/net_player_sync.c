#include <math.h>
#include <string.h>
#include "../vr/vr_openxr.h"
#include "net_player_sync.h"
#include "net_core.h"
#include "net_protocol.h"
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

void netPlayerSyncInit(void) {
}

void netPlayerSyncBeforeTick(s32 playernum) {
    if (!netIsActive()) return;
    
    int local_slot = netGetLocalSlot();
    if (playernum == local_slot) {
        return;
    }
    
    if (netIsRemotePlayerActive(playernum) && g_playerPointers[playernum]) {
        const struct netplayermove *m = netGetRemotePlayerMove(playernum);
        struct player *pl = g_playerPointers[playernum];
        if (!pl || !m) return;
        
        if (pl->prop) {
            /* Position: check for huge delta or initial snap */
            float dx = m->pos.x - pl->prop->pos.x;
            float dy = m->pos.y - pl->prop->pos.y;
            float dz = m->pos.z - pl->prop->pos.z;
            float dist_sq = dx*dx + dy*dy + dz*dz;
            
            /* If player was marked dead and moves/teleports on respawn, revive them */
            if (pl->bonddead && dist_sq > (150.0f * 150.0f)) {
                pl->bonddead = 0;
                pl->deathanimfinished = 0;
            }

            if (dist_sq > (512.0f * 512.0f) || dist_sq < 0.0001f || (pl->bonddead == 0 && pl->deathanimfinished)) {
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
                pl->prop->chr->ground = pl->prop->pos.y;

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
        }
    }
}

void netPlayerSyncAfterTick(s32 playernum) {
    if (!netIsActive()) return;
    
    int local_slot = netGetLocalSlot();
    if (playernum != local_slot || !g_playerPointers[playernum]) return;
    
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

