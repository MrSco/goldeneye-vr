/* Run with tools/test_net_round.py after configuring the Android release build.
 * Exercise the production state machine and serializers without booting the ROM.
 * Transport/time/voice are fakes; game and ENet functions outside these tests are
 * discarded by the linker's function-section garbage collection. */
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>
#include "../../port/src/net/net_core.c"

static u64 test_now;
static int resets, starts, voice_clears;
s32 g_gameOverFlag;
struct player_data g_playerPlayerData[MAX_PLAYER_COUNT]; /* round scores reset at warmup -> match */
unsigned VrMpFavStages, VrMpFavSets;
u64 sysGetMicroseconds(void) { return test_now; }
void sysLogPrintf(s32 level, const char *fmt, ...) { (void)level; (void)fmt; }
void netVoiceForgetSlot(uint8_t slot) { (void)slot; voice_clears++; }
void netPlayersTickedReset(void) {}
static int s_hudmsg_count, s_lobby_stops, s_launcher_restarts;
void hudmsgTopShow(char *mess) { (void)mess; s_hudmsg_count++; }
void gevrLobbySessionStopped(void) { s_lobby_stops++; }
void gevrRestartToLauncher(void) { s_launcher_restarts++; }
ENetPacket *enet_packet_create(const void *data, size_t size, uint32_t flags) {
    ENetPacket *p = calloc(1, sizeof(*p));
    p->data = malloc(size); memcpy(p->data, data, size); p->dataLength = size; p->flags = flags;
    return p;
}
int enet_peer_send(ENetPeer *peer, uint8_t channel, ENetPacket *packet) {
    (void)peer; (void)channel;
    if (packet->data[6] == NET_MSG_ROUND_RESET) resets++;
    if (packet->data[6] == NET_MSG_ROUND_PHASE && packet->data[8] == NET_PHASE_IN_PROGRESS) starts++;
    free(packet->data); free(packet);
    return 0;
}

static ENetPeer peer1;

/* The host's clock probe answered (net_core.c netReceiveClock): rounds wait
 * for every connected client's clock since v0.3.7, and a sample stays fresh
 * for NET_CLOCK_FRESH_US, which the host's 2 s probe keeps up in a match. */
static void clock_sync(void) {
    assert(netClockSample(&s_clock_sync[1], test_now - 400, test_now - 300, test_now - 200, test_now));
}

/* A round reset reloads the stage under a new combat epoch: every headset
 * reports STAGE_READY and slot 1 answers the new epoch's clock probe. */
static void stage_reloaded(void) {
    for (int i=0;i<2;i++) s_lobby_state.slots[i].loaded = 1;
    clock_sync();
}

static void session(void) {
    test_now = 1000000;
    s_state = NET_STATE_INGAME; s_local_slot = s_host_slot = 0;
    s_phase = NET_PHASE_WARMUP; s_host = (ENetHost *)1;
    netResetLobbyState();
    for (int i=0;i<2;i++) {
        s_lobby_state.slots[i].connected = s_lobby_state.slots[i].ready = 1;
        s_lobby_state.slots[i].loaded = 1;
        s_lobby_state.slots[i].chr_id = i;
        for (int k=0;k<4;k++) s_lobby_state.slots[i].loadout[k] = ITEM_TT33;
    }
    memset(&peer1, 0, sizeof(peer1));
    peer1.state = ENET_PEER_STATE_CONNECTED; peer1.data = (void *)(intptr_t)2; /* slot 1 */
    s_client_peers[1] = &peer1;
    memset(s_clock_sync, 0, sizeof(s_clock_sync));
    netLatchRoundSettings(); netClearVotes(-1);
    s_round_reset_pending = s_round_reset_loading = s_start_after_load = false;
    s_next_round_at_us = s_countdown_end_us = 0;
    g_gameOverFlag = 0;
    resets = starts = voice_clears = 0;
}

