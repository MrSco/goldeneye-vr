#include "net_core.h"
#include "net_coop.h"
#include "net_horde.h"
#include "game/gevr_ally.h"
#include "system.h"
#include <math.h>

void netHordeSend(int slot)
{
    if (!netHordeActive() || !netIsHost()) return;
    const HordeState *s = gevrHordeState();
    unsigned char raw[1024];
    struct netbuf b = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&b);
    netbufWriteU32(&b, GEVR_NET_MAGIC); netbufWriteU16(&b, GEVR_NET_VERSION);
    netbufWriteU8(&b, NET_MSG_HORDE_STATE); netbufWriteU8(&b, netGetLocalSlot());
    /* A run epoch rejects a snapshot from the preceding retry. */
    netbufWriteU32(&b, netGetWorldEpoch());
    const uint32_t values[] = {s->revision,s->wave,s->completed,s->remaining,s->alive,s->kills,s->revives,
        s->timer,s->spawn_timer,s->next_id,s->rng,s->elapsed,
        s->credits[0],s->credits[1],s->credits[2],s->credits[3],
        s->grant_seq[0],s->grant_seq[1],s->grant_seq[2],s->grant_seq[3],s->box_timer};
    for (unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++) netbufWriteU32(&b,values[i]);
    netbufWriteU8(&b,s->box_state); netbufWriteU8(&b,s->box_spot);
    netbufWriteU8(&b,s->box_buyer); netbufWriteU8(&b,s->box_weapon); netbufWriteU8(&b,s->box_uses);
    for (unsigned i=0;i<4;i++) { netbufWriteU8(&b,s->grant_kind[i]); netbufWriteU8(&b,s->grant_item[i]); }
    for (unsigned i=0;i<4;i++) {
        netbufWriteU32(&b,s->ammo_grants[i]);
        for (unsigned g=0;g<HORDE_GUN_SLOTS;g++) netbufWriteU8(&b,s->guns[i][g]);
        netbufWriteU8(&b,s->grant_replace[i]);
    }
    for (unsigned i=0;i<HORDE_AMMO_DROPS;i++) {
        netbufWriteU32(&b,s->ammo[i].id); netbufWriteU32(&b,s->ammo[i].ticks);
        for (unsigned k=0;k<3;k++) netbufWriteF32(&b,s->ammo[i].pos[k]);
    }
    netbufWriteU32(&b,s->instakill_ticks); netbufWriteU32(&b,s->max_ammo_seq);
    netbufWriteU32(&b,s->power_seq); netbufWriteU32(&b,s->power_kind);
    for (unsigned i=0;i<4;i++) for (unsigned k=0;k<2;k++) netbufWriteU32(&b,s->explosives[i][k]);
    for (unsigned i=0;i<HORDE_POWER_DROPS;i++) {
        netbufWriteU32(&b,s->powers[i].id); netbufWriteU32(&b,s->powers[i].ticks);
        for (unsigned k=0;k<3;k++) netbufWriteF32(&b,s->powers[i].pos[k]);
        netbufWriteU32(&b,s->powers[i].kind);
    }
    netbufWriteU8(&b,s->phase); netbufWriteU8(&b,s->players);
    if (slot < 0) netCoopBroadcast(b.data,b.wp,1);
    else netCoopSendTo(slot,b.data,b.wp);
}

