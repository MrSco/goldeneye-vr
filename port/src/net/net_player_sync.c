#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../vr/vr_openxr.h"
#include "net_player_sync.h"
#include "net_core.h"
#include "net_game.h"
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
extern void bondviewUpdatePlayerRoom(struct player *player);
static coord3d s_remote_render_pos[GEVR_MAX_PLAYERS];
static StandTile *s_remote_render_tile[GEVR_MAX_PLAYERS];
static bool s_remote_render_valid[GEVR_MAX_PLAYERS];
static f32 s_remote_barrel_pitch[GEVR_MAX_PLAYERS];   /* logged only (see netPlayerSyncBeforeTick) */
static f32 s_remote_barrel_yaw[GEVR_MAX_PLAYERS];
static s8 s_copy_weapon[GEVR_MAX_PLAYERS][2];         /* the item last given to a copy's hand (netSyncCopyHand) */
static u64 s_copy_dead_since_us[GEVR_MAX_PLAYERS];    /* the copy has been dead since (0: alive) */
static u64 s_copy_alive_since_us[GEVR_MAX_PLAYERS];   /* the copy has been alive since (0: dead) */
static u64 s_owner_dead_since_us[GEVR_MAX_PLAYERS];   /* the owner has reported dead since (0: alive) */

void netPlayerSyncInit(void) {
}

/*
 * A remote copy is placed at its owner's position, not walked there, so its
 * stand tile has to be moved along with it, as the game moves the camera tile
 * (bondview.c bondviewSetCurrentPlayerPosition). The copy's rooms are built
 * from this tile (chrprop.c chrpropUpdateRoomList) and its model stands on it
 * (chr.c sub_GAME_7F01FC10): a stale tile files the body in a room the view
 * never draws, which hid every other player from v0.3.4.
 */
static StandTile *netSyncRemoteTile(struct player *pl, const coord3d *from, bool snapped) {
    extern s32 walkTilesBetweenPoints_NoCallback(StandTile **tileStack, f32 start_x, f32 start_z, f32 dest_x, f32 dest_z);
    extern s32 stanTestPointWithinTileBoundsMaybe(StandTile *tile, f32 p_x, f32 p_z);
    extern StandTile *stanFindTileBelowPos(coord3d *pos, u8 *rooms, f32 *yRtn);
    coord3d to = pl->prop->pos;
    StandTile *tile = pl->field_488.current_tile_ptr;

    if (tile && !snapped) {
        StandTile *walked = tile;
        if (walkTilesBetweenPoints_NoCallback(&walked, from->x, from->z, to.x, to.z) &&
            walked && stanTestPointWithinTileBoundsMaybe(walked, to.x, to.z)) {
            tile = walked;
        } else if (!stanTestPointWithinTileBoundsMaybe(tile, to.x, to.z)) {
            tile = NULL;
        }
    } else {
        tile = NULL;
    }

    if (!tile) {
        /* The highest tile below the eye. A crouch brings the eye down to
         * 30 units over the floor, so no lower probe: it would go under it. */
        f32 y;
        tile = stanFindTileBelowPos(&to, NULL, &y);
        if (!tile) return NULL;
    }

    pl->field_488.current_tile_ptr = tile;
    pl->field_488.current_tile_ptr_for_portals = tile;
    pl->prop->stan = tile;
    return tile;
}

