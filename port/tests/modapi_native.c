/*
 * The mod API's weapons and items areas (port/src/gevr_modapi.c) end to
 * end: the real script host (gevr_modscript.c) and Lua, over a fake game
 * standing in for src/game/gevr_modgame.c (two hands, a stats table, an
 * inventory and spare rounds, local or not), driven as the gun code and the
 * stage's setup drive it, so test_modapi.py can check each rule.
 *
 *   modapi_test <command>...
 *     start | prepare <stage> | begin <stage> | title   as modscript_native.c
 *     local <0|1>                  the current player is this machine's human
 *     hold <hand> <item> <rounds>  what a hand holds
 *     fire <hand>                  gunfire.c: ask, then spend a round if not declined
 *     reload <hand> <rounds>       the native reload: asked, then start, the rounds in, end
 *     autoreload <hand> <reserve>  gunfire.c's automatic reload: ASKED unless still declined
 *     draw <hand> <item>           a switch's end
 *     empty <hand>                 the out-of-ammo switch asks: KEPT or SWITCH
 *     look <hand> <0|1|2>          the models the hand's gun is drawn with
 *     show <item>                  the item's stats now
 *     loadedis <hand>              the hand's rounds now
 *     weapon|box <index> <id>      prop.c: a weapon (item) or ammo box (type) about to be placed
 *     crate <index> <t:n,...>      an ammo crate: rounds by type
 *     armour <index> <amount>
 *     took <kind> [id] [rounds]    propPickupByPlayer: weapon <item>, ammo <type> <rounds>,
 *                                  crate <t:n,...>, armour <amount>, key, object
 *     own <item> | inv             an item in the inventory; what's there
 *     ammois <type>                the spare rounds of a type
 *     torso <0|1>                  VR hands in play (the torso's frame) and haptics, or not
 *     handat <hand> <x> <y> <z>    a hand's place in the torso's frame (hand 0 right)
 *     untracked <hand>
 *     grip <ctrl> <anchor> <taken> | release <ctrl> <anchor> | near <ctrl> <anchor>
 *                                  bondview2.c's grip tick, gevr_bodyslots.c's reach (ctrl 1 the gun hand)
 *     spawning <id> <index> <body> <head> <weapon>   expand_09_characters: guard_spawning (-1 none)
 *     addguard <id> <body> <head> <alive> <w> <wl> <health> <max> <x> <y> <z>   a guard in the stage
 *     alerted <id> <how> <player> | killed <id> <how> <item> <player> | retiring <id> <x> <y> <z>
 *     online <active> <host> <dedicated> <slot>   the network as this machine sees it
 *     netrecv <mod> <from> <channel> <text> | netoff <mod> <from> | peerready <slot>
 *     frame                        a frame in play (gevrModScriptFrame: the held messages go out)
 *     slotid|slottaken|slotfreed <kind> <slot>   gevr_modgame.c: a runtime pickup named (SLOTID id),
 *                                  taken by this player, freed (propobj.c objFree)
 *     panel <0|1>                  this build has the in-game screen (input.c gevrPanelAvailable)
 *     ui                           the pages the in-game screen shows now (UIPAGE, UIROW, UIBUTTON, UIASKED)
 *     uiset <page> <row> <value> | uiclick <page> <button> | uiopened   the screen's side
 *     runs <0|1>                   this machine runs the guards (solo, the co-op host) in a stage
 *     gspawned <id>                chrai.c: a guard the game spawned
 *     hurt <target> <id> <amount> <how> <part> <item> <by>    before_damage, then
 *                                  damage_applied with what's left (DAMAGE amount|cancelled)
 *     downed <slot> <0|1> | died <slot>
 *     vitals <health> <armour> <dead> <down>   this machine's player
 *
 * Prints LOG lines, DECLINED/FIRED per shot, SPAWN per pickup placed, and
 * the answers. Run in a
 * folder holding mods/tactical/scripts and save/.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "system.h"
#include "gevr_modpack.h"
#include "gevr_modscript.h"
#include "gevr_modapi.h"

/* ---- the host's surroundings, as modscript_native.c ---- */
static gevrModInfo s_info;
static unsigned long long s_clock = 1000000;
s32 g_gevrStereo = 1;
int gevrVrReady(void) { return 1; }
unsigned int gevrCoopIntroSeed(void) { return 12345u; }
static int s_online, s_hostNet, s_dedicated, s_slot;
bool netIsActive(void) { return s_online != 0; }
bool netIsHost(void) { return s_hostNet != 0; }
bool netIsDedicated(void) { return s_dedicated != 0; }
int netGetLocalSlot(void) { return s_slot; }
/* input.c: the in-game screen (mod.ui.open and close) */
static int s_panel = 1;
int gevrPanelAvailable(void) { return s_panel; }
void gevrPanelOpenAsk(void) { printf("PANEL open\n"); }
void gevrPanelCloseAsk(void) { printf("PANEL close\n"); }

