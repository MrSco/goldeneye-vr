#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../vr/vr_openxr.h"
#include "net_player_sync.h"
#include "net_core.h"
#include "net_coop.h"
#include "net_game.h"
#include "net_protocol.h"
#include "net_voice.h"
#include "system.h"
#include "gevr_avatar.h"
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
 *
 * Issue #95: the tile is the floor under the body's whole cylinder, room by
 * room (stan.c stanFindGroundAtCyl, Perfect Dark's cdFindGroundInfoAtCyl),
 * probed from the eye: a crouch brings the eye down to 30 units over the
 * floor, so no lower. The centre-only lookup this replaces took the floor
 * below a body at a ledge, and tested many-sided tiles by three points.
 * *floory is that floor, or -FLT_MAX with no tile.
 */
static StandTile *netSyncRemoteTile(struct player *pl, bool snapped, f32 *floory) {
    extern StandTile *stanFindGroundAtCyl(coord3d *pos, f32 radius, u8 *rooms, StandTile *prefer, f32 *groundy, s32 *incentre);
    coord3d to = pl->prop->pos;
    StandTile *prev = snapped ? NULL : pl->field_488.current_tile_ptr;
    u8 rooms[PROPRECORD_STAN_ROOM_LEN + 2];
    StandTile *tile;
    s32 n = 0, incentre;

    *floory = -3.4028235e38f;
    if (prev) rooms[n++] = prev->room;
    for (int i = 0; i < PROPRECORD_STAN_ROOM_LEN && pl->prop->rooms[i] != 0xff; i++)
        rooms[n++] = pl->prop->rooms[i];
    rooms[n] = 0xff;

    tile = stanFindGroundAtCyl(&to, pl->field_488.collision_radius, n ? rooms : NULL, prev, floory, &incentre);
    if (!tile) return NULL;

    pl->field_488.current_tile_ptr = tile;
    pl->field_488.current_tile_ptr_for_portals = tile;
    pl->prop->stan = tile;
    return tile;
}

/*
 * Where a copy's feet go (#95): its owner's own ground. The owner's eye is
 * its ground plus the eye height (bondview2.c
 * bondviewUpdatePlayerCollisionPositionFields), so the eye less the copy's
 * eye height is that ground standing, falling or on a ledge, and lower
 * when crouched; then the floor under the body holds it up. chrTick took
 * the tile's floor instead (chr.c sub_GAME_7F01FC10), every frame, as
 * playerTick sets CHRFLAG_INIT: other players dropped to the floor below
 * at ledges and stood on the ground mid-fall.
 */
static f32 s_remote_ground[GEVR_MAX_PLAYERS];
static bool s_remote_ground_valid[GEVR_MAX_PLAYERS];

static void netSyncRemoteGround(int slot, struct player *pl, f32 floory) {
    f32 feet = pl->prop->pos.y - pl->eyeheight;
    s_remote_ground[slot] = feet > floory ? feet : floory;
    s_remote_ground_valid[slot] = true;
    if (pl->prop->chr) pl->prop->chr->ground = s_remote_ground[slot];
}

/* chr.c's tile floor, or another headset's player's feet */
f32 gevrNetRemoteGround(PropRecord *prop, f32 ground) {
    if (!netIsActive() || !prop || prop->type != PROP_TYPE_VIEWER) return ground;
    int slot = getPlayerPointerIndex(prop);
    if (slot < 0 || slot >= GEVR_MAX_PLAYERS || netSlotIsLocalHuman(slot) || !s_remote_ground_valid[slot]) return ground;
    return s_remote_ground[slot];
}

static int s_follow_slot[GEVR_MAX_LOCAL_PLAYERS] = { -1, -1, -1, -1 };
static bool s_spectator_entered[GEVR_MAX_LOCAL_PLAYERS];
static bool s_follow_a[GEVR_MAX_LOCAL_PLAYERS], s_follow_b[GEVR_MAX_LOCAL_PLAYERS];
void netSpectatorReset(void) {
    for (int i = 0; i < GEVR_MAX_LOCAL_PLAYERS; i++) {
        s_follow_slot[i] = -1;
        s_spectator_entered[i] = false;
        s_follow_a[i] = s_follow_b[i] = false;
    }
}
int netSpectatorTarget(void) {
    int cur = get_cur_playernum();
    int seat = netLocalSeatOfSlot(cur);
    if (seat < 0) seat = 0;
    int sl = netSlotOfLocalSeat(seat);
    return (sl >= 0 && netSlotIsSpectator(sl)) ? s_follow_slot[seat] : -1;
}
static bool netCanFollow(int self_slot, int slot) {
    return slot >= 0 && slot < getPlayerCount() && slot != self_slot &&
           netSlotOccupied(slot) && !netSlotIsSpectator(slot) && g_playerPointers[slot] &&
           g_playerPointers[slot]->prop && !g_playerPointers[slot]->bonddead;
}
void netSpectatorFrame(void) {
    int cur = get_cur_playernum();
    int seat = netLocalSeatOfSlot(cur);
    /* a dedicated server's own seat: no human, but it follows play as it did
     * before split screen, and the world wakes around what it watches */
    const bool server_seat = seat < 0 && netIsDedicated() && cur == netGetLocalSlot();
    if (server_seat) seat = 0;
    if (seat < 0 || seat >= GEVR_MAX_LOCAL_PLAYERS) return;
    if (!(server_seat ? netSlotIsSpectator(cur) : gevrSpectating()) || !g_CurrentPlayer || !g_CurrentPlayer->prop) {
        if (s_spectator_entered[seat]) {
            s_follow_slot[seat] = -1;
            s_spectator_entered[seat] = false;
            s_follow_a[seat] = s_follow_b[seat] = false;
        }
        return;
    }
    struct player *pl = g_CurrentPlayer;
    if (!s_spectator_entered[seat]) {
        currentPlayerEquipWeaponWrapper(GUNRIGHT, ITEM_UNARMED);
        currentPlayerEquipWeaponWrapper(GUNLEFT, ITEM_UNARMED);
        pl->cheatBondInvincible = true;
        s_spectator_entered[seat] = true;
    }
    bool a = (seat == 0) ? get_button_state(1, "a") : false;
    bool b = (seat == 0) ? get_button_state(1, "b") : false;
    int direction = !pl->mpmenuon ? (a && !s_follow_a[seat] ? 1 : b && !s_follow_b[seat] ? -1 : 0) : 0;
    if (direction) netTouchLocalActivity();
    s_follow_a[seat] = a; s_follow_b[seat] = b;
    int old = s_follow_slot[seat];
    if (!netCanFollow(cur, s_follow_slot[seat]) || direction) {
        int start = s_follow_slot[seat] < 0 ? cur : s_follow_slot[seat];
        int step = direction ? direction : 1;
        s_follow_slot[seat] = -1;
        for (int i = 1; i <= GEVR_MAX_PLAYERS; i++) {
            int slot = (start + i * step + GEVR_MAX_PLAYERS * 2) % GEVR_MAX_PLAYERS;
            if (netCanFollow(cur, slot)) { s_follow_slot[seat] = slot; break; }
        }
    }
    pl->speedforwards = pl->speedsideways = 0;
    if (s_follow_slot[seat] < 0) return; /* retain a safe camera until somebody is alive */
    struct player *target = g_playerPointers[s_follow_slot[seat]];
    pl->prop->pos = target->prop->pos;
    pl->pos = pl->field_488.collision_position = pl->field_488.pos = pl->prop->pos;
    {
        f32 floory;
        netSyncRemoteTile(pl, old != s_follow_slot[seat], &floory);
    }
    bondviewUpdatePlayerRoom(pl);
    if (old != s_follow_slot[seat]) {
        char label[64];
        if (seat == 0) gevrSpectatorAim(target->vv_theta);
        else pl->vv_theta = target->vv_theta;
        snprintf(label, sizeof(label), "SPECTATING %s", netGetSlotName(s_follow_slot[seat]));
        if (seat == 0) hudmsgTopShow(label);
        else hudmsgBottomShow(label);
        sysLogPrintf(LOG_NOTE, "spectator (seat %d slot %d): following slot %d", seat, cur, s_follow_slot[seat]);
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
    /* Mission scripts search world weapon props for landed throws. A remote
     * hand must follow the local hand's rule: selecting a mine cannot create
     * a world mine before its owner throws it (#161). */
    bool has_model = weapon > ITEM_UNARMED && weapon < ITEM_IDS_MAX
        && !(netCoopActive() && gevrCoopThrownMissionItem(weapon))
        && (s32)getPropForHeldItem((ITEM_IDS)weapon) >= 0;
    if (s_copy_weapon[slot][hand] != weapon || (has_model && current != weapon)
        || (!has_model && current != ITEM_UNARMED)) {
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

/* The copy's simulation has no local stick or headset. Restore its owner pose
 * after that simulation as well as before it, before any body/aim render pass. */
static void netSyncCopyOrientation(struct player *pl, const struct netplayermove *move) {
    pl->vv_theta = move->angles[0];
    pl->vv_verta = move->angles[1];
    pl->vv_verta360 = move->angles[1] < 0 ? move->angles[1] + 360.0f : move->angles[1];
    pl->vv_costheta = cosf(pl->vv_theta * (M_PI_F / 180.0f));
    pl->vv_sintheta = sinf(pl->vv_theta * (M_PI_F / 180.0f));
    pl->vv_cosverta = cosf(pl->vv_verta * (M_PI_F / 180.0f));
    pl->vv_sinverta = sinf(pl->vv_verta * (M_PI_F / 180.0f));
    pl->field_488.theta_transform.x = -pl->vv_sintheta;
    pl->field_488.theta_transform.y = 0;
    pl->field_488.theta_transform.z = pl->vv_costheta;
    pl->field_488.applied_view.x = -pl->vv_sintheta * pl->vv_cosverta;
    pl->field_488.applied_view.y = pl->vv_sinverta;
    pl->field_488.applied_view.z = pl->vv_costheta * pl->vv_cosverta;
    pl->field_488.applied_view2.x = pl->vv_sintheta * pl->vv_sinverta;
    pl->field_488.applied_view2.y = pl->vv_cosverta;
    pl->field_488.applied_view2.z = -pl->vv_costheta * pl->vv_sinverta;
    pl->field_2A08 = pl->vv_verta * (M_PI_F / 180.0f);
    pl->field_2A0C = 0;
}

void netPlayerSyncBeforeTick(s32 playernum) {
    if (!netIsActive()) return;
    
    if (netSlotIsLocalHuman(playernum)) {
        return;
    }
    if (playernum < 0 || playernum >= GEVR_MAX_PLAYERS) return;
    s_remote_render_valid[playernum] = false;
    s_remote_ground_valid[playernum] = false;
    
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
            f32 floory;
            s_remote_render_tile[playernum] = netSyncRemoteTile(pl, snapped, &floory);

            /* Forward movement speeds for third-person animations */
            pl->speedforwards = m->movespeed[0];
            pl->speedsideways = m->movespeed[1];
            
            /* Orientation */
            netSyncCopyOrientation(pl, m);
            
            /* Crouch / Stance; co-op: a downed player crouches (#94, revive) */
            pl->crouchpos = gevrCoopDowned(playernum) ? CROUCH_SQUAT : m->crouchpos;
            
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
                netSyncRemoteGround(playernum, pl, floory);

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

/*
 * The idle kick's activity (net_core.c netTouchLocalActivity): walking, turning
 * or looking, moving, or firing. Not the move's other flags: UCMD_AIMVALID is set
 * every frame a gun has a barrel pose (always, in VR) and UCMD_DUCK while
 * crouched, so a headset asleep or left in a match never idled out.
 */
static bool netSyncLocalActivity(int slot, f32 speedforwards, f32 speedsideways, const struct netplayermove *move) {
    static coord3d s_last_act_pos[GEVR_MAX_PLAYERS];
    static f32 s_last_act_theta[GEVR_MAX_PLAYERS], s_last_act_verta[GEVR_MAX_PLAYERS];
    if (slot < 0 || slot >= GEVR_MAX_PLAYERS) slot = 0;
    bool act = false;
    if (fabsf(speedforwards) > 0.02f || fabsf(speedsideways) > 0.02f) act = true;
    if (move->ucmd & (UCMD_FIRE | UCMD_FIRE_LEFT)) act = true;
    if (fabsf(move->angles[0] - s_last_act_theta[slot]) > 2.0f || fabsf(move->angles[1] - s_last_act_verta[slot]) > 2.0f) {
        act = true;
        s_last_act_theta[slot] = move->angles[0];
        s_last_act_verta[slot] = move->angles[1];
    }
    f32 dx = move->pos.x - s_last_act_pos[slot].x, dy = move->pos.y - s_last_act_pos[slot].y, dz = move->pos.z - s_last_act_pos[slot].z;
    if (dx * dx + dy * dy + dz * dz > 0.0016f) {
        act = true;
        s_last_act_pos[slot] = move->pos;
    }
    return act;
}

/*
 * A bot the host runs (gevr_bot.c): its move goes out as an owner's does,
 * from its own hands and view. It has no tracked controllers, so no barrel
 * aim (the copies pose it from its view) and no hand pose.
 */
static void netSyncSendBotMove(int slot) {
    struct player *pl = g_playerPointers[slot];
    if (!pl) return;
    struct netplayermove move;
    memset(&move, 0, sizeof(move));
    move.tick = (u32)(sysGetMicroseconds() / 1000);
    for (int hand = GUNRIGHT; hand <= GUNLEFT; hand++) {
        bool enabled = hand == GUNRIGHT || netActiveDualWield();
        if (enabled && !pl->bonddead && pl->hands[hand].weapon_firing_status)
            move.ucmd |= hand == GUNLEFT ? UCMD_FIRE_LEFT : UCMD_FIRE;
    }
    if (pl->crouchpos != CROUCH_STAND) move.ucmd |= UCMD_DUCK;
    move.movespeed[0] = pl->speedforwards;
    move.movespeed[1] = pl->speedsideways;
    move.angles[0] = pl->vv_theta;
    move.angles[1] = pl->vv_verta;
    move.weaponnum = (s8)pl->hands[GUNRIGHT].weaponnum;
    move.weaponnum_left = netActiveDualWield() ? (s8)pl->hands[GUNLEFT].weaponnum : ITEM_UNARMED;
    move.crouchpos = (s8)pl->crouchpos;
    move.health = pl->bondhealth;
    move.armour = pl->bondarmour;
    move.dead = pl->bonddead ? 1 : 0;
    move.pos = pl->prop ? pl->prop->pos : pl->pos;
    netSendOwnedMove(slot, &move);
}

void netPlayerSyncAfterTick(s32 playernum) {
    if (!netIsActive()) return;
    /* This tick ran the level's players: their structs are valid for voice. */
    netVoicePlayersTick();

    int local_slot = netGetLocalSlot();
    bool is_local_human = netSlotIsLocalHuman(playernum);
    if (!is_local_human && netSlotOwned(playernum)) {
        netSyncSendBotMove(playernum);
        return;
    }
    if (!is_local_human) {
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
                if (!remote->bonddead) netSyncCopyOrientation(remote, m);
            }
            if (remote->prop->chr && s_remote_ground_valid[playernum])
                remote->prop->chr->ground = s_remote_ground[playernum];
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

    if (playernum == local_slot) {
        static int last_phase = -1;
        static unsigned last_connected = 0;
        unsigned connected = 0;
        const NetMsgLobbyState *lobby = netGetLobbyState();
        int phase = netGetPhase();
        for (int slot = 0; slot < GEVR_MAX_PLAYERS; slot++)
            if (lobby->slots[slot].connected) connected |= 1u << slot;
        {
            const char *gnotice = netTakeLocalGuestNotice();
            if (gnotice && gnotice[0]) hudmsgTopShow((char *)gnotice);
        }
        /* Runs for the local spectator too, before the movement-send guard.
         * Repeat reminders while menus are closed; a transient join notice
         * must not hide the host's request for the rest of warmup. */
        {
            static char last_notice[128];
            static u64 next_notice_us;
            char notice[128];u64 now=sysGetMicroseconds();
            netRoundNoticeTextFit(notice,sizeof(notice),gevrLocalViewCount()>=3);
            {
                /* the headset has no START: its Menu opens the Multiplayer Options folder in the warmup */
                extern s32 g_gevrStereo;
                char *go=g_gevrStereo?strstr(notice,"START: "):NULL;
                if(go) { memmove(go+6,go+7,strlen(go+7)+1);memcpy(go,"MENU: ",6); }
            }
            if(notice[0] && (strcmp(notice,last_notice) || now>=next_notice_us)) {
                extern void gevrHudTopReplace(const char *mess, const char *prefix);
                gevrHudTopReplace(notice,last_notice[0]?last_notice:notice);
                next_notice_us=now+5000000ull;
            }
            snprintf(last_notice,sizeof(last_notice),"%s",notice);
        }
        /* The host's countdown to the next round (net_core.c
         * netScheduleRound): a top message each second, a fade to black over
         * its last second, and a fade in once the new stage has loaded.
         * Applied to every local split-screen viewport. */
        {
            extern void currentPlayerSetFadeColour(s32 r, s32 g, s32 b, f32 frac);
            extern void currentPlayerSetFadeFrac(f32 maxfadetime, f32 frac);
            static s32 last_sec = -1;
            static s32 fading = 0;
            u64 end = netGetCountdownEndUs();
            u64 now = sysGetMicroseconds();
            if (netTakeStageFadeIn()) {
                s32 prev = get_cur_playernum();
                for (int s = 0; s < netLocalSlotCount(); s++) {
                    int sl = netSlotOfLocalSeat(s);
                    if (sl >= 0 && g_playerPointers[sl]) {
                        set_cur_player(sl);
                        currentPlayerSetFadeColour(0, 0, 0, 1.0f);
                        currentPlayerSetFadeFrac(60.0f, 0.0f);
                    }
                }
                set_cur_player(prev);
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
                    s32 prev = get_cur_playernum();
                    for (int s = 0; s < netLocalSlotCount(); s++) {
                        int sl = netSlotOfLocalSeat(s);
                        if (sl >= 0 && g_playerPointers[sl]) {
                            set_cur_player(sl);
                            currentPlayerSetFadeColour(0, 0, 0, 0.0f);
                            currentPlayerSetFadeFrac(60.0f, 1.0f);
                        }
                    }
                    set_cur_player(prev);
                    fading = 1;
                }
            } else {
                if (fading) {
                    s32 prev = get_cur_playernum();
                    for (int s = 0; s < netLocalSlotCount(); s++) {
                        int sl = netSlotOfLocalSeat(s);
                        if (sl >= 0 && g_playerPointers[sl]) {
                            set_cur_player(sl);
                            currentPlayerSetFadeFrac(15.0f, 0.0f);
                        }
                    }
                    set_cur_player(prev);
                    fading = 0;
                }
                last_sec = -1;
            }
        }
        if (last_phase >= 0) {
            if (last_phase != NET_PHASE_IN_PROGRESS && phase == NET_PHASE_IN_PROGRESS)
                hudmsgTopShow("MATCH STARTED");
            for (int slot = 0; slot < GEVR_MAX_PLAYERS; slot++) {
                if (!netSlotIsLocalHuman(slot) && (connected & (1u << slot)) &&
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
    if (!pl || netSlotIsSpectator(playernum)) return;
    
    struct netplayermove move;
    memset(&move, 0, sizeof(move));
    
    move.tick = (u32)(sysGetMicroseconds() / 1000);
    move.ucmd = 0;
    {
        extern s32 g_gevrStereo;
        extern s32 gevrHandChopSwinging(s32 ctrl);
        extern int gevrVrTriggerDown[2];
        extern s32 gevrStereoShotWorld(s32 handnum, coord3d *origin, coord3d *dir);
        extern s32 lvlGetControlsLockedFlag(void);   /* lv.c */
        bool vr_self = (g_gevrStereo != 0) && (playernum == local_slot);
        for (int hand = GUNRIGHT; hand <= GUNLEFT; hand++) {
            int item = getCurrentPlayerWeaponId(hand);
            bool enabled = hand == GUNRIGHT || netActiveDualWield();
            bool trig = vr_self ? (gevrVrTriggerDown[hand] != 0) : (pl->hands[hand].weapon_firing_status != 0);
            bool fire = enabled && !pl->bonddead && !pl->mpmenuon && !lvlGetControlsLockedFlag() && trig;
            if (vr_self && enabled && (item == ITEM_FIST || item == ITEM_KNIFE)) fire |= !pl->mpmenuon && gevrHandChopSwinging(hand == GUNRIGHT ? 1 : 0);
            else if (item == ITEM_UNARMED || item == ITEM_TRIGGER ||
                     (pl->hands[hand].weapon_ammo_in_magazine <= 0 && !bondwalkItemCheckBitflags(item, WEAPONSTATBITFLAG_CLICKY))) fire = false;
            if (fire) move.ucmd |= hand == GUNLEFT ? UCMD_FIRE_LEFT : UCMD_FIRE;
            if (vr_self && enabled && gevrStereoShotWorld(hand, hand == GUNLEFT ? &move.aimorigin_l : &move.aimorigin,
                                                          hand == GUNLEFT ? &move.aimdir_l : &move.aimdir))
                move.ucmd |= hand == GUNLEFT ? UCMD_AIMVALID_LEFT : UCMD_AIMVALID;
        }
    }
    if (pl->crouchpos != CROUCH_STAND) {
        move.ucmd |= UCMD_DUCK;
    }
    if (netSlotChoosing(playernum)) {
        move.ucmd |= UCMD_CHOOSING;   /* the others' co-op guards and revives (net_core.c netSlotChoosingSeen) */
    }
    
    move.movespeed[0] = pl->speedforwards;
    move.movespeed[1] = pl->speedsideways;
    
    /* Head (HMD) / camera orientation */
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
    
    {
        extern s32 g_gevrStereo;
        if (g_gevrStereo && playernum == local_slot) {
            gevrAvatarCapture(&move.avatar, pl);

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
        }
    }

    if (netSyncLocalActivity(playernum, pl->speedforwards, pl->speedsideways, &move)) netTouchLocalActivity();

    /* The owner's side of the copy's pose line above, to compare against. */
    if (playernum == local_slot) {
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

    netSendOwnedMove(playernum, &move);
}