static int s_follow_slot = -1;
static bool s_spectator_entered = false;
static bool s_follow_a, s_follow_b;
void netSpectatorReset(void) {
    s_follow_slot = -1;
    s_spectator_entered = false;
    s_follow_a = s_follow_b = false;
}
int netSpectatorTarget(void) { return netLocalIsSpectator() ? s_follow_slot : -1; }
static bool netCanFollow(int slot) {
    return slot >= 0 && slot < getPlayerCount() && slot != netGetLocalSlot() &&
           netSlotOccupied(slot) && !netSlotIsSpectator(slot) && g_playerPointers[slot] &&
           g_playerPointers[slot]->prop && !g_playerPointers[slot]->bonddead;
}
void netSpectatorFrame(void) {
    if (!gevrSpectating() || !g_CurrentPlayer || !g_CurrentPlayer->prop) {
        if (s_spectator_entered) netSpectatorReset();
        return;
    }
    struct player *pl = g_CurrentPlayer;
    if (!s_spectator_entered) {
        currentPlayerEquipWeaponWrapper(GUNRIGHT, ITEM_UNARMED);
        currentPlayerEquipWeaponWrapper(GUNLEFT, ITEM_UNARMED);
        pl->cheatBondInvincible = true;
        s_spectator_entered = true;
    }
    bool a = get_button_state(1, "a"), b = get_button_state(1, "b");
    int direction = !pl->mpmenuon ? (a && !s_follow_a ? 1 : b && !s_follow_b ? -1 : 0) : 0;
    if (direction) netTouchLocalActivity();
    s_follow_a = a; s_follow_b = b;
    int old = s_follow_slot;
    if (!netCanFollow(s_follow_slot) || direction) {
        int start = s_follow_slot < 0 ? netGetLocalSlot() : s_follow_slot;
        int step = direction ? direction : 1;
        s_follow_slot = -1;
        for (int i = 1; i <= GEVR_MAX_PLAYERS; i++) {
            int slot = (start + i * step + GEVR_MAX_PLAYERS * 2) % GEVR_MAX_PLAYERS;
            if (netCanFollow(slot)) { s_follow_slot = slot; break; }
        }
    }
    pl->speedforwards = pl->speedsideways = 0;
    if (s_follow_slot < 0) return; /* retain a safe camera until somebody is alive */
    struct player *target = g_playerPointers[s_follow_slot];
    coord3d from = pl->prop->pos;
    pl->prop->pos = target->prop->pos;
    pl->pos = pl->field_488.collision_position = pl->field_488.pos = pl->prop->pos;
    netSyncRemoteTile(pl, &from, old != s_follow_slot);
    bondviewUpdatePlayerRoom(pl);
    if (old != s_follow_slot) {
        char label[64];
        gevrSpectatorAim(target->vv_theta);
        snprintf(label, sizeof(label), "SPECTATING %s", netGetSlotName(s_follow_slot));
        hudmsgTopShow(label);
        sysLogPrintf(LOG_NOTE, "spectator: following slot %d", s_follow_slot);
    }
}