/* What the in-game screen would draw */
static void showPages(void)
{
    const int n = gevrModUiCount();
    int i, r, c;

    for (i = 0; i < n; i++) {
        const GevrModUiPage *p = gevrModUiPage(i);

        printf("UIPAGE %d %s %s %s|%s\n", i, p->mod, p->id, p->title, p->text);
        for (r = 0; r < p->rows; r++) {
            const GevrModUiRow *w = &p->row[r];

            printf("UIROW %d %d %d %s %s|%d|%d|%d|", i, r, w->kind, w->id, w->label, w->value, w->min, w->max);
            for (c = 0; c < w->count; c++) printf("%s%s", c ? "," : "", w->choices[c]);
            printf("|%s|%s\n", w->blocked, w->tip);
        }
        for (r = 0; r < p->buttons; r++) printf("UIBUTTON %d %d %s %s|%s\n", i, r, p->button[r].id, p->button[r].label, p->button[r].blocked);
    }
    printf("UIASKED %d of %d\n", gevrModUiAsked(), n);
}

int netSendModMessage(int kind, int to, const char *mod, const char *channel, const void *data, int len)
{
    int i;

    if (to == s_slot) return 0;
    printf("NETSEND %d %d %s %s %d", kind, to, mod, channel ? channel : "-", len);
    for (i = 0; i < len; i++) printf(" %02x", ((const unsigned char *)data)[i]);
    printf("\n");
    return 1;
}
const gevrModInfo *gevrModActive(void) { return &s_info; }
const char *gevrModsRoot(void) { return "mods"; }
const char *fsFullPath(const char *rel)
{
    static char buf[FS_MAXPATH + 1];

    if (rel[0] == '$' && rel[1] == 'S') {
        snprintf(buf, sizeof(buf), "save%s", rel + 2);
        return buf;
    }
    return rel;
}
u64 sysGetMicroseconds(void) { return s_clock++; }
s32 sysArgCheck(const char *arg) { (void)arg; return 0; }
void sysLogPrintf(s32 level, const char *fmt, ...)
{
    char line[2048];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    for (char *c = line; *c; c++) {
        if (*c == '\n') *c = '|';
    }
    printf("LOG %d %s\n", (int)level, line);
}

/* ---- the fake game (src/game/gevr_modgame.c's part) ---- */
static int s_local = 1;
static int s_held[2] = { 4, 0 }, s_loaded[2] = { 7, 0 };
static GevrModWeaponStats s_stats[GEVR_MODAPI_ITEMS];
static int s_hasStats[GEVR_MODAPI_ITEMS];

int gevrModGameLocal(void) { return s_local; }
int gevrModGamePlayerReady(void) { return 1; }
int gevrModGameHeld(int hand) { return s_held[hand]; }
static int s_look[2];
void gevrModGameScope(int hand, int mode, int item, const float at[3], float radius, float zoomDeg)
{
    printf("SCOPE %d %d %d", hand, mode, item);
    if (mode == GEVR_MODSCOPE_MOD) printf(" %.1f %.1f %.1f %.1f %.1f", at[0], at[1], at[2], radius, zoomDeg);
    printf("\n");
}
int gevrModGameLook(int hand) { return s_look[hand]; }
/* gun.c's per-hand models: a mod's files are the names starting Gtac */
int gevrModGameGunModel(int hand, int item, const char *name)
{
    printf("GUNMODEL %d %d %s\n", hand, item, name ? name : "-");
    return name == NULL || strncmp(name, "Gtac", 4) == 0;
}
void gevrModGameGunModelReset(void) { printf("GUNMODEL reset\n"); }
/* gevr_modattach.c: models starting Gno won't load, nor a file past the
 * mission's GEVR_MODAPI_ATTACH_MODELS; the offset printed whole (-0 as 0) */