void netHordeReceive(struct netbuf *b)
{
    uint32_t epoch=netbufReadU32(b);
    HordeState s={0};
    uint32_t *fields[] = {&s.revision,&s.wave,&s.completed,&s.remaining,&s.alive,&s.kills,&s.revives,
        &s.timer,&s.spawn_timer,&s.next_id,&s.rng,&s.elapsed,
        &s.credits[0],&s.credits[1],&s.credits[2],&s.credits[3],
        &s.grant_seq[0],&s.grant_seq[1],&s.grant_seq[2],&s.grant_seq[3],&s.box_timer};
    for (unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++) *fields[i]=netbufReadU32(b);
    s.box_state=netbufReadU8(b); s.box_spot=netbufReadU8(b);
    s.box_buyer=netbufReadU8(b); s.box_weapon=netbufReadU8(b); s.box_uses=netbufReadU8(b);
    for (unsigned i=0;i<4;i++) { s.grant_kind[i]=netbufReadU8(b); s.grant_item[i]=netbufReadU8(b); }
    for (unsigned i=0;i<4;i++) {
        s.ammo_grants[i]=netbufReadU32(b);
        for (unsigned g=0;g<HORDE_GUN_SLOTS;g++) s.guns[i][g]=netbufReadU8(b);
        s.grant_replace[i]=netbufReadU8(b);
    }
    for (unsigned i=0;i<HORDE_AMMO_DROPS;i++) {
        s.ammo[i].id=netbufReadU32(b); s.ammo[i].ticks=netbufReadU32(b);
        for (unsigned k=0;k<3;k++) {
            s.ammo[i].pos[k]=netbufReadF32(b);
            if (!isfinite(s.ammo[i].pos[k]) || fabsf(s.ammo[i].pos[k])>1000000.0f) return;
        }
        if (s.ammo[i].ticks>HORDE_AMMO_LIFETIME || (s.ammo[i].id && !s.ammo[i].ticks)) return;
        for (unsigned j=0;j<i;j++) if (s.ammo[i].id && s.ammo[i].id==s.ammo[j].id) return;
    }
    s.instakill_ticks=netbufReadU32(b); s.max_ammo_seq=netbufReadU32(b);
    s.power_seq=netbufReadU32(b); s.power_kind=netbufReadU32(b);
    for (unsigned i=0;i<4;i++) for (unsigned k=0;k<2;k++) s.explosives[i][k]=netbufReadU32(b);
    for (unsigned i=0;i<HORDE_POWER_DROPS;i++) {
        s.powers[i].id=netbufReadU32(b); s.powers[i].ticks=netbufReadU32(b);
        for (unsigned k=0;k<3;k++) {
            s.powers[i].pos[k]=netbufReadF32(b);
            if (!isfinite(s.powers[i].pos[k]) || fabsf(s.powers[i].pos[k])>1000000.0f) return;
        }
        s.powers[i].kind=netbufReadU32(b);
    }
    s.phase=netbufReadU8(b); s.players=netbufReadU8(b);
    if (b->error || netbufReadLeft(b) || epoch!=netGetWorldEpoch() || !hordeStateValid(&s)) return;
    gevrHordeApplyState(&s);
}

void netHordeRequestAction(unsigned action, unsigned item)
{
    if (!netHordeRunning()) return;
    if (netIsHost()) {
        if (gevrHordeHandleAction(netGetLocalSlot(), action, item)) netHordeSend(-1);
        return;
    }
    unsigned char raw[24];
    struct netbuf b = { .data = raw, .size = sizeof(raw) };
    netbufStartWrite(&b);
    netbufWriteU32(&b, GEVR_NET_MAGIC); netbufWriteU16(&b, GEVR_NET_VERSION);
    netbufWriteU8(&b, NET_MSG_HORDE_STATE); netbufWriteU8(&b, netGetLocalSlot());
    netbufWriteU32(&b, netGetWorldEpoch());
    netbufWriteU8(&b, (uint8_t)action); netbufWriteU32(&b, item);
    netCoopBroadcast(b.data, b.wp, 1);
}

void netHordeReceiveClient(int slot, struct netbuf *b)
{
    if (!netHordeRunning() || !netIsHost() || slot < 0 || slot >= 4) return;
    uint32_t epoch = netbufReadU32(b);
    uint8_t action = netbufReadU8(b);
    uint32_t item = netbufReadU32(b);
    if (b->error || netbufReadLeft(b) || epoch != netGetWorldEpoch()) return;
    if (gevrHordeHandleAction(slot, action, item)) netHordeSend(-1);
}

void netHordeTick(void)
{
    static uint64_t sent;
    uint64_t now=sysGetMicroseconds();
    if (netHordeRunning() && netIsHost() && now-sent>=500000) {
        sent=now; netHordeSend(-1);
    }
}

uint64_t gevrHordeGuardToken(const struct ChrRecord *chr) {
    unsigned id=netHordeActive() && gevrAllyIs((struct ChrRecord *)chr) ? GEVR_ALLY_HORDE_ID : gevrHordeGuardId(chr);
    return id ? ((uint64_t)netGetWorldEpoch()<<32)|id : 0;
}
int gevrHordeTokenCurrent(uint64_t token) {
    return (uint32_t)(token>>32)==netGetWorldEpoch() && (uint32_t)token!=0;
}