static void test_round_flow(void) {
    session();
    netReadyProgress(); assert(netCountdownSecondsLeft() == 0); /* no clock from slot 1 yet */
    clock_sync(); netReadyProgress();
    assert(netCountdownSecondsLeft() == 10 && resets == 0);
    test_now += 9999999; netRoundTick(); assert(resets == 0);
    test_now++; netRoundTick(); assert(resets == 1 && netTakeRoundReset());
    netRoundTick(); assert(resets == 1 && !netTakeRoundReset());
    s_lobby_state.slots[0].ready = s_lobby_state.slots[1].ready = 1;
    netReadyProgress(); assert(s_phase == NET_PHASE_WARMUP && starts == 0); /* still loading */
    stage_reloaded(); netReadyProgress(); assert(s_phase == NET_PHASE_IN_PROGRESS && starts == 1);
    netHostRoundEnded(); u64 deadline = s_results_deadline_us;
    netHostRoundEnded(); assert(s_results_deadline_us == deadline);
    test_now = deadline-1; netRoundTick(); assert(!s_next_round_at_us);
    test_now++; netRoundTick(); assert(netCountdownSecondsLeft() == 20 && resets == 1);
    u64 end = s_next_round_at_us; netHostContinue(); assert(s_next_round_at_us == end);
    s_vote[NET_BALLOT_STAGE][1] = 1;
    s_vote[NET_BALLOT_WEAPONS][1] = 5;
    test_now = end-1; netRoundTick(); assert(s_round.config.stage == 27);
    test_now++; netRoundTick(); assert(resets == 2 && s_round.config.stage == 31 && s_round.config.weapon_set == 5);
    s_lobby_state.slots[0].ready = s_lobby_state.slots[1].ready = 1;
    stage_reloaded(); netReadyProgress(); assert(starts == 2);
    netHostReturnToLobby(); assert(resets == 3 && s_lobby_open && s_round.config.stage == 31);
    s_lobby_state.slots[0].ready = s_lobby_state.slots[1].ready = 1;
    stage_reloaded(); netReadyProgress(); netReadyProgress(); assert(!s_next_round_at_us && starts == 2);
    netHostStartRoundNow(); assert(netCountdownSecondsLeft() == 10);
    s_local_slot = 1; end = s_next_round_at_us; netHostReturnToLobby();
    assert(s_next_round_at_us == end && resets == 3); /* client cannot control rounds */
}

static void test_settings_and_wire(void) {
    session(); s_lobby_state.config.loadouts = 1; netLatchRoundSettings();
    s_lobby_state.config.stage = 34;
    s_lobby_state.config.dual_wield = NET_DUAL_ANY;
    s_lobby_state.slots[1].loadout[0] = ITEM_LASER;
    s_lobby_state.slots[1].chr_id = 63;
    assert(s_round.config.stage == 27 && netActiveDualWield() == 0);
    assert(netActiveLoadoutItem(1,0) == ITEM_TT33 && s_round.character[1] == 1);
    u8 raw[256]; struct netbuf buf = {.data=raw,.size=sizeof(raw)};
    netbufStartWrite(&buf); netWriteRoundSettings(&buf); u32 size=buf.wp;
    NetRoundSettings read; netbufStartReadData(&buf,raw,size);
    assert(netReadRoundSettings(&buf,&read) && !netbufReadLeft(&buf));
    assert(read.config.stage == 27 && read.loadout[1][0] == ITEM_TT33);
    for (u32 n=0;n<size;n++) { netbufStartReadData(&buf,raw,n); assert(!netReadRoundSettings(&buf,&read)); }
    /* the stage byte follows the combat identity (epoch and every slot's life) */
    u8 idraw[64]; struct netbuf id = {.data=idraw,.size=sizeof(idraw)};
    netbufStartWrite(&id); netWriteCombatIdentity(&id); assert(!id.error && id.wp < size);
    raw[id.wp]=255; netbufStartReadData(&buf,raw,size); assert(!netReadRoundSettings(&buf,&read));
    netLatchRoundSettings();
    assert(netActiveLoadoutItem(1,0) == ITEM_LASER && s_round.character[1] == 63 && netActiveDualWield() == NET_DUAL_ANY);
    struct netplayermove sent={0}, received={0};
    sent.weaponnum=ITEM_TT33; sent.weaponnum_left=ITEM_LASER;
    sent.ucmd=UCMD_FIRE_LEFT|UCMD_AIMVALID_LEFT; sent.aimorigin_l.x=123; sent.aimdir_l.z=-1;
    buf.size=sizeof(raw); netbufStartWrite(&buf); netbufWritePlayerMove(&buf,&sent); size=buf.wp;
    netbufStartReadData(&buf,raw,size); netbufReadPlayerMove(&buf,&received);
    assert(!buf.error && !netbufReadLeft(&buf));
    assert(received.weaponnum_left == ITEM_LASER && received.aimorigin_l.x == 123 && received.ucmd == sent.ucmd);
}