static int s_attachNext = 1, s_attached, s_attachFiles;
static char s_attachFile[GEVR_MODAPI_ATTACH_MODELS][64];
static int attachFileLoads(const char *model)
{
    int i;

    for (i = 0; i < s_attachFiles; i++) {
        if (!strcmp(s_attachFile[i], model)) return 1;
    }
    if (s_attachFiles >= GEVR_MODAPI_ATTACH_MODELS) return 0;
    snprintf(s_attachFile[s_attachFiles++], sizeof(s_attachFile[0]), "%s", model);
    return 1;
}
int gevrModGameAttach(const GevrModAttach *a)
{
    int id = strncmp(a->model, "Gno", 3) == 0 ? 0 : s_attached >= GEVR_MODAPI_ATTACH_MAX ? -1
           : !attachFileLoads(a->model) ? 0 : s_attachNext++;
    int i;

    if (id > 0) s_attached++;
    printf("ATTACH %d %d %d %d %s %s", id, a->on, a->where, a->node, a->model, a->like);
    for (i = 0; i < 4; i++) {
        printf(" |%.0f %.0f %.0f %.0f", a->offset[i][0] + 0.0f, a->offset[i][1] + 0.0f, a->offset[i][2] + 0.0f,
               a->offset[i][3] + 0.0f);
    }
    printf("\n");
    return id;
}
int gevrModGameDetach(int id) { printf("DETACH %d\n", id); s_attached--; return 1; }
int gevrModGameAttachShow(int id, int shown) { printf("ATTACHSHOW %d %d\n", id, shown); return 1; }
int gevrModGameAttachPlace(int id, const float offset[4][4])
{
    int i;

    printf("ATTACHPLACE %d", id);
    for (i = 0; i < 4; i++) {
        printf(" |%.0f %.0f %.0f %.0f", offset[i][0] + 0.0f, offset[i][1] + 0.0f, offset[i][2] + 0.0f, offset[i][3] + 0.0f);
    }
    printf("\n");
    return 1;
}
void gevrModGameAttachReset(void) { printf("ATTACH reset\n"); s_attached = 0; s_attachFiles = 0; }
int gevrModGameLoaded(int hand) { return s_loaded[hand]; }
void gevrModGameSetLoaded(int hand, int rounds) { s_loaded[hand] = rounds; }
int gevrModGameStats(int item, GevrModWeaponStats *out)
{
    if (item < 0 || item >= GEVR_MODAPI_ITEMS || !s_hasStats[item]) return 0;
    *out = s_stats[item];
    return 1;
}
void gevrModGameSetStats(int item, const GevrModWeaponStats *in)
{
    if (item >= 0 && item < GEVR_MODAPI_ITEMS && s_hasStats[item]) s_stats[item] = *in;
}

/* the items area: guns and thrown weapons as gevr_modgame.c lists them for
 * GoldenEye (ITEM_KNIFE to ITEM_ROCKETLAUNCH but the watch laser, the
 * grenade and the mines); gun.c's ammo_related maxima */
static int s_inv[GEVR_MODAPI_ITEMS];
static int s_ammo[GEVR_MODAPI_AMMO_TYPES + 1];
static const int s_ammoMax[GEVR_MODAPI_AMMO_TYPES + 1] = { 0, 800, 200, 400, 100, 12, 3, 10, 10, 10, 10, 12, 200, 100 };

int gevrModGameSwappable(int item) { return (item >= 2 && item <= 25 && item != 23) || (item >= 26 && item <= 29); }
int gevrModGameHas(int item) { return s_inv[item]; }
int gevrModGameGive(int item)
{
    const int given = !s_inv[item];

    s_inv[item] = 1;
    if (item == 29) s_inv[30] = 1;   /* remote mines bring the detonator */
    return given;
}
int gevrModGameTake(int item)
{
    const int had = s_inv[item];

    s_inv[item] = 0;
    if (s_held[0] == item) s_held[0] = 1;   /* the gun hand to its fist */
    if (s_held[1] == item) s_held[1] = 0;
    return had;
}
int gevrModGameAmmo(int type) { return s_ammo[type]; }
/* the game's drops: a weapon slot's id or a box slot's (as gevr_modgame.c
 * names them), none from guard 999 */
static int s_dropWeapons, s_dropBoxes;
int gevrModGameDrop(int item, int ammo, int from)
{
    printf("DROP %d %d %d\n", item, ammo, from);
    if (from == 999) return -1;
    return item ? gevrModApiPickupSlotId(GEVR_MODAPI_PICKUP_WEAPON, s_dropWeapons++)
                : gevrModApiPickupSlotId(GEVR_MODAPI_PICKUP_BOX, s_dropBoxes++);
}
void gevrModGameSetAmmo(int type, int rounds) { s_ammo[type] = rounds; }
int gevrModGameAmmoMax(int type) { return type >= 1 && type <= GEVR_MODAPI_AMMO_TYPES ? s_ammoMax[type] : 0; }

/* the hands area: the torso's frame, two hands, the six anchors */
static int s_torso = 1, s_tracked[2] = { 1, 1 };
static float s_handAt[2][3];
static const float s_anchorAt[GEVR_MODAPI_ANCHORS][4] = {
    { 18, -55, 5, 12 }, { -18, -55, 5, 12 }, { 15, -5, -20, 15 }, { -15, -5, -20, 15 }, { 0, -30, 15, 14 }, { -10, -60, 15, 10 },
};

int gevrModGameHandAt(int hand, float out[3])
{
    if (!s_torso || !s_tracked[hand]) return 0;
    memcpy(out, s_handAt[hand], sizeof(s_handAt[hand]));
    return 1;
}
int gevrModGameAnchorAt(int anchor, float out[3], float *radius)
{
    if (!s_torso) return 0;
    memcpy(out, s_anchorAt[anchor], 3 * sizeof(float));
    *radius = s_anchorAt[anchor][3];
    return 1;
}
int gevrModGameHaptic(int hand, float amplitude, float seconds)
{
    if (!s_torso) return 0;
    printf("HAPTIC %d %.2f %.2f\n", hand, amplitude, seconds);
    return 1;
}

