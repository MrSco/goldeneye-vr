/*
 * Mods' scripts (docs/mod-system.md, Scripting): Lua 5.4 (port/external/lua)
 * in a sandbox, for the mod campaign this run plays (gevr_modpack.c).
 *
 * - The sandbox: the base, coroutine, math, string, table and utf8
 *   libraries; no io, os, package or debug, no dofile or loadfile, load
 *   of text only, no string.dump. require reads the mod's own scripts/.
 * - Each mod's heap is capped (GEVR_MODSCRIPT_MEMORY_CAP); a count hook
 *   stops a runaway call; any error turns the mod off (for the mission, or
 *   for the run when it can't start), logged with its name, never the game.
 * - The budget: GEVR_MODSCRIPT_BUDGET_US a frame for all mods together,
 *   averaged over a second of play; over it for GEVR_MODSCRIPT_OVER_SECONDS
 *   in a row, the costliest mod pauses for the mission, with a note.
 *   Loading's events (campaign_enter, mission_prepare, mission_start,
 *   mission_end) aren't counted. --perf-log shows each mod's time a second.
 */
/* the C library's first: Lua's luaconf.h includes <limits.h>, and through
 * the game's include/ wrapper that leaves MB_LEN_MAX at 1, which glibc's
 * stdlib.h (the dedicated server's) refuses after it */
#include <dirent.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "gevr_modscript.h"
#include "gevr_modpack.h"
#include "fs.h"
#include "system.h"
#include "../external/lua/lua.h"
#include "../external/lua/lauxlib.h"
#include "../external/lua/lualib.h"

/* the game's, declared here so this file needs none of its headers */
extern s32 g_gevrStereo;                     /* bondview2.c: this frame is drawn in stereo */
extern int gevrVrReady(void);                /* vr_openxr.cpp; port/flat: 0 */
extern unsigned int gevrCoopIntroSeed(void); /* net_core.c: the same on every headset for the mission */
extern bool netIsActive(void);               /* net_core.c */
/* src/game/gevr_modapi.c: the API areas' tables into the mod table on the
 * stack's top (mod.weapon, mod.items...), and what the mods changed (weapon
 * stats...) back to the game's own, at a mission's end and when a mod stops */
extern void gevrModApiOpen(lua_State *L);
extern void gevrModApiReset(void);

#define MAX_MODS 4
#define PATH_CAP (FS_MAXPATH * 2)
#define LOG_LINES_A_SECOND 30

typedef struct ModScript {
    lua_State *L;
    char id[GEVR_MOD_ID_MAX + 1];
    char title[GEVR_MOD_TITLE_MAX + 1];
    char version[GEVR_MOD_VERSION_MAX + 1];
    char dir[PATH_CAP];    /* its install folder (scripts/ is in it) */
    char data[PATH_CAP];   /* mod.save's folder, outside the install so an update keeps it */
    size_t mem;
    int broken;            /* can't run this run: main.lua or campaign_enter failed */
    int off;               /* off for this mission: an error, or the budget (paused) */
    int paused;
    u64 callStart, runaway;
    u64 windowUs;          /* its play time this second */
    unsigned long long rng;
    u64 logWindow;
    int logLines;
} ModScript;

static ModScript s_mods[MAX_MODS];
static int s_count;
static int s_inMission, s_missionStage, s_missionComplete, s_playing;
static u64 s_windowStart, s_windowUs;
static int s_windowFrames, s_overSeconds;
static char s_note[192];
static int s_hasNote;
static int s_perfLog = -1;

static const char *const s_events[] = {
    "campaign_enter", "mission_prepare", "mission_start", "mission_end", "tick",
    /* the weapons area (docs/mod-system.md, Where each hook sits) */
    "weapon_fire_attempt", "weapon_fire", "reload_start", "reload_end", "weapon_drawn",
    /* the items area */
    "pickup_spawning", "item_pickup",
    /* the hands area */
    "grip", "release", "hand_near",
    /* the guards area */
    "guard_spawning", "guard_alerted", "guard_killed", "corpse_retiring", "guard_spawned",
    /* the damage and player area */
    "before_damage", "damage_applied", "player_downed", "player_revived", "player_died",
};

static void note(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(s_note, sizeof(s_note), fmt, ap);
    va_end(ap);
    s_hasNote = 1;
}

/* ---- files ---- */

