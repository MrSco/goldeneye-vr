/*
 * The mod API's weapons area (port/src/gevr_modapi.c) end to end: the real
 * script host (gevr_modscript.c) and Lua, over a fake game standing in for
 * src/game/gevr_modgame.c (two hands, a stats table, local or not), driven
 * as the gun code drives it, so test_modapi.py can check each rule.
 *
 *   modapi_test <command>...
 *     start | prepare <stage> | begin <stage> | title   as modscript_native.c
 *     local <0|1>                  the current player is this machine's human
 *     hold <hand> <item> <rounds>  what a hand holds
 *     fire <hand>                  gunfire.c: ask, then spend a round if not declined
 *     reload <hand> <rounds>       the native reload: start, the rounds in, end
 *     draw <hand> <item>           a switch's end
 *     show <item>                  the item's stats now
 *     loadedis <hand>              the hand's rounds now
 *
 * Prints LOG lines, DECLINED/FIRED per shot, and the answers. Run in a
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
bool netIsActive(void) { return false; }
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

static void stats(int item, int magazine, int rate, float damage)
{
    s_hasStats[item] = 1;
    s_stats[item].magazine = magazine;
    s_stats[item].fireRate = rate;
    s_stats[item].damage = damage;
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

            gevrModApiReloadStart(hand, s_held[hand]);
            s_loaded[hand] = atoi(argv[i + 2]);
            gevrModApiReloadEnd(hand, s_held[hand]);
            i += 2;
        } else if (!strcmp(cmd, "draw") && i + 2 < argc) {
            const int hand = atoi(argv[i + 1]);

            s_held[hand] = atoi(argv[i + 2]);
            gevrModApiDrawn(hand, s_held[hand]);
            i += 2;
        } else if (!strcmp(cmd, "show") && i + 1 < argc) {
            const int item = atoi(argv[++i]);

            printf("STATS %d %d %d %.2f\n", item, s_stats[item].magazine, s_stats[item].fireRate, s_stats[item].damage);
        } else if (!strcmp(cmd, "loadedis") && i + 1 < argc) {
            printf("LOADED %d\n", s_loaded[atoi(argv[++i])]);
        } else {
            fprintf(stderr, "unknown command %s\n", cmd);
            return 2;
        }
        fflush(stdout);
    }
    gevrModScriptShutdown();
    return 0;
}