/* the guards area: GoldenEye's split of the body table (bodies, then heads
 * 46 to 87, the last row a body); bodies 5 and 6 have their own heads */
static GevrModGuardState s_guards[8];
static int s_guardCount;

int gevrModGameBodyOk(int body)
{
    if (body < 0 || body > 88 || body == 45 || (body >= 46 && body != 88)) return 0;
    return body == 5 || body == 6 ? 2 : 1;
}
int gevrModGameHeadOk(int head) { return head >= 46 && head < 88; }
int gevrModGameGuardGun(int item) { return item >= 4 && item <= 25 && item != 23; }

/* sound and smoke: a bank of 120; guard 99 isn't there */
int gevrModGameSoundCount(void) { return 120; }
void gevrModGameSetSpeed(float scale) { printf("SPEED %.2f\n", scale); }
int gevrModGamePlaySound(int id, float volume, const float *pos)
{
    if (pos) printf("SOUND %d %.2f %g %g %g\n", id, volume, pos[0], pos[1], pos[2]);
    else printf("SOUND %d %.2f -\n", id, volume);
    return 1;
}
int gevrModGameSmoke(int type, int at, int which)
{
    if (at == GEVR_MODAT_GUARD && which == 99) return 0;
    printf("SMOKE %d %d %d\n", type, at, which);
    return 1;
}

/* the damage and player area: this machine's player */
static float s_health = 1.0f, s_armour = 0.0f;
static int s_dead, s_down;

int gevrModGamePlayerHealth(float *health, float *armour)
{
    *health = s_health;
    *armour = s_armour;
    return 1;
}
int gevrModGameSetPlayerHealth(float health)
{
    if (s_dead || s_down) return 0;
    s_health = health;
    return 1;
}
int gevrModGameSetPlayerArmour(float armour)
{
    if (s_dead || s_down) return 0;
    s_armour = armour;
    return 1;
}

/* guards part 2: spawns take the next id; pad 999 has no room; paths 3 and 4 */
static int s_runsGuards = 1, s_nextGuard = 5000;

int gevrModGameRunsGuards(void) { return s_runsGuards; }
int gevrModGameAiListOk(int id) { return id == 2 || id == 5 || id == 7 || id == 13 || id == 1025; }
int gevrModGameSpawnGuard(const GevrModGuardOrder *o)
{
    GevrModGuardState *g;

    if (!s_runsGuards || o->pad == 999 || s_guardCount >= 8) return -1;
    printf("SPAWNGUARD pad %d body %d head %d ai %d weapons %d %d\n", o->pad, o->body, o->head, o->ai, o->weapon[0],
           o->weapon[1]);
    g = &s_guards[s_guardCount++];
    memset(g, 0, sizeof(*g));
    g->id = s_nextGuard;
    g->body = o->body;
    g->head = o->head;
    g->alive = 1;
    g->weapon[0] = o->weapon[0];
    g->weapon[1] = o->weapon[1];
    g->maxHealth = g->health = 1;
    return s_nextGuard++;
}
int gevrModGameGiveGuardWeapon(int id, int item, int hand)
{
    int i;

    for (i = 0; i < s_guardCount; i++) {
        if (s_guards[i].id == id && s_guards[i].alive) {
            s_guards[i].weapon[hand] = item;
            printf("GIVEGUARD %d %d %d\n", id, item, hand);
            return 1;
        }
    }
    return 0;
}
int gevrModGamePatrol(int id, int path)
{
    int i;

    for (i = 0; i < s_guardCount; i++) {
        if (s_guards[i].id == id) return s_guards[i].alive && (path == 3 || path == 4);
    }
    return 0;
}
/* three pads and a bound one: pad i at (100 i, 0, -10 i) in room 5 + i, bound 10000 at (999, 1, 2) in no room */
int gevrModGamePadCount(int *bound)
{
    *bound = 1;
    return 3;
}
int gevrModGamePad(int pad, GevrModPad *out)
{
    if (pad == 10000) {
        out->pos[0] = 999; out->pos[1] = 1; out->pos[2] = 2; out->room = -1;
    } else if (pad >= 0 && pad < 3) {
        out->pos[0] = 100.0f * pad; out->pos[1] = 0; out->pos[2] = -10.0f * pad; out->room = 5 + pad;
    } else {
        return 0;
    }
    out->look[0] = 0; out->look[1] = 0; out->look[2] = 1;
    return 1;
}
static int guardAlive(int id)
{
    int i;

    for (i = 0; i < s_guardCount; i++) {
        if (s_guards[i].id == id) return s_guards[i].alive;
    }
    return 0;
}
int gevrModGameGoTo(int id, int pad, int speed)
{
    printf("GOTO %d %d %d\n", id, pad, speed);
    return guardAlive(id) && (pad < 3 || pad == 10000);
}
int gevrModGameDifficulty(void) { return 2; }   /* 00 Agent */
int gevrModGameFolder(void) { return 3; }       /* the fourth folder */
static int s_modCampaign = 1;
int gevrModGameComplete(int mission, int difficulty, int seconds)
{
    printf("COMPLETE %d %d %d\n", mission, difficulty, seconds);
    return s_modCampaign;
}
int gevrModGameGiveHat(int id, int hat)
{
    printf("HAT %d %d\n", id, hat);
    return guardAlive(id);
}
int gevrModGameFace(int id, int pad)
{
    printf("FACE %d %d\n", id, pad);
    return guardAlive(id) && (pad < 3 || pad == 10000);
}
int gevrModGamePaths(int *ids, int max)
{
    if (max < 2) return 0;
    ids[0] = 3;
    ids[1] = 4;
    return 2;
}
int gevrModGameGuards(int *ids, int max)
{
    int i;

    for (i = 0; i < s_guardCount && i < max; i++) ids[i] = s_guards[i].id;
    return i;
}
int gevrModGameGuard(int id, GevrModGuardState *out)
{
    int i;

    for (i = 0; i < s_guardCount; i++) {
        if (s_guards[i].id == id) {
            *out = s_guards[i];
            return 1;
        }
    }
    return 0;
}