static int joinPath(char *out, size_t cap, const char *a, const char *b)
{
    const int n = snprintf(out, cap, "%s/%s", a, b);

    return n >= 0 && (size_t)n < cap ? 0 : -1;
}

static int makeDirs(const char *path)
{
    char buf[PATH_CAP];
    struct stat st;
    size_t i, n = strlen(path);

    if (n == 0 || n >= sizeof(buf)) return -1;
    memcpy(buf, path, n + 1);
    for (i = 1; i <= n; i++) {
        if (buf[i] == '/' || buf[i] == '\\' || buf[i] == '\0') {
            const char c = buf[i];

            if (i == 2 && buf[1] == ':') continue;
            buf[i] = '\0';
            if (stat(buf, &st) != 0) {
#ifdef _WIN32
                _mkdir(buf);
#else
                mkdir(buf, 0777);
#endif
            }
            buf[i] = c;
        }
    }
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode) ? 0 : -1;
}

/* A whole file, at most max bytes; NULL when missing or bigger */
static char *readFile(const char *path, size_t max, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *data;
    long size;

    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || (size_t)size > max || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    data = (char *)malloc((size_t)size + 1);
    if (data && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data) {
        data[size] = '\0';
        *len = (size_t)size;
    }
    return data;
}

/* ---- the Lua state ---- */

static ModScript *modOf(lua_State *L)
{
    return *(ModScript **)lua_getextraspace(L);
}

/* Each mod's heap, capped: past the cap an allocation fails, which Lua
 * raises as "not enough memory" in the call that asked */
static void *modAlloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    ModScript *m = (ModScript *)ud;
    const size_t old = ptr ? osize : 0;
    void *p;

    if (nsize == 0) {
        free(ptr);
        m->mem -= old;
        return NULL;
    }
    if (nsize > old && m->mem - old + nsize > GEVR_MODSCRIPT_MEMORY_CAP) {
        return NULL;
    }
    p = realloc(ptr, nsize);
    if (p) {
        m->mem = m->mem - old + nsize;
    }
    return p;
}

/* Every 10,000 instructions: a call past its time is a runaway */
static void runawayHook(lua_State *L, lua_Debug *ar)
{
    ModScript *m = modOf(L);

    (void)ar;
    if (m->callStart && sysGetMicroseconds() - m->callStart > m->runaway) {
        luaL_error(L, "a call ran over %d ms (an endless loop?)", (int)(m->runaway / 1000));
    }
}

static int traceback(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);

    if (!msg) msg = luaL_typename(L, 1);
    luaL_traceback(L, L, msg, 1);
    return 1;
}

/* A script error: the mod off for the mission, or for the run while it starts */
static void modFail(ModScript *m, const char *what, const char *err)
{
    sysLogPrintf(LOG_ERROR, "mods: %s: %s: %s", m->id, what, err);
    if (s_inMission && !m->broken) {
        m->off = 1;
        gevrModApiReset();   /* what it changed goes back */
        note("%s stopped for this mission: a script error (see the log).", m->title);
    } else {
        m->broken = 1;
        note("%s's scripts didn't start (see the log); it plays without them.", m->title);
    }
}

/* The function and its nargs arguments on the stack's top, run protected:
 * 0 with its nresults results left on the stack, or -1 (an error: none) */
static int modCall(ModScript *m, int nargs, int nresults, int load, const char *what)
{
    lua_State *L = m->L;
    const int base = lua_gettop(L) - nargs;
    int rc;

    lua_pushcfunction(L, traceback);
    lua_insert(L, base);
    m->runaway = load ? GEVR_MODSCRIPT_LOAD_RUNAWAY_US : GEVR_MODSCRIPT_RUNAWAY_US;
    m->callStart = sysGetMicroseconds();
    if (!m->callStart) m->callStart = 1;
    rc = lua_pcall(L, nargs, nresults, base);
    m->callStart = 0;
    if (rc != LUA_OK) {
        const char *err = lua_tostring(L, -1);

        modFail(m, what, err ? err : rc == LUA_ERRMEM ? "out of memory" : "an error in its error handler");
        lua_pop(L, 1);
    }
    lua_remove(L, base);
    return rc == LUA_OK ? 0 : -1;
}

/* ---- the API: mod.*, print, require, load ---- */

