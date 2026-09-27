#include <math.h>
#include <string.h>
#include "../vr/vr_openxr.h"
#include "net_player_sync.h"
#include "net_core.h"
#include <ultra64.h>
#include <bondtypes.h>
#include <player.h>
#include <bondview.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern bool get_button_state(int controllerIndex, const char* buttonName);

void netPlayerSyncInit(void) {
}

void netPlayerSyncBeforeTick(s32 playernum) {
    if (!netIsActive()) return;
    
    int local_slot = netGetLocalSlot();
    if (playernum == local_slot) {
        return;
    }
    
    if (netIsRemotePlayerActive(playernum) && g_playerPointers[playernum]) {
        const NetMsgPlayerState *ps = netGetRemotePlayerState(playernum);
        struct player *pl = g_playerPointers[playernum];
        if (pl && pl->prop) {
            float lerp = 0.35f;
            pl->prop->pos.x += (ps->pos_x - pl->prop->pos.x) * lerp;
            pl->prop->pos.y += (ps->pos_y - pl->prop->pos.y) * lerp;
            pl->prop->pos.z += (ps->pos_z - pl->prop->pos.z) * lerp;
            
            pl->pos = pl->prop->pos;
            pl->field_488.collision_position = pl->prop->pos;
            pl->vv_theta = ps->head_yaw;
            
            if (pl->prop->chr) {
                pl->prop->chr->aimendback = ps->head_pitch;
                pl->prop->chr->aimendsideback = 0.0f;
            }
            pl->hands[GUNRIGHT].field_87D = ps->is_firing;
        }
    }
}

void netPlayerSyncAfterTick(s32 playernum) {
    if (!netIsActive()) return;
    
    int local_slot = netGetLocalSlot();
    if (playernum != local_slot || !g_playerPointers[playernum]) return;
    
    struct player *pl = g_playerPointers[playernum];
    if (!pl || !pl->prop) return;
    
    NetMsgPlayerState local_state;
    memset(&local_state, 0, sizeof(local_state));
    
    local_state.pos_x = pl->prop->pos.x;
    local_state.pos_y = pl->prop->pos.y;
    local_state.pos_z = pl->prop->pos.z;
    
    float qx = vr_HMD_rot_Q.x;
    float qy = vr_HMD_rot_Q.y;
    float qz = vr_HMD_rot_Q.z;
    float qw = vr_HMD_rot_Q.w;
    
    float sin_pitch = 2.0f * (qw * qx - qy * qz);
    if (fabsf(sin_pitch) >= 1.0f) {
        local_state.head_pitch = copysignf(90.0f, sin_pitch);
    } else {
        local_state.head_pitch = asinf(sin_pitch) * (180.0f / (float)M_PI);
    }
    local_state.head_yaw = atan2f(2.0f * (qw * qy + qx * qz), 1.0f - 2.0f * (qx * qx + qy * qy)) * (180.0f / (float)M_PI);
    
    local_state.hand_x = gCtrlPos[1][0];
    local_state.hand_y = gCtrlPos[1][1];
    local_state.hand_z = gCtrlPos[1][2];
    local_state.is_firing = get_button_state(1, "trigger") ? 1 : 0;
    
    netSendLocalPlayerState(&local_state);
}