static void crateArg(GevrModPickup *p, const char *list)
{
    while (*list) {
        int t = 0, n = 0, used = 0;

        if (sscanf(list, "%d:%d%n", &t, &n, &used) != 2) break;
        if (t >= 1 && t <= GEVR_MODAPI_AMMO_TYPES) p->crate[t] = n;
        list += used;
        if (*list == ',') list++;
    }
}

static void spawned(const GevrModPickup *p)
{
    int t;

    if (p->none) {
        printf("SPAWN %d none\n", p->index);
        return;
    }
    switch (p->kind) {
    case GEVR_MODPICKUP_WEAPON: printf("SPAWN %d weapon %d\n", p->index, p->item); break;
    case GEVR_MODPICKUP_AMMO: printf("SPAWN %d ammo %d\n", p->index, p->ammo); break;
    case GEVR_MODPICKUP_ARMOUR: printf("SPAWN %d armour %.2f\n", p->index, p->armour); break;
    default:
        printf("SPAWN %d crate", p->index);
        for (t = 1; t <= GEVR_MODAPI_AMMO_TYPES; t++) {
            if (p->crate[t]) printf(" %d:%d", t, p->crate[t]);
        }
        printf("\n");
    }
}

static void stats(int item, int magazine, int rate, float damage)
{
    s_hasStats[item] = 1;
    s_stats[item].magazine = magazine;
    s_stats[item].fireRate = rate;
    s_stats[item].damage = damage;
    s_stats[item].automatic = rate >= 0;
}