static int l_on(lua_State *L)
{
    const char *event = luaL_checkstring(L, 1);
    size_t i, n;

    luaL_checktype(L, 2, LUA_TFUNCTION);
    for (i = 0; i < sizeof(s_events) / sizeof(s_events[0]) && strcmp(event, s_events[i]) != 0; i++) {
    }
    if (i == sizeof(s_events) / sizeof(s_events[0])) {
        return luaL_error(L, "mod.on: there is no event \"%s\"", event);
    }
    lua_getfield(L, LUA_REGISTRYINDEX, "gevr.on");
    if (lua_getfield(L, -1, event) != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, event);
    }
    n = (size_t)lua_rawlen(L, -1);
    lua_pushvalue(L, 2);
    lua_rawseti(L, -2, (lua_Integer)n + 1);
    return 0;
}

/* mod.log and print: the arguments as tostring would, on one log line */
static int l_log(lua_State *L)
{
    ModScript *m = modOf(L);
    char line[512];
    size_t used = 0;
    const int n = lua_gettop(L);
    const u64 now = sysGetMicroseconds();
    int i;

    if (now - m->logWindow > 1000000) {
        m->logWindow = now;
        m->logLines = 0;
    }
    if (++m->logLines > LOG_LINES_A_SECOND) {
        if (m->logLines == LOG_LINES_A_SECOND + 1) {
            sysLogPrintf(LOG_WARNING, "mod %s: (more lines this second left out)", m->id);
        }
        return 0;
    }
    line[0] = '\0';
    for (i = 1; i <= n && used + 1 < sizeof(line); i++) {
        size_t len;
        const char *s = luaL_tolstring(L, i, &len);
        const int w = snprintf(line + used, sizeof(line) - used, "%s%s", i > 1 ? " " : "", s);

        lua_pop(L, 1);
        if (w < 0) break;
        used += (size_t)w < sizeof(line) - used ? (size_t)w : sizeof(line) - used - 1;
    }
    sysLogPrintf(LOG_NOTE, "mod %s: %s", m->id, line);
    return 0;
}

/* mod.setting(name[, default]): the player's choice for it (settings.txt in
 * the mod's data folder, name=value lines), as the default's type; else it */
static int l_setting(lua_State *L)
{
    ModScript *m = modOf(L);
    const char *name = luaL_checkstring(L, 1);
    const size_t nameLen = strlen(name);
    char path[PATH_CAP];
    char *text = NULL, *line;
    size_t len;

    lua_settop(L, 2);
    if (joinPath(path, sizeof(path), m->data, "settings.txt") == 0) {
        text = readFile(path, 64 << 10, &len);
    }
    for (line = text; line && *line; ) {
        char *eol = strchr(line, '\n');

        if (eol) *eol = '\0';
        if (eol && eol > line && eol[-1] == '\r') eol[-1] = '\0';
        if (!strncmp(line, name, nameLen) && line[nameLen] == '=') {
            const char *value = line + nameLen + 1;

            if (lua_type(L, 2) == LUA_TNUMBER) {
                if (!lua_stringtonumber(L, value)) lua_pushvalue(L, 2);
            } else if (lua_type(L, 2) == LUA_TBOOLEAN) {
                lua_pushboolean(L, !strcmp(value, "true") || !strcmp(value, "1") || !strcmp(value, "on"));
            } else {
                lua_pushstring(L, value);
            }
            free(text);
            return 1;
        }
        line = eol ? eol + 1 : NULL;
    }
    free(text);
    lua_pushvalue(L, 2);
    return 1;
}

static int l_capabilities(lua_State *L)
{
    const int vr = g_gevrStereo != 0;

    lua_createtable(L, 0, 4);
    lua_pushboolean(L, vr);
    lua_setfield(L, -2, "vr");
    lua_pushboolean(L, vr && gevrVrReady());
    lua_setfield(L, -2, "hands");
#ifdef GEVR_HEADLESS
    lua_pushboolean(L, 1);
#else
    lua_pushboolean(L, 0);
#endif
    lua_setfield(L, -2, "headless");
    lua_pushstring(L,
#if defined(GEVR_HEADLESS)
                   "server"
#elif defined(GEVR_VISIONOS)
                   "visionos"
#elif defined(GEVR_TVOS)
                   "tvos"
#elif defined(GEVR_IOS)
                   "ios"
#elif defined(ANDROID) && defined(GEVR_MOBILE)
                   "android"
#elif defined(ANDROID)
                   "quest"
#else
                   "pc"
#endif
    );
    lua_setfield(L, -2, "platform");
    return 1;
}

