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
struct WeaponObjRecord;
struct PropRecord;

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
int gevrCoopGuardHitElsewhere(struct ChrRecord *chr, int hitpart, struct coord3d *vector, int weaponid);   /* 2 reported */
void gevrCoopChrSpawned(struct ChrRecord *chr, struct AIRecord *ailist, int spawnflags);   /* chraction.c chrSpawnAtCoord */
void gevrCoopChrIdentityChanged(struct ChrRecord *chr); /* cloning assigns/reassigns IDs after the initial spawn */
void gevrCoopChrRemoved(struct ChrRecord *chr);   /* chr.c chrTick, CHRHIDDEN_REMOVE */
void gevrCoopGuardLaunched(struct ObjectRecord *obj);   /* chraction.c: a guard's grenade or rocket, on the host */
int gevrCoopGuardExplosive(struct ObjectRecord *obj);   /* explosion.c explosionCreate: one of those, going off */
int gevrCoopGuardBlastNow(void);            /* explosion.c: the host is running a guard: its blast is the world's */

/* Revive: down, not dead, until a teammate stands beside you (net_coop.c) */
int gevrCoopDowned(int player);             /* the player is down */
int gevrCoopLocalDowned(void);              /* this headset's player, as the current player, is down: no control */
int gevrCoopGoDown(void);                   /* bondview2.c record_damage_kills: down instead of dead (TRUE) */
void netCoopReviveTick(void);               /* netPoll: being revived; the host: everyone down fails the mission */
void netCoopSlotLeft(int slot);             /* the host: a player left; not down, holding nothing */

/* Drop-in and a host change (net_core.c) */
void netCoopSendTo(int slot, const unsigned char *data, unsigned int size);   /* the host: to one player, reliably */
void netCoopPlayerJoined(int slot, int returning);   /* the host: a player loaded in (STAGE_READY) */
void netCoopHostLost(int oldhost, int elected);      /* netHostLost */
void netCoopBecameHost(void);               /* netHostTakeOver: the AI resumes here */

/* Doors the host's guards, scripts and timers move or lock (net_core.c) */
void netSendHostDoorState(struct ObjectRecord *door, int state);   /* propobj.c doorActivate */
void netSendHostDoorLock(struct ObjectRecord *door);               /* chrai.c AI_DoorSetLock / UnsetLock */

/* The mission: the host's, shown everywhere (objective_status.c, chrai.c, gunfire.c) */
int gevrCoopHostObjectiveStatus(int objective);   /* a teammate's headset: the host's status */
int gevrCoopTeammateHolds(int tag);         /* the host: a teammate's player holds this objective item */
void gevrCoopReportRoom(int room);          /* a teammate's headset: its player's objective events */
void gevrCoopReportDeposit(int item, int room);
void gevrCoopReportPhoto(int tag);
void gevrCoopReportKeyCopy(void);
void gevrCoopReportAlarm(int on);           /* propobj.c propobjInteract: an alarm switch */
void gevrCoopGrantItem(int item);           /* a shared mission gadget, into every occupied slot */
void gevrCoopReportGadgetUse(struct ObjectRecord *obj); /* gunfire.c: a client used a gadget on this */
int gevrCoopThrownMissionItem(int item);    /* a mine or other throw the mission script watches */
int gevrCoopDeferRemoteMineSettle(struct WeaponObjRecord *wep); /* host: wait for the thrower (#130) */
void gevrCoopReportMineSettled(struct WeaponObjRecord *wep, struct PropRecord *onto);
int gevrCoopNearestPlayer(const struct coord3d *pos);   /* background scripts: nearest living player */
void gevrCoopAiText(int top, int textid);   /* the host: a mission script's message, for everyone */
int gevrCoopObjectiveSnapshot(unsigned char *statuses, int max, int localslot);   /* objective_status.c */
int gevrCoopHeldObjectiveTags(int *tags, int max, int localslot);
void gevrCoopApplyPhoto(int tag);
int gevrCoopAnyCopiedKey(void);
void netCoopClientTick(void);               /* netPoll, a teammate's headset: the items its player holds */

/* The party's menus: the solo front end, the host driving (net_coop_menu.c) */
int gevrCoopSession(void);                  /* a co-op party, in its menus or a mission */
int gevrCoopIsHost(void);                   /* ...and this headset hosts it */
unsigned int gevrCoopIntroSeed(void);       /* the mission's intro camera, the same on every headset */
int gevrWatchController(void);              /* options.c: the solo watch reads this headset's controller */
/* The host's scripted cutscenes on every headset (net_coop.c, bondview2.c) */
enum { NET_COOP_CINEMA_NOCONTROL = 1, NET_COOP_CINEMA_CAMERA = 2, NET_COOP_CINEMA_MASK = 3 };
int gevrCoopCinemaCollect(float *pos, float *pos2, int *pad, unsigned char *rgb, float *frac);   /* the host */
int netCoopCinemaState(float *pos, float *pos2, int *pad, unsigned char *rgb, float *frac);     /* a teammate */
void gevrCoopCinemaTick(void);              /* bondview2.c: this headset's player follows the host's */
int gevrCoopCinemaCamera(float *pos, float *pos2, void **stan, float *arg6);   /* its camera, the host's */
int gevrCoopCinemaHides(int player);        /* another player's copy stays out of the shot */
void gevrCoopCinemaReset(void);             /* a stage's load */

/* The party's tally of the last mission, for the statistics page (front.c; net_core.c) */
int gevrCoopTallyCount(void);               /* slots to look at, 0 none */
int gevrCoopTallyPlayed(int slot);
int gevrCoopTallyKills(int slot);
const char *gevrCoopTallyName(int slot);
void netCoopMissionEnded(int result);       /* net_core.c: the mission ended here (NET_COOP_RESULT_*) */
void gevrCoopMenuTick(void);                /* front.c menu_init: the host's screen out, the others' in */
void gevrCoopPrepareSaveFolder(void);       /* stage load: keep a local folder even when its picker was skipped */
int gevrCoopMenuFollowing(void);            /* this headset shows the host's screen: no input of its own */
void netCoopReceiveMenu(struct netbuf *b);  /* net_core.c: NET_MSG_COOP_MENU */
void netCoopMenuReset(void);                /* a stage's load: wait for the host's word again */
void netCoopHostStartMission(int stage, int difficulty);   /* front.c init_menu0B_runstage, the host */

#ifdef __cplusplus
}
#endif

#endif /* _NET_COOP_H */
