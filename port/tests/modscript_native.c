/*
 * The mods' script host (port/src/gevr_modscript.c) and Lua 5.4
 * (port/external/lua), with the game around them stubbed, behind a small
 * command language, so test_modscript.py can run each scenario and read
 * what happened:
 *
 *   modscript_test <id> <title> <command>...
 *     start            gevrModScriptStart (main.c, after romdataInit)
 *     prepare <stage>  lvlStageLoad before proplvreset2 (mission_end, mission_prepare)
 *     begin <stage>    lvlStageLoad's end (mission_start)
 *     title            the title stage loads (the mission ends)
 *     complete         the debrief (file.c)
 *     frames <n> <ticks> <us>  n frames; each clock read moves <us> on
 *     online <0|1>     netIsActive (the mission's shared seed)
 *     note             the note for the screen, taken
 *     event <name> <cancellable>  gevrModScriptEvent(name, "right", 12): CANCEL 0/1
 *     resets           how often gevrModApiReset ran (the API areas' put-back)
 *     running          gevrModScriptRunning
 *
 * Prints "LOG <level> <line>" for every log line, and the answers. Run in
 * a folder holding mods/<id>/scripts and save/.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "system.h"
#include "../external/lua/lua.h"
#include "gevr_modpack.h"
#include "gevr_modscript.h"

static gevrModInfo s_info;
static unsigned long long s_clock = 1000000, s_step = 1;
static int s_online;

s32 g_gevrStereo = 1;
int gevrVrReady(void) { return 1; }
unsigned int gevrCoopIntroSeed(void) { return 12345u; }
bool netIsActive(void) { return s_online != 0; }
const gevrModInfo *gevrModActive(void) { return s_info.m.id[0] ? &s_info : NULL; }
const char *gevrModsRoot(void) { return "mods"; }
/* the API areas (src/game/gevr_modapi.c) are test_modapi's: here only their put-back is counted */
static int s_resets;
void gevrModApiOpen(lua_State *L) { (void)L; }
void gevrModApiReset(void) { s_resets++; }

const char *fsFullPath(const char *rel)
{
    static char buf[FS_MAXPATH + 1];

    if (rel[0] == '$' && rel[1] == 'S') {
        snprintf(buf, sizeof(buf), "save%s", rel + 2);
        return buf;
    }
    return rel;
}

u64 sysGetMicroseconds(void)
{
    const unsigned long long now = s_clock;

    s_clock += s_step;
    return now;
}

s32 sysArgCheck(const char *arg)
{
    return !strcmp(arg, "--perf-log");
}

void sysLogPrintf(s32 level, const char *fmt, ...)
{
    char line[2048];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    for (char *c = line; *c; c++) {
        if (*c == '\n') *c = '|';   /* a traceback, on the one line */
    }
    printf("LOG %d %s\n", (int)level, line);
}

int main(int argc, char **argv)
{
    int i;

    if (argc < 3) return 2;
    snprintf(s_info.m.id, sizeof(s_info.m.id), "%s", argv[1]);
    snprintf(s_info.m.title, sizeof(s_info.m.title), "%s", argv[2]);
    snprintf(s_info.m.version, sizeof(s_info.m.version), "1.0.0");
    s_info.m.kind = GEVR_MOD_CAMPAIGN;
    for (i = 3; i < argc; i++) {
        const char *cmd = argv[i];

        if (!strcmp(cmd, "start")) {
            gevrModScriptStart();
        } else if (!strcmp(cmd, "prepare") && i + 1 < argc) {
            gevrModScriptStageLoad(atoi(argv[++i]), 1, 0);
        } else if (!strcmp(cmd, "begin") && i + 1 < argc) {
            gevrModScriptStageLoad(atoi(argv[++i]), 1, 1);
        } else if (!strcmp(cmd, "title")) {
            gevrModScriptStageLoad(0, 0, 0);
        } else if (!strcmp(cmd, "complete")) {
            gevrModScriptMissionComplete();
        } else if (!strcmp(cmd, "frames") && i + 3 < argc) {
            const int n = atoi(argv[i + 1]), ticks = atoi(argv[i + 2]);
            const unsigned long long step = strtoull(argv[i + 3], NULL, 10);

            i += 3;
            s_step = step;
            for (int f = 0; f < n; f++) gevrModScriptFrame(ticks);
            s_step = 1;
        } else if (!strcmp(cmd, "online") && i + 1 < argc) {
            s_online = atoi(argv[++i]);
        } else if (!strcmp(cmd, "note")) {
            const char *note = gevrModScriptTakeNote();

            printf("NOTE %s\n", note ? note : "-");
        } else if (!strcmp(cmd, "event") && i + 2 < argc) {
            GevrModArg args[2];
            const int cancellable = atoi(argv[i + 2]);

            args[0].kind = GEVR_MODARG_STRING;
            args[0].s = "right";
            args[1].kind = GEVR_MODARG_INTEGER;
            args[1].n = 12;
            printf("CANCEL %d\n", gevrModScriptEvent(argv[i + 1], args, 2, cancellable));
            i += 2;
        } else if (!strcmp(cmd, "resets")) {
            printf("RESETS %d\n", s_resets);
        } else if (!strcmp(cmd, "running")) {
            printf("RUNNING %d\n", gevrModScriptRunning());
        } else {
            fprintf(stderr, "unknown command %s\n", cmd);
            return 2;
        }
        fflush(stdout);
    }
    gevrModScriptShutdown();
    return 0;
}