static unsigned long long nextRandom(ModScript *m)
{
    unsigned long long z = (m->rng += 0x9E3779B97F4A7C15ull);

    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* mod.random (and math.random): as math.random, from the mission's seed,
 * which in co-op every headset shares (docs/mod-system.md, Multiplayer) */
static int l_random(lua_State *L)
{
    ModScript *m = modOf(L);
    const unsigned long long r = nextRandom(m);
    lua_Integer low, up;

    switch (lua_gettop(L)) {
        case 0:
            lua_pushnumber(L, (lua_Number)(r >> 11) * (1.0 / 9007199254740992.0));
            return 1;
        case 1:
            low = 1;
            up = luaL_checkinteger(L, 1);
            break;
        case 2:
            low = luaL_checkinteger(L, 1);
            up = luaL_checkinteger(L, 2);
            break;
        default:
            return luaL_error(L, "wrong number of arguments");
    }
    luaL_argcheck(L, low <= up, lua_gettop(L), "interval is empty");
    {
        const unsigned long long span = (unsigned long long)up - (unsigned long long)low;

        lua_pushinteger(L, (lua_Integer)((unsigned long long)low + (span == ~0ull ? r : r % (span + 1))));
    }
    return 1;
}

/* mod.save's file names: letters, digits, '_', '-', '.', not first a dot */
static int saveName(lua_State *L, int arg, char *path, size_t cap)
{
    ModScript *m = modOf(L);
    const char *name = luaL_checkstring(L, arg);
    size_t i, n = strlen(name);

    if (n == 0 || n > 64 || name[0] == '.' || !strcmp(name, "settings.txt")) {
        return luaL_error(L, "mod.save: \"%s\" isn't a name it can use", name);
    }
    for (i = 0; i < n; i++) {
        const char c = name[i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
            return luaL_error(L, "mod.save: \"%s\" isn't a name it can use", name);
        }
    }
    if (joinPath(path, cap, m->data, name) != 0) {
        return luaL_error(L, "mod.save: the path is too long");
    }
    return 0;
}

static int l_save_read(lua_State *L)
{
    char path[PATH_CAP];
    size_t len;
    char *text;

    saveName(L, 1, path, sizeof(path));
    text = readFile(path, GEVR_MODSCRIPT_DATA_CAP, &len);
    if (!text) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, text, len);
    free(text);
    return 1;
}

/* What the mod's data folder holds, but skip (the file about to be replaced) */
static unsigned long long dataBytes(const ModScript *m, const char *skip)
{
    DIR *d = opendir(m->data);
    struct dirent *e;
    unsigned long long total = 0;
    char path[PATH_CAP];
    struct stat st;

    if (!d) return 0;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || (skip && !strcmp(e->d_name, skip))) continue;
        if (joinPath(path, sizeof(path), m->data, e->d_name) == 0 && stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            total += (unsigned long long)st.st_size;
        }
    }
    closedir(d);
    return total;
}