int main(int argc, char **argv)
{
    int i;

    snprintf(s_info.m.id, sizeof(s_info.m.id), "tactical");
    snprintf(s_info.m.title, sizeof(s_info.m.title), "Tactical");
    snprintf(s_info.m.version, sizeof(s_info.m.version), "1.0.0");
    s_info.m.kind = GEVR_MOD_CAMPAIGN;
    stats(4, 7, -1, 1.0f);     /* wppk: single shot */
    stats(8, 30, 3, 1.5f);     /* ak47: automatic */
    stats(26, 1, -1, 4.0f);    /* grenade */
    for (i = 1; i < argc; i++) {
        const char *cmd = argv[i];

        if (!strcmp(cmd, "start")) {
            gevrModScriptStart();
        } else if (!strcmp(cmd, "prepare") && i + 1 < argc) {
            gevrModScriptStageLoad(atoi(argv[++i]), 1, 0);
        } else if (!strcmp(cmd, "begin") && i + 1 < argc) {
            gevrModScriptStageLoad(atoi(argv[++i]), 1, 1);
        } else if (!strcmp(cmd, "title")) {
            gevrModScriptStageLoad(0, 0, 0);
        } else if (!strcmp(cmd, "local") && i + 1 < argc) {
            s_local = atoi(argv[++i]);
        } else if (!strcmp(cmd, "hold") && i + 3 < argc) {
            const int hand = atoi(argv[i + 1]);

            s_held[hand] = atoi(argv[i + 2]);
            s_loaded[hand] = atoi(argv[i + 3]);
            i += 3;
        } else if (!strcmp(cmd, "fire") && i + 1 < argc) {
            /* gunfire.c: ask before anything is spent; declined, it clicks dry */
            const int hand = atoi(argv[++i]);

            if (gevrModApiFireAttempt(hand, s_held[hand])) {
                printf("DECLINED %d\n", s_loaded[hand]);
            } else {
                s_loaded[hand] -= 1;
                gevrModApiFired(hand, s_held[hand]);
                printf("FIRED %d\n", s_loaded[hand]);
            }
        } else if (!strcmp(cmd, "reload") && i + 2 < argc) {
            const int hand = atoi(argv[i + 1]);

            if (gevrModApiReloadAttempt(hand, s_held[hand], 30)) {
                printf("RELOAD DECLINED %d\n", s_loaded[hand]);
            } else {
                gevrModApiReloadStart(hand, s_held[hand]);
                s_loaded[hand] = atoi(argv[i + 2]);
                gevrModApiReloadEnd(hand, s_held[hand]);
            }
            i += 2;
        } else if (!strcmp(cmd, "autoreload") && i + 2 < argc) {
            /* gunfire.c: an empty gun asks each tick, unless the last answer still holds */
            const int hand = atoi(argv[i + 1]);
            const int reserve = atoi(argv[i + 2]);

            if (gevrModApiReloadDeclined(hand, s_held[hand], reserve)) {
                printf("AUTO HELD\n");
            } else {
                printf("AUTO %s\n", gevrModApiReloadAttempt(hand, s_held[hand], reserve) ? "DECLINED" : "RELOADS");
            }
            i += 2;
        } else if (!strcmp(cmd, "look") && i + 2 < argc) {
            s_look[atoi(argv[i + 1])] = atoi(argv[i + 2]);
            i += 2;
        } else if (!strcmp(cmd, "empty") && i + 1 < argc) {
            const int hand = atoi(argv[++i]);

            printf("%s\n", gevrModApiOutOfAmmo(hand, s_held[hand]) ? "KEPT" : "SWITCH");
        } else if (!strcmp(cmd, "draw") && i + 2 < argc) {
            const int hand = atoi(argv[i + 1]);

            s_held[hand] = atoi(argv[i + 2]);
            gevrModApiDrawn(hand, s_held[hand]);
            i += 2;
        } else if (!strcmp(cmd, "show") && i + 1 < argc) {
            const int item = atoi(argv[++i]);

            printf("STATS %d %d %d %.2f\n", item, s_stats[item].magazine, s_stats[item].fireRate, s_stats[item].damage);
        } else if (!strcmp(cmd, "show2") && i + 1 < argc) {
            const GevrModWeaponStats *s = &s_stats[atoi(argv[++i])];

            printf("STATS2 %.2f %.2f %.2f %d %.2f %.2f %d %d\n", s->inaccuracy, s->recoilBack, s->recoilUp, s->sound,
                   s->loudnessMin, s->loudnessMax, s->automatic, s->fireRate);
        } else if (!strcmp(cmd, "loadedis") && i + 1 < argc) {
            printf("LOADED %d\n", s_loaded[atoi(argv[++i])]);
        } else if ((!strcmp(cmd, "weapon") || !strcmp(cmd, "box") || !strcmp(cmd, "crate") || !strcmp(cmd, "armour"))
                   && i + 2 < argc) {
            /* prop.c: a pickup's record about to be placed */
            GevrModPickup p;

            memset(&p, 0, sizeof(p));
            p.index = atoi(argv[i + 1]);
            if (cmd[0] == 'w') {
                p.kind = GEVR_MODPICKUP_WEAPON;
                p.item = atoi(argv[i + 2]);
            } else if (cmd[0] == 'b') {
                p.kind = GEVR_MODPICKUP_AMMO;
                p.ammo = atoi(argv[i + 2]);
            } else if (cmd[0] == 'c') {
                p.kind = GEVR_MODPICKUP_CRATE;
                crateArg(&p, argv[i + 2]);
            } else {
                p.kind = GEVR_MODPICKUP_ARMOUR;
                p.armour = (float)atof(argv[i + 2]);
            }
            i += 2;
            gevrModApiPickupSpawning(&p);
            spawned(&p);
        } else if (!strcmp(cmd, "took") && i + 1 < argc) {
            /* propPickupByPlayer: the local player took it */
            GevrModPickup got;
            const char *kind = argv[++i];

            memset(&got, 0, sizeof(got));
            got.pickup = 77;   /* the pickup's id, passed through as item_pickup's fourth */
            if (!strcmp(kind, "weapon") && i + 1 < argc) {
                got.kind = GEVR_MODPICKUP_WEAPON;
                got.item = atoi(argv[++i]);
            } else if (!strcmp(kind, "ammo") && i + 2 < argc) {
                got.kind = GEVR_MODPICKUP_AMMO;
                got.ammo = atoi(argv[i + 1]);
                got.rounds = atoi(argv[i + 2]);
                i += 2;
            } else if (!strcmp(kind, "crate") && i + 1 < argc) {
                got.kind = GEVR_MODPICKUP_CRATE;
                crateArg(&got, argv[++i]);
            } else if (!strcmp(kind, "armour") && i + 1 < argc) {
                got.kind = GEVR_MODPICKUP_ARMOUR;
                got.armour = (float)atof(argv[++i]);
            } else {
                got.kind = !strcmp(kind, "key") ? GEVR_MODPICKUP_KEY : GEVR_MODPICKUP_OBJECT;
            }
            gevrModApiItemPickup(&got);
        } else if (!strcmp(cmd, "campaign") && i + 1 < argc) {
            s_modCampaign = atoi(argv[++i]);   /* a mod campaign runs (its own save) */
        } else if (!strcmp(cmd, "slotid") && i + 2 < argc) {
            printf("SLOTID %d\n", gevrModApiPickupSlotId(atoi(argv[i + 1]), atoi(argv[i + 2])));
            i += 2;
        } else if (!strcmp(cmd, "slottaken") && i + 2 < argc) {
            gevrModApiPickupSlotTaken(atoi(argv[i + 1]), atoi(argv[i + 2]));
            i += 2;
        } else if (!strcmp(cmd, "slotfreed") && i + 2 < argc) {
            gevrModApiPickupSlotFreed(atoi(argv[i + 1]), atoi(argv[i + 2]));
            i += 2;
        } else if (!strcmp(cmd, "dropped") && i + 3 < argc) {
            /* propobjSetDropped: a chr's weapon falls */
            gevrModApiWeaponDropped(atoi(argv[i + 1]), atoi(argv[i + 2]), atoi(argv[i + 3]));
            i += 3;
        } else if (!strcmp(cmd, "own") && i + 1 < argc) {
            s_inv[atoi(argv[++i])] = 1;
        } else if (!strcmp(cmd, "inv")) {
            int k;

            printf("INV");
            for (k = 0; k < GEVR_MODAPI_ITEMS; k++) {
                if (s_inv[k]) printf(" %d", k);
            }
            printf(" held %d %d\n", s_held[0], s_held[1]);
        } else if (!strcmp(cmd, "torso") && i + 1 < argc) {
            s_torso = atoi(argv[++i]);
        } else if (!strcmp(cmd, "handat") && i + 4 < argc) {
            const int hand = atoi(argv[i + 1]);

            s_tracked[hand] = 1;
            s_handAt[hand][0] = (float)atof(argv[i + 2]);
            s_handAt[hand][1] = (float)atof(argv[i + 3]);
            s_handAt[hand][2] = (float)atof(argv[i + 4]);
            i += 4;
        } else if (!strcmp(cmd, "untracked") && i + 1 < argc) {
            s_tracked[atoi(argv[++i])] = 0;
        } else if (!strcmp(cmd, "grip") && i + 3 < argc) {
            gevrModApiGrip(atoi(argv[i + 1]), atoi(argv[i + 2]), atoi(argv[i + 3]));
            i += 3;
        } else if (!strcmp(cmd, "release") && i + 2 < argc) {
            gevrModApiRelease(atoi(argv[i + 1]), atoi(argv[i + 2]));
            i += 2;
        } else if (!strcmp(cmd, "near") && i + 2 < argc) {
            gevrModApiHandNear(atoi(argv[i + 1]), atoi(argv[i + 2]));
            i += 2;
        } else if (!strcmp(cmd, "spawning") && i + 5 < argc) {
            GevrModGuardSpawn g;

            g.id = atoi(argv[i + 1]);
            g.index = atoi(argv[i + 2]);
            g.body = atoi(argv[i + 3]);
            g.head = atoi(argv[i + 4]);
            g.weapon = atoi(argv[i + 5]);
            i += 5;
            gevrModApiGuardSpawning(&g);
            printf("GUARD %d body %d head %d weapon %d\n", g.id, g.body, g.head, g.weapon);
        } else if (!strcmp(cmd, "addguard") && i + 11 < argc && s_guardCount < 8) {
            GevrModGuardState *g = &s_guards[s_guardCount++];

            g->id = atoi(argv[i + 1]);
            g->body = atoi(argv[i + 2]);
            g->head = atoi(argv[i + 3]);
            g->alive = atoi(argv[i + 4]);
            g->alerted = !g->alive;
            g->weapon[0] = atoi(argv[i + 5]);
            g->weapon[1] = atoi(argv[i + 6]);
            g->health = (float)atof(argv[i + 7]);
            g->maxHealth = (float)atof(argv[i + 8]);
            g->pos[0] = (float)atof(argv[i + 9]);
            g->pos[1] = (float)atof(argv[i + 10]);
            g->pos[2] = (float)atof(argv[i + 11]);
            i += 11;
        } else if (!strcmp(cmd, "alerted") && i + 3 < argc) {
            gevrModApiGuardAlerted(atoi(argv[i + 1]), atoi(argv[i + 2]), atoi(argv[i + 3]));
            i += 3;
        } else if (!strcmp(cmd, "killed") && i + 4 < argc) {
            gevrModApiGuardKilled(atoi(argv[i + 1]), atoi(argv[i + 2]), atoi(argv[i + 3]), atoi(argv[i + 4]));
            i += 4;
        } else if (!strcmp(cmd, "hurt") && i + 7 < argc) {
            GevrModDamage d;

            memset(&d, 0, sizeof(d));
            d.target = atoi(argv[i + 1]);
            d.id = atoi(argv[i + 2]);
            d.amount = (float)atof(argv[i + 3]);
            d.how = atoi(argv[i + 4]);
            d.part = atoi(argv[i + 5]);
            d.item = atoi(argv[i + 6]);
            d.by = atoi(argv[i + 7]);
            i += 7;
            gevrModApiBeforeDamage(&d);
            if (d.cancelled) {
                printf("DAMAGE cancelled\n");
            } else {
                printf("DAMAGE %.2f\n", d.amount);
                d.health = 3.0f - d.amount;
                d.armour = 0.25f;
                gevrModApiDamageApplied(&d);
            }
        } else if (!strcmp(cmd, "downed") && i + 2 < argc) {
            gevrModApiPlayerDowned(atoi(argv[i + 1]), atoi(argv[i + 2]));
            i += 2;
        } else if (!strcmp(cmd, "died") && i + 1 < argc) {
            gevrModApiPlayerDied(atoi(argv[++i]));
        } else if (!strcmp(cmd, "vitals") && i + 4 < argc) {
            s_health = (float)atof(argv[i + 1]);
            s_armour = (float)atof(argv[i + 2]);
            s_dead = atoi(argv[i + 3]);
            s_down = atoi(argv[i + 4]);
            i += 4;
        } else if (!strcmp(cmd, "online") && i + 4 < argc) {
            s_online = atoi(argv[i + 1]);
            s_hostNet = atoi(argv[i + 2]);
            s_dedicated = atoi(argv[i + 3]);
            s_slot = atoi(argv[i + 4]);
            i += 4;
        } else if (!strcmp(cmd, "netrecv") && i + 4 < argc) {
            /* the text, \0 standing for a NUL */
            char data[1100];
            int len = 0;
            const char *t = argv[i + 4];

            while (*t && len < (int)sizeof(data)) {
                if (t[0] == '\\' && t[1] == '0') {
                    data[len++] = 0;
                    t += 2;
                } else {
                    data[len++] = *t++;
                }
            }
            gevrModNetReceive(argv[i + 1], atoi(argv[i + 2]), argv[i + 3], data, len);
            i += 4;
        } else if (!strcmp(cmd, "netoff") && i + 2 < argc) {
            gevrModNetStopped(argv[i + 1], atoi(argv[i + 2]));
            i += 2;
        } else if (!strcmp(cmd, "peerready") && i + 1 < argc) {
            gevrModApiPeerReady(atoi(argv[++i]));
        } else if (!strcmp(cmd, "panel") && i + 1 < argc) {
            s_panel = atoi(argv[++i]);
        } else if (!strcmp(cmd, "ui")) {
            showPages();
        } else if (!strcmp(cmd, "uiset") && i + 3 < argc) {
            gevrModUiSet(atoi(argv[i + 1]), atoi(argv[i + 2]), atoi(argv[i + 3]));
            i += 3;
        } else if (!strcmp(cmd, "uiclick") && i + 2 < argc) {
            gevrModUiClick(atoi(argv[i + 1]), atoi(argv[i + 2]));
            i += 2;
        } else if (!strcmp(cmd, "uiopened")) {
            gevrModUiOpened();
        } else if (!strcmp(cmd, "frame")) {
            gevrModScriptFrame(1);
        } else if (!strcmp(cmd, "runs") && i + 1 < argc) {
            s_runsGuards = atoi(argv[++i]);
        } else if (!strcmp(cmd, "gspawned") && i + 1 < argc) {
            gevrModApiGuardSpawned(atoi(argv[++i]));
        } else if (!strcmp(cmd, "retiring") && i + 4 < argc) {
            const float pos[3] = { (float)atof(argv[i + 2]), (float)atof(argv[i + 3]), (float)atof(argv[i + 4]) };

            gevrModApiCorpseRetiring(atoi(argv[i + 1]), pos);
            i += 4;
        } else if (!strcmp(cmd, "ammois") && i + 1 < argc) {
            const int type = atoi(argv[++i]);

            printf("AMMO %d %d\n", type, s_ammo[type]);
        } else {
            fprintf(stderr, "unknown command %s\n", cmd);
            return 2;
        }
        fflush(stdout);
    }
    gevrModScriptShutdown();
    return 0;
}