static void netSyncCopyHand(struct player *pl, int slot, int hand, int weapon, int fire) {
    ChrRecord *chr = pl->prop->chr;
    int current = chr->weapons_held[hand] && chr->weapons_held[hand]->weapon ? chr->weapons_held[hand]->weapon->weaponnum : ITEM_UNARMED;
    /*
     * An item with no held model (fists) leaves the hand empty, so read
     * back from the chr it never matched and the hand was re-given every
     * tick, replaying the body's draw (playtest 2026-09-30: "takes the gun
     * out from behind the back"). The item last given is remembered per
     * hand; the chr is consulted only for an item that has a model, which a
     * new chr (a stage load) will lack.
     */
    bool has_model = weapon > ITEM_UNARMED && weapon < ITEM_IDS_MAX && (s32)getPropForHeldItem((ITEM_IDS)weapon) >= 0;
    if (s_copy_weapon[slot][hand] != weapon || (has_model && current != weapon)) {
        sysLogPrintf(LOG_NOTE, "net: copy %d %s hand weapon %d -> %d (held %d, given %d)", slot,
                     hand == GUNLEFT ? "left" : "right", current, weapon, pl->hands[hand].weaponnum, s_copy_weapon[slot][hand]);
        chrSetWeaponFlag4(chr, hand);
        if (chr->weapons_held[hand] && chr->weapons_held[hand]->obj) objFreePermanently(chr->weapons_held[hand]->obj, 1);
        if (has_model) {
            chrGiveWeapon(chr, getPropForHeldItem((ITEM_IDS)weapon), (ITEM_IDS)weapon, hand == GUNLEFT ? PROPFLAG_WEAPON_LEFTHANDED : 0);
            /* Multiply the newly-created model's native scale once, never the
             * previous hand model's scale on a tick or weapon swap. */
            if (chr->weapons_held[hand] && chr->weapons_held[hand]->obj && chr->weapons_held[hand]->obj->model)
                chr->weapons_held[hand]->obj->model->scale *= netGunSizeFactor(netActiveGunSize());
        }
        s_copy_weapon[slot][hand] = (s8)weapon;
    }
    if (pl->hands[hand].weaponnum != weapon) {
        pl->hands[hand].weaponnum = (ITEM_IDS)weapon;
        pl->hands[hand].weapon_next_weapon = weapon;
        pl->hands[hand].weapon_action_state = GUN_ANIM_STATE_IDLE;
        pl->hands[hand].weapon_current_animation = 0;
    }
    if (weapon > ITEM_UNARMED && weapon < ITEM_IDS_MAX) {
        WeaponStats *stats = get_ptr_item_statistics((ITEM_IDS)weapon);
        if (stats && stats->AmmoType > 0 && stats->AmmoType < (s32)(sizeof(pl->ammoheldarr)/sizeof(pl->ammoheldarr[0]))) {
            s32 mag = stats->MagSize > 0 ? stats->MagSize : 1;
            if (pl->ammoheldarr[stats->AmmoType] < mag) pl->ammoheldarr[stats->AmmoType] = mag;
            if (pl->hands[hand].weapon_ammo_in_magazine <= 0) pl->hands[hand].weapon_ammo_in_magazine = mag;
        }
    }
    pl->hands[hand].field_87D = fire;
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
            /*
             * The owner decides its own life (protocol 10). Each headset
             * applies the host's damage events to the copy as well, for the
             * kill credit and the death animation, but the accounting drifts
             * (armour picked up on one headset, the damage-flash gate), and a
             * copy that died where its owner did not stood as a corpse
             * ignoring the owner's moves (playtest 2026-09-30). So: dead
             * owner, live copy: the copy dies, credited to whoever last hurt
             * it here; live owner, dead copy: the copy respawns (any pad, the
             * position below snaps it); both alive: the owner's numbers.
             */
            /*
             * With a grace period each way. The owner's packets run a
             * round trip behind the host's damage event, so the copy dies
             * here while the owner's last packet still says alive, and the
             * owner's own death follows within the round trip: acting at
             * once respawned a copy in the tick it died, mid death, and the
             * game crashed in the next object tick (host log, 2026-09-30).
             * A dead copy is resurrected only after the owner has reported
             * alive for 1.5 s past the death; a live copy is killed only
             * after the owner has reported dead for 0.5 s, by when the
             * damage event that kills it properly has normally arrived.
             */
            {
                u64 now = sysGetMicroseconds();
                if (pl->bonddead) {
                    if (!s_copy_dead_since_us[playernum]) s_copy_dead_since_us[playernum] = now;
                    s_copy_alive_since_us[playernum] = 0;
                } else {
                    if (!s_copy_alive_since_us[playernum]) s_copy_alive_since_us[playernum] = now;
                    s_copy_dead_since_us[playernum] = 0;
                }
                if (m->dead) {
                    if (!s_owner_dead_since_us[playernum]) s_owner_dead_since_us[playernum] = now;
                } else {
                    s_owner_dead_since_us[playernum] = 0;
                }
                /*
                 * ... and the copy alive for 0.5 s as well: the owner's
                 * RESPAWN revives the copy while its last state packet still
                 * says dead, and the kill rule killed the fresh copy at once,
                 * crediting a second kill for one death (match 2026-09-30,
                 * 14:52). The post-respawn packets arrive well inside that.
                 */
                /* copies take no damage of their own now (net_core.c netApplyDamage): the owner's death is the kill */
                if (m->dead && !pl->bonddead && now - s_owner_dead_since_us[playernum] > 100000
                    && now - s_copy_alive_since_us[playernum] > 500000) {
                    s32 prev = get_cur_playernum();
                    s32 killer = netLastAttacker(playernum);
                    sysLogPrintf(LOG_NOTE, "net: copy %d: owner dead, copy alive (health %.2f): killing, credit %d", playernum, pl->bondhealth, killer);
                    set_cur_player(playernum);
                    record_damage_kills(1000.0f, 0.0f, 1.0f, killer, 1);
                    set_cur_player(prev);
                    s_copy_dead_since_us[playernum] = now;
                } else if (!m->dead && pl->bonddead && m->health > 0.0f && now - s_copy_dead_since_us[playernum] > 1500000) {
                    s32 prev = get_cur_playernum();
                    sysLogPrintf(LOG_NOTE, "net: copy %d: owner alive (health %.2f) %.1f s past the copy's death: respawning the copy",
                                 playernum, m->health, (now - s_copy_dead_since_us[playernum]) / 1000000.0f);
                    set_cur_player(playernum);
                    mp_respawn_handler_net(0, m->angles[0]);
                    set_cur_player(prev);
                    pl->deathanimfinished = 1;   /* snap to the owner below */
                    s_copy_dead_since_us[playernum] = 0;
                } else if (!m->dead && !pl->bonddead) {
                    pl->bondhealth = m->health;
                    pl->bondarmour = m->armour;
                }
            }
            /* A corpse stays at its death location until the reliable respawn
             * event resets the player. Late movement packets must not drag it. */
            if (pl->bonddead) {
                pl->speedforwards = 0.0f;
                pl->speedsideways = 0.0f;
                pl->hands[GUNRIGHT].field_87D = pl->hands[GUNLEFT].field_87D = 0;
                return;
            }
            /* Position: check for huge delta or initial snap */
            coord3d from = pl->prop->pos;
            bool snapped = false;
            float dx = m->pos.x - pl->prop->pos.x;
            float dy = m->pos.y - pl->prop->pos.y;
            float dz = m->pos.z - pl->prop->pos.z;
            float dist_sq = dx*dx + dy*dy + dz*dz;

            if (dist_sq > (512.0f * 512.0f) || dist_sq < 0.0001f || pl->deathanimfinished) {
                snapped = dist_sq >= 0.0001f;
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
            s_remote_render_tile[playernum] = netSyncRemoteTile(pl, &from, snapped);

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
            
            /*
             * The body's aim, as bondviewRenderDebugBondView finds it for a
             * drawn view (bondview2.c playerTick reads field_2A08/field_2A0C):
             * the aim's pitch, and its yaw off the view; radians, up and left
             * positive. Online the copy's view is never built; the owner's
             * barrel gives both, the head's pitch without it. The chr's
             * aimendback is playerTick's to write: set here, in degrees, it
             * stood whenever an animation blend skipped that write, and the
             * torso pitched through whole turns each time the copy changed
             * step (strafing left and right, user).
             */
            /*
             * Playtest 2026-09-30: posed from the barrel, the copies stood
             * bent over (a VR gun rests 30-47 degrees down while the head is
             * level: "net: pose local"), twisted at the waist whenever the
             * controller swung, and jumped between the two sources as the
             * grip came and went (the "draws the gun from behind the back"
             * look). The flat game poses the body from the view: its pitch,
             * and no yaw, since its gun points where it looks. So here, with
             * the barrel's pitch and yaw only logged.
             */
            {
                coord3d o;
                coord3d d;
                s_remote_barrel_pitch[playernum] = 0.0f;
                s_remote_barrel_yaw[playernum] = 0.0f;
                if (!pl->bonddead && netGetRemoteAim(playernum, GUNRIGHT, &o, &d)) {
                    f32 theta = pl->vv_theta * (M_PI_F / 180.0f);
                    f32 fx = -sinf(theta), fz = cosf(theta);   /* the view's forward, level */
                    s_remote_barrel_pitch[playernum] = atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z)) * (180.0f / M_PI_F);
                    s_remote_barrel_yaw[playernum] = atan2f(d.x * fz - d.z * fx, d.x * fx + d.z * fz) * (180.0f / M_PI_F);
                }
                pl->field_2A08 = pl->vv_verta * (M_PI_F / 180.0f);
                pl->field_2A0C = 0.0f;
            }
            /* Third-person character model animation */
            if (pl->prop->chr) {
                /* Network position is at the eye; the model's ground is at
                 * the feet. Using eye height here causes vertical twitching. */
                pl->prop->chr->ground = pl->prop->pos.y - pl->eyeheight;

                netSyncCopyHand(pl, playernum, GUNRIGHT, netRemoteWeapon(playernum, GUNRIGHT), netRemoteTrigger(playernum, GUNRIGHT));
                netSyncCopyHand(pl, playernum, GUNLEFT, netRemoteWeapon(playernum, GUNLEFT), netRemoteTrigger(playernum, GUNLEFT));
            }

            /*
             * Playtest logging (2026-09-30: "bent over at the torso", "twists
             * when the controllers move", "bends when strafing", "draws the
             * gun from behind the back on grip"): what the copy's body is
             * posed from, every 2 s and at each change of the aim source.
             * pitch/yaw are field_2A08/field_2A0C in degrees (up and left
             * positive), head is the owner's view pitch, turn is field_1280
             * (the strafe turn), anim the body's animation offset.
             */
            {
                static u64 next_pose_log_us[GEVR_MAX_PLAYERS];
                static s8 last_aim[GEVR_MAX_PLAYERS] = { [0 ... GEVR_MAX_PLAYERS - 1] = -1 };
                s8 aim = (m->ucmd & UCMD_AIMVALID) ? 1 : 0;
                u64 now = sysGetMicroseconds();
                if (aim != last_aim[playernum] || now >= next_pose_log_us[playernum]) {
                    sysLogPrintf(LOG_NOTE, "net: pose %d %s pitch %.0f yaw %.0f (barrel %.0f/%.0f) head %.0f fwd %.2f side %.2f turn %.0f crouch %d anim %d wep %d/%d fire %d%d%s",
                                 playernum, aim ? "barrel" : "head",
                                 pl->field_2A08 * (180.0f / M_PI_F), pl->field_2A0C * (180.0f / M_PI_F),
                                 s_remote_barrel_pitch[playernum], s_remote_barrel_yaw[playernum], pl->vv_verta,
                                 pl->speedforwards, pl->speedsideways, pl->field_1280, pl->crouchpos,
                                 pl->players_cur_animation, pl->hands[GUNRIGHT].weaponnum, pl->hands[GUNLEFT].weaponnum,
                                 pl->hands[GUNRIGHT].field_87D ? 1 : 0, pl->hands[GUNLEFT].field_87D ? 1 : 0,
                                 aim != last_aim[playernum] ? " (aim source changed)" : "");
                    last_aim[playernum] = aim;
                    next_pose_log_us[playernum] = now + 2000000;
                }
            }

            /* Crucial: update player room list so prop renders across portal room boundaries */
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
            const struct netplayermove *m = netGetRemotePlayerMove(playernum);
            StandTile *tile = s_remote_render_tile[playernum];
            remote->prop->pos = s_remote_render_pos[playernum];
            remote->pos = remote->prop->pos;
            remote->field_488.collision_position = remote->prop->pos;
            remote->field_488.pos = remote->prop->pos;
            if (tile) {
                remote->field_488.current_tile_ptr = tile;
                remote->field_488.current_tile_ptr_for_portals = tile;
                remote->prop->stan = tile;
            }
            /* The tick read no stick for the copy and zeroed these; the body's
             * walk animation (bondview2.c playerTick) is chosen from them. */
            if (m) {
                remote->speedforwards = m->movespeed[0];
                remote->speedsideways = m->movespeed[1];
            }
            if (remote->prop->chr)
                remote->prop->chr->ground = remote->prop->pos.y - remote->eyeheight;
            bondviewUpdatePlayerRoom(remote);

            {
                static u64 next_log_us[GEVR_MAX_PLAYERS];
                u64 now = sysGetMicroseconds();
                if (now >= next_log_us[playernum]) {
                    const u8 *rooms = remote->prop->rooms;
                    next_log_us[playernum] = now + 5000000;
                    sysLogPrintf(LOG_NOTE, "net: remote %d at %.0f,%.0f,%.0f tile room %d, prop rooms %d %d %d, %s",
                                 playernum, remote->prop->pos.x, remote->prop->pos.y, remote->prop->pos.z,
                                 tile ? (s32)tile->room : -1,
                                 rooms[0] == 0xff ? -1 : rooms[0],
                                 rooms[0] == 0xff || rooms[1] == 0xff ? -1 : rooms[1],
                                 rooms[0] == 0xff || rooms[1] == 0xff || rooms[2] == 0xff ? -1 : rooms[2],
                                 (remote->prop->flags & PROPFLAG_ONSCREEN) ? "on screen" : "off screen");
                }
            }
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
        /* The host's countdown to the next round (net_core.c
         * netScheduleRound): a top message each second, a fade to black over
         * its last second, and a fade in once the new stage has loaded. */
        {
            extern void currentPlayerSetFadeColour(s32 r, s32 g, s32 b, f32 frac);
            extern void currentPlayerSetFadeFrac(f32 maxfadetime, f32 frac);
            static s32 last_sec = -1;
            static s32 fading = 0;
            u64 end = netGetCountdownEndUs();
            u64 now = sysGetMicroseconds();
            if (netTakeStageFadeIn()) {
                currentPlayerSetFadeColour(0, 0, 0, 1.0f);
                currentPlayerSetFadeFrac(60.0f, 0.0f);
                fading = 0;
            }
            if (end) {
                s32 sec = end > now ? (s32)((end - now + 999999) / 1000000) : 0;
                if (sec > 0 && sec != last_sec) {
                    extern void gevrHudTopReplace(const char *mess, const char *prefix);
                    char message[48];
                    snprintf(message, sizeof(message), "MATCH STARTS IN %d", sec);
                    gevrHudTopReplace(message, "MATCH STARTS IN");   /* in place: the queue dropped numbers */
                    last_sec = sec;
                }
                if (!fading && end <= now + 1000000) {
                    currentPlayerSetFadeColour(0, 0, 0, 0.0f);
                    currentPlayerSetFadeFrac(60.0f, 1.0f);
                    fading = 1;
                }
            } else {
                if (fading) {
                    currentPlayerSetFadeFrac(15.0f, 0.0f);
                    fading = 0;
                }
                last_sec = -1;
            }
        }
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
    if (!pl || netLocalIsSpectator()) return;
    
    struct netplayermove move;
    memset(&move, 0, sizeof(move));
    
    move.tick = (u32)(sysGetMicroseconds() / 1000);
    move.ucmd = 0;
    {
        extern s32 gevrHandChopSwinging(s32 ctrl);
        extern int gevrVrTriggerDown[2];
        extern s32 gevrStereoShotWorld(s32 handnum, coord3d *origin, coord3d *dir);
        for (int hand = GUNRIGHT; hand <= GUNLEFT; hand++) {
            int item = getCurrentPlayerWeaponId(hand);
            bool enabled = hand == GUNRIGHT || netActiveDualWield();
            bool fire = enabled && !pl->bonddead && !pl->mpmenuon && !lvlGetControlsLockedFlag() && gevrVrTriggerDown[hand];
            if (enabled && (item == ITEM_FIST || item == ITEM_KNIFE)) fire |= !pl->mpmenuon && gevrHandChopSwinging(hand == GUNRIGHT ? 1 : 0);
            else if (item == ITEM_UNARMED || item == ITEM_TRIGGER ||
                     (pl->hands[hand].weapon_ammo_in_magazine <= 0 && !bondwalkItemCheckBitflags(item, WEAPONSTATBITFLAG_CLICKY))) fire = false;
            if (fire) move.ucmd |= hand == GUNLEFT ? UCMD_FIRE_LEFT : UCMD_FIRE;
            if (enabled && gevrStereoShotWorld(hand, hand == GUNLEFT ? &move.aimorigin_l : &move.aimorigin,
                                                hand == GUNLEFT ? &move.aimdir_l : &move.aimdir))
                move.ucmd |= hand == GUNLEFT ? UCMD_AIMVALID_LEFT : UCMD_AIMVALID;
        }
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
    move.weaponnum_left = netActiveDualWield() ? (s8)getCurrentPlayerWeaponId(GUNLEFT) : ITEM_UNARMED;
    move.crouchpos = (s8)pl->crouchpos;
    move.health = pl->bondhealth;
    move.armour = pl->bondarmour;
    move.dead = pl->bonddead ? 1 : 0;
    
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

    {
        static coord3d s_last_act_pos;
        static f32 s_last_act_theta = 0.0f, s_last_act_verta = 0.0f;
        bool act = false;
        if (fabsf(pl->speedforwards) > 0.02f || fabsf(pl->speedsideways) > 0.02f) act = true;
        if (move.ucmd != 0) act = true;
        if (fabsf(move.angles[0] - s_last_act_theta) > 2.0f || fabsf(move.angles[1] - s_last_act_verta) > 2.0f) {
            act = true;
            s_last_act_theta = move.angles[0];
            s_last_act_verta = move.angles[1];
        }
        f32 dx = move.pos.x - s_last_act_pos.x, dy = move.pos.y - s_last_act_pos.y, dz = move.pos.z - s_last_act_pos.z;
        if (dx * dx + dy * dy + dz * dz > 0.0016f) {
            act = true;
            s_last_act_pos = move.pos;
        }
        if (act) netTouchLocalActivity();
    }

    /* The owner's side of the copy's pose line above, to compare against. */
    {
        static u64 next_local_log_us;
        u64 now = sysGetMicroseconds();
        if (now >= next_local_log_us) {
            next_local_log_us = now + 2000000;
            sysLogPrintf(LOG_NOTE, "net: pose local %s pitch %.0f yaw %.0f head %.0f fwd %.2f side %.2f turn %.0f crouch %d wep %d/%d ucmd %x aimdir %.2f,%.2f,%.2f",
                         (move.ucmd & UCMD_AIMVALID) ? "barrel" : "head",
                         pl->field_2A08 * (180.0f / M_PI_F), pl->field_2A0C * (180.0f / M_PI_F), pl->vv_verta,
                         pl->speedforwards, pl->speedsideways, pl->field_1280, pl->crouchpos,
                         move.weaponnum, move.weaponnum_left, (unsigned)move.ucmd,
                         move.aimdir.x, move.aimdir.y, move.aimdir.z);
        }
    }

    netSendLocalPlayerMove(&move);
}