static int l_save_write(lua_State *L)
{
    ModScript *m = modOf(L);
    char path[PATH_CAP], temp[PATH_CAP];
    size_t len;
    const char *text;
    FILE *f;
    int ok;

    saveName(L, 1, path, sizeof(path));
    text = luaL_checklstring(L, 2, &len);
    if (dataBytes(m, luaL_checkstring(L, 1)) + len > GEVR_MODSCRIPT_DATA_CAP) {
        lua_pushnil(L);
        lua_pushfstring(L, "mod.save keeps at most %d KB a mod", (int)(GEVR_MODSCRIPT_DATA_CAP >> 10));
        return 2;
    }
    if (makeDirs(m->data) != 0 || snprintf(temp, sizeof(temp), "%s.part", path) >= (int)sizeof(temp)) {
        lua_pushnil(L);
        lua_pushstring(L, "mod.save: couldn't make its folder");
        return 2;
    }
    /* whole or not at all: a crash leaves the old file */
    f = fopen(temp, "wb");
    ok = f && fwrite(text, 1, len, f) == len;
    ok = f && fclose(f) == 0 && ok;
    if (ok) {
        remove(path);
        ok = rename(temp, path) == 0;
    }
    if (!ok) {
        remove(temp);
        lua_pushnil(L);
        lua_pushstring(L, "mod.save: couldn't write it");
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int l_save_remove(lua_State *L)
{
    char path[PATH_CAP];

    saveName(L, 1, path, sizeof(path));
    lua_pushboolean(L, remove(path) == 0);
    return 1;
}

/* require "a.b": scripts/a/b.lua, once; a module's name is letters, digits, '_' and dots */
static int l_require(lua_State *L)
{
    ModScript *m = modOf(L);
    const char *name = luaL_checkstring(L, 1);
    char rel[96], path[PATH_CAP], chunk[112];
    size_t i, n = strlen(name), len;
    char *text;

    if (n == 0 || n > 64 || name[0] == '.' || name[n - 1] == '.' || strstr(name, "..")) {
        return luaL_error(L, "require: \"%s\" isn't a module name", name);
    }
    for (i = 0; i < n; i++) {
        const char c = name[i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) {
            return luaL_error(L, "require: \"%s\" isn't a module name", name);
        }
        rel[i] = c == '.' ? '/' : c;
    }
    rel[n] = '\0';
    lua_getfield(L, LUA_REGISTRYINDEX, "gevr.loaded");
    if (lua_getfield(L, -1, name) != LUA_TNIL) {
        return 1;
    }
    lua_pop(L, 1);
    snprintf(chunk, sizeof(chunk), "@scripts/%s.lua", rel);
    if (joinPath(path, sizeof(path), m->dir, chunk + 1) != 0
        || (text = readFile(path, GEVR_MODSCRIPT_SCRIPT_CAP, &len)) == NULL) {
        return luaL_error(L, "require: scripts/%s.lua isn't in the mod (or is over 1 MB)", rel);
    }
    if (luaL_loadbufferx(L, text, len, chunk, "t") != LUA_OK) {
        free(text);
        return lua_error(L);
    }
    free(text);
    lua_call(L, 0, 1);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, name);
    return 1;
}

/* load: text only (a binary chunk could step outside the sandbox) */
static int l_load(lua_State *L)
{
    size_t len;
    const char *text = luaL_checklstring(L, 1, &len);
    const char *name = luaL_optstring(L, 2, "=(load)");
    const char *mode = luaL_optstring(L, 3, "t");

    if (strchr(mode, 'b')) {
        lua_pushnil(L);
        lua_pushstring(L, "load: binary chunks aren't allowed");
        return 2;
    }
    if (luaL_loadbufferx(L, text, len, name, "t") != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    if (!lua_isnone(L, 4)) {
        lua_pushvalue(L, 4);
        if (!lua_setupvalue(L, -2, 1)) lua_pop(L, 1);
    }
    return 1;
}

static const luaL_Reg s_modFuncs[] = {
    { "on", l_on },
    { "log", l_log },
    { "setting", l_setting },
    { "capabilities", l_capabilities },
    { "random", l_random },
    { NULL, NULL },
};

static const luaL_Reg s_saveFuncs[] = {
    { "read", l_save_read },
    { "write", l_save_write },
    { "remove", l_save_remove },
    { NULL, NULL },
};

/* The sandbox and the API, protected (an allocation failure raises, not panics) */
static int openSandbox(lua_State *L)
{
    static const luaL_Reg libs[] = {
        { LUA_GNAME, luaopen_base }, { LUA_COLIBNAME, luaopen_coroutine }, { LUA_TABLIBNAME, luaopen_table },
        { LUA_STRLIBNAME, luaopen_string }, { LUA_MATHLIBNAME, luaopen_math }, { LUA_UTF8LIBNAME, luaopen_utf8 },
    };
    static const char *const gone[] = { "dofile", "loadfile", "collectgarbage" };
    ModScript *m = modOf(L);
    size_t i;

    for (i = 0; i < sizeof(libs) / sizeof(libs[0]); i++) {
        luaL_requiref(L, libs[i].name, libs[i].func, 1);
        lua_pop(L, 1);
    }
    for (i = 0; i < sizeof(gone) / sizeof(gone[0]); i++) {
        lua_pushnil(L);
        lua_setglobal(L, gone[i]);
    }
    lua_getglobal(L, LUA_STRLIBNAME);
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");
    lua_pop(L, 1);
    lua_getglobal(L, LUA_MATHLIBNAME);
    lua_pushcfunction(L, l_random);
    lua_setfield(L, -2, "random");
    lua_pushnil(L);
    lua_setfield(L, -2, "randomseed");
    lua_pop(L, 1);
    lua_register(L, "load", l_load);
    lua_register(L, "print", l_log);
    lua_register(L, "require", l_require);
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "gevr.on");
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "gevr.loaded");

    lua_newtable(L);
    luaL_setfuncs(L, s_modFuncs, 0);
    gevrModApiOpen(L);
    lua_newtable(L);
    luaL_setfuncs(L, s_saveFuncs, 0);
    lua_setfield(L, -2, "save");
    lua_pushstring(L, m->id);
    lua_setfield(L, -2, "id");
    lua_pushstring(L, m->title);
    lua_setfield(L, -2, "title");
    lua_pushstring(L, m->version);
    lua_setfield(L, -2, "version");
    lua_pushinteger(L, GEVR_MOD_API_MAX);
    lua_setfield(L, -2, "api");
    lua_setglobal(L, "mod");
    return 0;
}

