/*
 * Online co-op (#94): the host runs the guards, the other headsets show them
 * (net_coop.c). Integer-only, like net_game.h: the game's C files include it
 * and see bool as s32, the port's as a one-byte bool. It includes nothing,
 * so it leaves a game file's own include order as it was.
 */
#ifndef _NET_COOP_H
#define _NET_COOP_H

#ifdef __cplusplus
extern "C" {
#endif

struct netbuf;
struct ChrRecord;
struct AIRecord;
struct coord3d;
struct ObjectRecord;

/* The net layer (net_core.c) */
void netCoopHostTick(void);                 /* netPoll: the guards' state, ten times a second */
void netCoopStageLoaded(void);              /* netStageLoaded: a new mission's guards */
void netCoopReceive(int type, int slot, int from_host, struct netbuf *b);
void netCoopBroadcast(const unsigned char *data, unsigned int size, int reliable);   /* host: to all; client: to the host */
int netCoopHostSlotOf(int localslot);       /* a client: this headset's guard slot -> the host's, -1 none */

/* The game */
int gevrCoopPuppets(void);                  /* a co-op client: its guards are the host's puppets */
int gevrCoopHostGuards(void);               /* the co-op host: it runs the guards */
void netCoopPuppetTick(struct ChrRecord *chr, int tickamount);    /* chr.c chrTick, a client's guard */
int gevrCoopGuardTarget(struct ChrRecord *chr);                   /* the host: the player a guard acts on */
void gevrCoopGuardProvoked(struct ChrRecord *chr, int player);
extern int g_gevrCoopGuardTick;             /* the host is running a guard as its target player */
int gevrCoopForwardGuardDamage(float damage, float vx, float vz); /* bondview2.c record_damage_kills */
void gevrCoopGuardHitPlayer(int target, float damage, float vx, float vz);
int gevrCoopGuardHitElsewhere(struct ChrRecord *chr, int hitpart, struct coord3d *vector, int weaponid);   /* 2 reported */
void gevrCoopChrSpawned(struct ChrRecord *chr, struct AIRecord *ailist, int spawnflags);   /* chraction.c chrSpawnAtCoord */
void gevrCoopChrRemoved(struct ChrRecord *chr);   /* chr.c chrTick, CHRHIDDEN_REMOVE */
void gevrCoopGuardLaunched(struct ObjectRecord *obj);   /* chraction.c: a guard's grenade or rocket, on the host */
int gevrCoopGuardExplosive(struct ObjectRecord *obj);   /* explosion.c explosionCreate: one of those, going off */
int gevrCoopDowned(int player);             /* revive (#94): the player is down, not dead */

#ifdef __cplusplus
}
#endif

#endif /* _NET_COOP_H */