static void test_ballots_roles_rotation(void) {
    session();
    s_vote[0][0]=1; s_vote[0][1]=0; assert(netTallyBallot(0)==1); /* lowest-slot tie */
    /* slot 6 is past Egypt's four and Bunker II's six (net_match.c), not Facility's eight */
    s_lobby_state.slots[6].connected=1; s_lobby_state.slots[6].spectator=1;
    assert(!netStageEligible(9) && !netStageEligible(8) && netStageEligible(0));
    assert(netGetPlayingCount()==2 && netGetConnectedPlayerCount()==3);
    assert(netVoiceSameGroup(0,6)); /* warmup: everyone hears everyone (v0.3.7) */
    s_phase = NET_PHASE_IN_PROGRESS;
    assert(!netVoiceSameGroup(0,6) && netVoiceSameGroup(0,1));
    s_phase = NET_PHASE_WARMUP;
    assert(netRotationPick(0,0,1u<<9,NET_NEXT_PLAYLIST)==1); /* favorites too small: all eligible */
    s_lobby_state.slots[6].connected=0;
    assert(netRotationPick(0,0,(1u<<0)|(1u<<9),NET_NEXT_PLAYLIST)==9);
    assert(netRotationPick(0,9,(1u<<0)|(1u<<9),NET_NEXT_PLAYLIST)==0);
    for (int i=0;i<100;i++) { test_now++; assert(netRotationPick(0,9,(1u<<0)|(1u<<9),NET_NEXT_SHUFFLE)==0); }
    s_lobby_state.config.scenario=SCENARIO_MWTGG; s_vote[1][0]=0;
    netResolveVotes(); assert(s_lobby_state.config.weapon_set==13 && netGetVote(0,0)==-1);
    s_lobby_state.slots[1].spectator=1;
    netBeginRoundReset(false); assert(!s_lobby_state.slots[1].spectator && voice_clears>=4);
    /* A migrated host may occupy any slot, and slot zero may be a player again. */
    s_local_slot=s_host_slot=1; assert(netIsHost() && netPlayerInRound(0));
}

static void test_idle_timeout(void) {
    session();
    s_last_local_activity_us = test_now;
    s_hudmsg_count = s_lobby_stops = s_launcher_restarts = 0;
    test_now += 269ULL * 1000000ULL;
    netRoundTick();
    assert(s_hudmsg_count == 0 && s_lobby_stops == 0 && s_launcher_restarts == 0);

    test_now += 1ULL * 1000000ULL;
    netRoundTick();
    assert(s_hudmsg_count == 1 && s_lobby_stops == 0 && s_launcher_restarts == 0);

    netTouchLocalActivity();
    s_hudmsg_count = 0;
    test_now += 10ULL * 1000000ULL;
    netRoundTick();
    assert(s_hudmsg_count == 0);

    test_now += 300ULL * 1000000ULL;
    netRoundTick();
    assert(s_lobby_stops == 1 && s_launcher_restarts == 1);
}

int main(void) {
    test_round_flow(); test_settings_and_wire(); test_ballots_roles_rotation(); test_idle_timeout();
    puts("PASS: round transitions, pending settings, truncated packets, both hands, ballots, rotation, roles and migration slots");
    return 0;
}
