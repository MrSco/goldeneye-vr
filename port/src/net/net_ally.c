/* Companion preferences and lifecycle follow the host, including a late join
 * after the companion's death. Position, guns and combat use normal NPC sync. */
#include "net_core.h"
#include "net_coop.h"
#include "net_ally.h"
#include "game/gevr_ally.h"
#include "system.h"
#include <string.h>

static uint32_t s_revision;
static uint64_t s_sent_us;
static bool s_was_host, s_sent;
static GevrAllyState s_last;

void netAllyWriteState(struct netbuf *b, const GevrAllyState *state)
{
    netbufWriteU8(b, state->enabled); netbufWriteU8(b, state->weapon);
    netbufWriteU8(b, state->aggression); netbufWriteU8(b, state->invincible);
    netbufWriteU16(b, state->health); netbufWriteU16(b, state->damage);
    netbufWriteU8(b, state->spawned); netbufWriteS16(b, state->identity);
}

int netAllyReadState(struct netbuf *b, GevrAllyState *state)
{
    state->enabled = netbufReadU8(b); state->weapon = netbufReadU8(b);
    state->aggression = netbufReadU8(b); state->invincible = netbufReadU8(b);
    state->health = netbufReadU16(b); state->damage = netbufReadU16(b);
    state->spawned = netbufReadU8(b); state->identity = netbufReadS16(b);
    return !b->error && gevrAllyStateValid(state);
}

void netAllyReset(void)
{
    s_revision = 0; s_sent_us = 0;
    s_was_host = s_sent = false;
    memset(&s_last, 0, sizeof(s_last));
}

void netAllySend(int slot)
{
    if (!netCoopActive() || !netIsHost()) return;
    /* A replacement host must outrun any old host snapshot another client
     * saw more recently, as with the Horde director's revision. */
    if (!s_was_host) { s_revision += 0x10000u; s_was_host = true; }
    GevrAllyState state;
    gevrAllyGetState(&state);
    unsigned char raw[32];
    struct netbuf b = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&b);
    netbufWriteU32(&b, GEVR_NET_MAGIC); netbufWriteU16(&b, GEVR_NET_VERSION);
    netbufWriteU8(&b, NET_MSG_COOP_ALLY); netbufWriteU8(&b, netGetLocalSlot());
    netbufWriteU32(&b, netGetWorldEpoch()); netbufWriteU32(&b, ++s_revision);
    netAllyWriteState(&b, &state);
    if (slot < 0) netCoopBroadcast(b.data, b.wp, 1);
    else netCoopSendTo(slot, b.data, b.wp);
    s_last = state; s_sent = true; s_sent_us = sysGetMicroseconds();
}

void netAllyHostTick(void)
{
    if (!netCoopActive() || !netIsHost()) return;
    GevrAllyState state;
    gevrAllyGetState(&state);
    if (!s_was_host || !s_sent || memcmp(&state, &s_last, sizeof(state))
        || sysGetMicroseconds() - s_sent_us >= 1000000ull) netAllySend(-1);
}

void netAllyReceive(struct netbuf *b)
{
    if (!netCoopActive() || netIsHost()) return;
    uint32_t epoch = netbufReadU32(b), revision = netbufReadU32(b);
    GevrAllyState state;
    if (!netAllyReadState(b, &state) || netbufReadLeft(b) || epoch != netGetWorldEpoch()
        || (int32_t)(revision - s_revision) < 0) return;
    s_revision = revision; s_was_host = false;
    gevrAllyApplyState(&state);
}