/* ---- events ---- */

typedef GevrModArg Arg;

static unsigned long long hashId(const char *id)
{
    unsigned long long h = 1469598103934665603ull;

    while (*id) h = (h ^ (unsigned char)*id++) * 1099511628211ull;
    return h;
}

/* Each running mod's handlers for event; load: a loading event, its own
 * runaway limit and not in the budget. cancellable: returns 1 when a handler
 * returned false. */
static int fire(const char *event, const Arg *args, int nargs, int load, int cancellable)
{
    int k, cancel = 0;

    for (k = 0; k < s_count; k++) {
        ModScript *m = &s_mods[k];
        lua_State *L = m->L;
        const u64 start = sysGetMicroseconds();
        lua_Integer i, n;

        if (m->broken || m->off) continue;
        lua_getfield(L, LUA_REGISTRYINDEX, "gevr.on");
        if (lua_getfield(L, -1, event) != LUA_TTABLE) {
            lua_pop(L, 2);
            continue;
        }
        n = (lua_Integer)lua_rawlen(L, -1);
        for (i = 1; i <= n && !m->broken && !m->off; i++) {
            int a;

            lua_rawgeti(L, -1, i);
            for (a = 0; a < nargs; a++) {
                if (args[a].kind == GEVR_MODARG_NUMBER) lua_pushnumber(L, args[a].n);
                else if (args[a].kind == GEVR_MODARG_INTEGER) lua_pushinteger(L, (lua_Integer)args[a].n);
                else if (args[a].kind == GEVR_MODARG_BOOL) lua_pushboolean(L, args[a].n != 0);
                else if (args[a].kind == GEVR_MODARG_NIL) lua_pushnil(L);
                else lua_pushstring(L, args[a].s ? args[a].s : "");
            }
            if (modCall(m, nargs, cancellable ? 1 : 0, load, event) == 0 && cancellable) {
                if (lua_type(L, -1) == LUA_TBOOLEAN && !lua_toboolean(L, -1)) cancel = 1;
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 2);
        if (!load) {
            const u64 spent = sysGetMicroseconds() - start;

            m->windowUs += spent;
            s_windowUs += spent;
        }
    }
    return cancel;
}

int gevrModScriptEvent(const char *event, const GevrModArg *args, int nargs, int cancellable)
{
    if (!s_count || !s_inMission || !s_playing) return 0;
    return fire(event, args, nargs, 0, cancellable);
}

/* One handler asked, inside its protected call: the arguments pushed, the
 * handler run, its answer read (an answer refused is that mod's error) */
typedef struct Ask {
    int (*push)(lua_State *L, void *ud);
    int (*answer)(lua_State *L, void *ud);
    void *ud;
} Ask;

static int askOne(lua_State *L)
{
    const Ask *a = (const Ask *)lua_touserdata(L, 1);
    int nargs;

    lua_remove(L, 1);   /* the handler alone */
    nargs = a->push(L, a->ud);
    lua_call(L, nargs, 1);
    lua_pushboolean(L, a->answer(L, a->ud) != 0);
    return 1;
}

int gevrModScriptAsk(const char *event, int (*push)(lua_State *L, void *ud), int (*answer)(lua_State *L, void *ud),
                     void *ud, int play)
{
    Ask a;
    int k, ran = 0, stop = 0;

    if (!s_count || !s_inMission || (play && !s_playing)) return 0;
    a.push = push;
    a.answer = answer;
    a.ud = ud;
    for (k = 0; k < s_count && !stop; k++) {
        ModScript *m = &s_mods[k];
        lua_State *L = m->L;
        const u64 start = sysGetMicroseconds();
        lua_Integer i, n;

        if (m->broken || m->off) continue;
        lua_getfield(L, LUA_REGISTRYINDEX, "gevr.on");
        if (lua_getfield(L, -1, event) != LUA_TTABLE) {
            lua_pop(L, 2);
            continue;
        }
        n = (lua_Integer)lua_rawlen(L, -1);
        for (i = 1; i <= n && !stop && !m->broken && !m->off; i++) {
            lua_pushcfunction(L, askOne);
            lua_pushlightuserdata(L, &a);
            lua_rawgeti(L, -3, i);
            if (modCall(m, 2, 1, !play, event) == 0) {
                stop = lua_toboolean(L, -1);
                lua_pop(L, 1);
                ran++;
            }
        }
        lua_pop(L, 2);
        if (play) {
            const u64 spent = sysGetMicroseconds() - start;

            m->windowUs += spent;
            s_windowUs += spent;
        }
    }
    return ran;
}

void gevrModScriptStart(void)
{
    const gevrModInfo *info = gevrModActive();
    char path[PATH_CAP];
    struct stat st;
    ModScript *m;

    if (!info || s_count >= MAX_MODS) return;
    m = &s_mods[s_count];
    memset(m, 0, sizeof(*m));
    snprintf(m->id, sizeof(m->id), "%s", info->m.id);
    snprintf(m->title, sizeof(m->title), "%s", info->m.title);
    snprintf(m->version, sizeof(m->version), "%s", info->m.version);
    if (joinPath(m->dir, sizeof(m->dir), gevrModsRoot(), m->id) != 0
        || snprintf(m->data, sizeof(m->data), "%s/%s", fsFullPath("$S/mod-data"), m->id) >= (int)sizeof(m->data)) {
        return;
    }
    /* a mod of data alone runs nothing */
    if (joinPath(path, sizeof(path), m->dir, "scripts/main.lua") != 0 || stat(path, &st) != 0) {
        sysLogPrintf(LOG_NOTE, "mods: %s has no scripts/main.lua; nothing to run", m->id);
        return;
    }
    m->L = lua_newstate(modAlloc, m);
    if (!m->L) {
        sysLogPrintf(LOG_ERROR, "mods: %s: no memory for its scripts", m->id);
        return;
    }
    *(ModScript **)lua_getextraspace(m->L) = m;
    lua_sethook(m->L, runawayHook, LUA_MASKCOUNT, 10000);
    m->rng = hashId(m->id) ^ (unsigned long long)sysGetMicroseconds();
    s_count++;
    sysLogPrintf(LOG_NOTE, "mods: starting %s's scripts (Lua %s.%s)", m->id, LUA_VERSION_MAJOR, LUA_VERSION_MINOR);
    lua_pushcfunction(m->L, openSandbox);
    if (modCall(m, 0, 0, 1, "the sandbox") != 0) return;
    lua_pushcfunction(m->L, l_require);
    lua_pushstring(m->L, "main");
    if (modCall(m, 1, 0, 1, "scripts/main.lua") != 0) return;
    fire("campaign_enter", NULL, 0, 1, 0);
}

void gevrModScriptStageLoad(int stage, int mission, int done)
{
    int k;

    if (!s_count) return;
    if (!done) {
        if (s_inMission) {
            Arg end[2];

            end[0].kind = 3;
            end[0].n = s_missionStage;
            end[1].kind = 2;
            end[1].s = s_missionComplete ? "complete" : "left";
            s_playing = 0;
            fire("mission_end", end, 2, 1, 0);
            s_inMission = 0;
            gevrModApiReset();   /* weapon stats and the like: the game's own again */
        }
        if (!mission) return;
        s_inMission = 1;
        s_playing = 0;
        s_missionStage = stage;
        s_missionComplete = 0;
        s_windowStart = 0;
        s_windowUs = 0;
        s_windowFrames = 0;
        s_overSeconds = 0;
        for (k = 0; k < s_count; k++) {
            ModScript *m = &s_mods[k];
            const unsigned long long seed = netIsActive() ? (unsigned long long)gevrCoopIntroSeed()
                                                          : (unsigned long long)sysGetMicroseconds();

            /* a new mission: one turned off by the last runs again */
            m->off = 0;
            m->paused = 0;
            m->windowUs = 0;
            m->rng = seed * 0x9E3779B97F4A7C15ull ^ hashId(m->id) ^ (unsigned long long)stage;
        }
        {
            Arg a;

            a.kind = 3;
            a.n = stage;
            gevrModApiReset();
            fire("mission_prepare", &a, 1, 1, 0);
        }
    } else if (mission && s_inMission) {
        Arg a;

        a.kind = 3;
        a.n = stage;
        fire("mission_start", &a, 1, 1, 0);
        s_playing = 1;
    }
}

void gevrModScriptMissionComplete(void)
{
    s_missionComplete = 1;
}

/* A second of play: the budget, and the perf log's line */
static void closeWindow(void)
{
    const double avg = s_windowFrames ? (double)s_windowUs / s_windowFrames : 0.0;
    int k;

    if (s_perfLog < 0) s_perfLog = sysArgCheck("--perf-log") ? 1 : 0;
    for (k = 0; s_perfLog && k < s_count; k++) {
        const ModScript *m = &s_mods[k];

        sysLogPrintf(LOG_NOTE, "perf: mod %s %.3f ms a frame (all mods %.3f of %.2f ms)%s, %.1f MB", m->id,
                     s_windowFrames ? m->windowUs / 1000.0 / s_windowFrames : 0.0, avg / 1000.0,
                     GEVR_MODSCRIPT_BUDGET_US / 1000.0, m->broken ? " broken" : m->paused ? " paused" : m->off ? " off" : "",
                     m->mem / 1048576.0);
    }
    s_overSeconds = avg > GEVR_MODSCRIPT_BUDGET_US ? s_overSeconds + 1 : 0;
    if (s_overSeconds >= GEVR_MODSCRIPT_OVER_SECONDS) {
        ModScript *costliest = NULL;

        for (k = 0; k < s_count; k++) {
            ModScript *m = &s_mods[k];

            if (!m->broken && !m->off && (!costliest || m->windowUs > costliest->windowUs)) costliest = m;
        }
        if (costliest) {
            costliest->off = 1;
            costliest->paused = 1;
            gevrModApiReset();
            sysLogPrintf(LOG_WARNING, "mods: %s paused for this mission: %.3f ms a frame for %d seconds (budget %.2f)",
                         costliest->id, avg / 1000.0, s_overSeconds, GEVR_MODSCRIPT_BUDGET_US / 1000.0);
            note("%s paused for this mission: its scripts took too long.", costliest->title);
        }
        s_overSeconds = 0;
    }
    s_windowUs = 0;
    s_windowFrames = 0;
    for (k = 0; k < s_count; k++) s_mods[k].windowUs = 0;
}

void gevrModScriptFrame(int ticks)
{
    Arg a;
    u64 now;

    if (!s_count || !s_inMission || !s_playing) return;
    a.kind = 1;
    a.n = ticks > 0 ? ticks / 60.0 : 0.0;
    if (a.n > GEVR_MODSCRIPT_TICK_CAP) a.n = GEVR_MODSCRIPT_TICK_CAP;
    fire("tick", &a, 1, 0, 0);
    s_windowFrames++;
    now = sysGetMicroseconds();
    if (!s_windowStart) {
        s_windowStart = now;
    } else if (now - s_windowStart >= 1000000) {
        closeWindow();
        s_windowStart = now;
    }
}

const char *gevrModScriptTakeNote(void)
{
    if (!s_hasNote) return NULL;
    s_hasNote = 0;
    return s_note;
}

int gevrModScriptRunning(void)
{
    int k;

    for (k = 0; k < s_count; k++) {
        if (!s_mods[k].broken) return 1;
    }
    return 0;
}

void gevrModScriptShutdown(void)
{
    int k;

    for (k = 0; k < s_count; k++) {
        if (s_mods[k].L) lua_close(s_mods[k].L);
        memset(&s_mods[k], 0, sizeof(s_mods[k]));
    }
    s_count = 0;
    s_inMission = s_playing = s_missionComplete = 0;
    s_hasNote = 0;
    s_windowStart = s_windowUs = 0;
    s_windowFrames = s_overSeconds = 0;
}
