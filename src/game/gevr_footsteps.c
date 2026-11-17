/*
 * Carnivorous' Footsteps patch, as GoldenEye with Mario Characters V3.17
 * carries it: its scripts poke the cartridge's memory (docs/mario-characters.md).
 *
 * A level's AI list says "2B FE" (GoldenEye's dead command 0x2B, TRYFindCover
 * here) and the PRINT after it spells what to do. The cartridge's code that
 * reads it (0x7005C250, called from the 0x2B case at 7F0360A0; Tomorrow Never
 * Dies 64 carries it too, DAB geconvert.c tndHookConvert) is ported here
 * whole, the strings read as it reads them:
 *
 *   2B 00        music volume back to what level start saved
 *   2B nn        the current stage's X track is nn (music_setup_entries)
 *   2B FE "AAAAAAAA[+Poooooooo...]+Xvvvvvvvv[...]"
 *                an address, pointer steps (*addr + offset), then C stores,
 *                A/S add/subtract (F after it: floats; T: times the tick),
 *                E/H/L go to label "+Rxx" when the word is equal, higher,
 *                lower (any other letter as L, as its code has it)
 *   2B FD "SSSSSSSS[+P...]++DDDDDDDD[+P...]+C[nnnnnnnn]"
 *                copies n bytes (4 with none) from S to D;
 *                "+A"/"+S" (F after it: floats) puts S +/- D in D
 *   2B FC "ii"   takes item ii from Bond (ff: every item from 32 to 2)
 *   2B FB        kills Bond (bondviewKillCurrentPlayer) while he is alive
 *
 * Its scripts name N64 addresses. Those in the cartridge's data segment and
 * Expansion Pak pieces are the binder's copy of them (gevrRomWindowBytes):
 * strings the port's tables point at change in place, and the tables it
 * copied out are read again (gevrRomWindowChanged). The rest are the port's
 * own variables, by what the N64 code keeps there (below).
 */

#include <ultra64.h>
#include <string.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "bondview.h"
#include "bg.h"
#include "bondinv.h"
#include "cheat.h"
#include "lv.h"
#include "objective_status.h"
#include "player.h"
#include "system.h"
#include "gevr_romlayout.h"
#include "gevr_footsteps.h"

extern s32 selected_stage;                 /* front.c, 0x8002A8F4 */
extern u16 mTrack2Vol;                     /* options.c, 0x80040B58 */
extern u8 g_CheatActivated[CHEAT_MAX];     /* front.c, 0x800696A0: Cheat Options' on and off */
extern struct levelentry levelinfotable[];   /* bg.c, 0x8004448C */
void bondviewKillCurrentPlayer(void);

/* g_CurrentPlayer's value as the scripts see it: they add its N64 field offsets */
#define VM_PLAYER 0x9f000000u
#define VM_PLAYER_SPAN 0x3000u

/*
 * Mario Characters keeps its own flag in the high half of each objective's
 * status word (0x80075D58 + 4 * i, the status in the low half: its code reads
 * the status with lh +2, 7F0075BC). AI 0x36 flips it; the briefing and the
 * watch show an objective by it (gevrFootstepsObjectiveDifficulty).
 */
static s16 g_ObjectiveFlag[OBJECTIVES_MAX];

/* The fields of struct player its scripts name, by N64 offset (bondview.h) */
static const struct {
    u32 off;
    u8 size;
    u8 isfloat;
    size_t host;
} g_PlayerFields[] = {
    { 0x0d8, 4, 0, offsetof(struct player, bonddead) },
    { 0x0dc, 4, 1, offsetof(struct player, bondhealth) },
    { 0x0e0, 4, 1, offsetof(struct player, bondarmour) },
    { 0x0e4, 4, 1, offsetof(struct player, oldhealth) },
    { 0x0e8, 4, 1, offsetof(struct player, oldarmour) },
    { 0x0ec, 4, 1, offsetof(struct player, apparenthealth) },
    { 0x0f0, 4, 1, offsetof(struct player, apparentarmour) },
    { 0x0f8, 4, 0, offsetof(struct player, healthshowtime) },
    { 0x1c8, 4, 0, offsetof(struct player, watch_animation_state) },
    { 0x41c, 4, 0, offsetof(struct player, bondtype) },
};

static int g_Unmapped;

static void noteUnmapped(u32 a, const char *what)
{
    if (g_Unmapped < 24) {
        g_Unmapped++;
        sysLogPrintf(LOG_WARNING, "footsteps: %s %08x is no memory the port has; ignored", what, a);
    }
}

/* One byte of the cartridge's memory. Returns 0 where the port has nothing there. */
static int vmByte(u32 a, u8 *out, int write, u8 value)
{
    u8 *w;
    u32 i;

    /* g_CurrentPlayer (0x8007A0B0) and its fields */
    if (a >= 0x8007a0b0u && a < 0x8007a0b4u) {
        if (!write) {
            *out = (u8)(VM_PLAYER >> (8 * (3 - (a - 0x8007a0b0u))));
        }
        return 1;
    }
    if (a >= VM_PLAYER && a < VM_PLAYER + VM_PLAYER_SPAN) {
        const u32 off = a - VM_PLAYER;

        for (i = 0; i < sizeof(g_PlayerFields) / sizeof(g_PlayerFields[0]); i++) {
            if (off >= g_PlayerFields[i].off && off < g_PlayerFields[i].off + g_PlayerFields[i].size && g_CurrentPlayer) {
                u8 *field = (u8 *)g_CurrentPlayer + g_PlayerFields[i].host;
                u32 v;
                const u32 shift = 8 * (3 - (off - g_PlayerFields[i].off));

                memcpy(&v, field, 4);
                if (!write) {
                    *out = (u8)(v >> shift);
                } else {
                    v = (v & ~(0xffu << shift)) | ((u32)value << shift);
                    memcpy(field, &v, 4);
                }
                return 1;
            }
        }
        noteUnmapped(0x80000000u | off, "player field");
        return 0;
    }
    /* Cheat Options' on and off, by cheat (0x800696A0, front.c): Play as Luigi is 8 */
    if (a >= 0x800696a0u && a < 0x800696a0u + CHEAT_MAX) {
        if (write) {
            g_CheatActivated[a - 0x800696a0u] = value;
        } else {
            *out = g_CheatActivated[a - 0x800696a0u];
        }
        return 1;
    }
    /* the cheats each player has on (0x80079E30, cheat.c) */
    if (a >= 0x80079e30u && a < 0x80079e30u + sizeof(g_CheatPlayerTextRelated)) {
        if (write) {
            g_CheatPlayerTextRelated[a - 0x80079e30u] = value;
        } else {
            *out = g_CheatPlayerTextRelated[a - 0x80079e30u];
        }
        return 1;
    }
    /* each objective's word: its own flag high, its status low */
    if (a >= 0x80075d58u && a < 0x80075d58u + 4u * OBJECTIVES_MAX) {
        const u32 k = (a - 0x80075d58u) / 4, b = (a - 0x80075d58u) % 4;
        u16 hi = (u16)g_ObjectiveFlag[k], lo = (u16)objectiveStatuses[k];

        if (!write) {
            *out = (u8)(b == 0 ? hi >> 8 : b == 1 ? hi : b == 2 ? lo >> 8 : lo);
        } else if (b < 2) {
            hi = b == 0 ? (u16)((hi & 0xff) | (value << 8)) : (u16)((hi & 0xff00) | value);
            g_ObjectiveFlag[k] = (s16)hi;
        } else {
            lo = b == 2 ? (u16)((lo & 0xff) | (value << 8)) : (u16)((lo & 0xff00) | value);
            objectiveStatuses[k] = (s16)lo;
        }
        return 1;
    }
    /* the music volume (options.c mTrack2Vol): written raw, as its code does */
    if (a >= 0x80040b58u && a < 0x80040b5au) {
        const u32 shift = 8 * (1 - (a - 0x80040b58u));

        if (!write) {
            *out = (u8)(mTrack2Vol >> shift);
        } else {
            mTrack2Vol = (u16)((mTrack2Vol & ~(0xffu << shift)) | ((u32)value << shift));
        }
        return 1;
    }
    /* the cartridge's data segment, Expansion Pak pieces and the RAM between */
    w = gevrRomWindowBytes(a, 1);
    if (w) {
        if (write) {
            *w = value;
        } else {
            *out = *w;
        }
        return 1;
    }
    noteUnmapped(a, write ? "write to" : "read of");
    return 0;
}

static u8 vmRead8(u32 a)
{
    u8 v = 0;

    vmByte(a, &v, 0, 0);
    return v;
}

static u32 vmRead32(u32 a)
{
    return ((u32)vmRead8(a) << 24) | ((u32)vmRead8(a + 1) << 16) | ((u32)vmRead8(a + 2) << 8) | vmRead8(a + 3);
}

static void vmWrite(u32 a, const u8 *bytes, u32 n)
{
    u32 i;

    for (i = 0; i < n; i++) {
        vmByte(a + i, NULL, 1, bytes[i]);
    }
    gevrRomWindowChanged(a, n);
}

static void vmWrite32(u32 a, u32 v)
{
    const u8 b[4] = { (u8)(v >> 24), (u8)(v >> 16), (u8)(v >> 8), (u8)v };

    vmWrite(a, b, 4);
}

static f32 asFloat(u32 v)
{
    f32 f;

    memcpy(&f, &v, 4);
    return f;
}

static u32 asBits(f32 f)
{
    u32 v;

    memcpy(&v, &f, 4);
    return v;
}

/* Eight characters as hex, as 0x8005C338 reads them: below '@' less '0', else less '7' */
static u32 hex8(const u8 *s)
{
    u32 v = 0;
    s32 i;

    for (i = 0; i < 8; i++) {
        const u8 c = s[i];

        v = (v << 4) + (u8)(c < 0x40 ? c - 0x30 : c - 0x37);
    }
    return v;
}

/* "+Rxx" two characters on: the label to go to */
static s32 labelAt(const u8 *s)
{
    const u8 text[8] = { '0', '0', '0', '0', '0', '0', s[0], s[1] };

    return (s32)(hex8(text) & 0xff);
}

/* 2B FE */
static s32 poke(const u8 *print)
{
    const u8 *t = print;
    u32 addr = hex8(t + 1);
    u32 v, a0;
    u8 op;
    s32 take;

    for (;;) {
        t += 10;
        op = t[0];
        v = hex8(t + 1);
        if (op != 'P') {
            break;
        }
        addr = vmRead32(addr) + v;
    }
    if (op == 'C') {
        vmWrite32(addr, v);
        return -1;
    }
    a0 = vmRead32(addr);
    if (op == 'A' || op == 'S') {
        if (t[9] == 'F') {
            f32 f = asFloat(v);
            f32 cur = asFloat(a0);

            if (t[10] == 'T') {
                f *= g_GlobalTimerDelta;
            }
            vmWrite32(addr, asBits(op == 'A' ? cur + f : cur - f));
        } else {
            if (t[9] == 'T') {
                v *= (u32)g_ClockTimer;
            }
            vmWrite32(addr, op == 'A' ? a0 + v : a0 - v);
        }
        return -1;
    }
    if (op == 'E') {
        take = a0 == v;
    } else if (op == 'H') {
        take = (s32)v < (s32)a0;
    } else {
        take = (s32)a0 < (s32)v;
    }
    return take ? labelAt(t + 11) : -1;
}

/* 2B FD */
static void poke2(const u8 *print)
{
    const u8 *t = print;
    u32 src = 0, dst = 0;
    s32 operand;
    u8 op = 0;

    for (operand = 0; operand < 2; operand++) {
        u32 addr = hex8(t + 1);

        for (;;) {
            t += 10;
            op = t[0];
            if (op != 'P') {
                break;
            }
            addr = vmRead32(addr) + hex8(t + 1);
        }
        if (operand == 0) {
            src = addr;
        } else {
            dst = addr;
        }
    }
    if (op == 'C') {
        u32 n = t[1] ? hex8(t + 1) : 4;
        u32 i;

        if (n > 0x10000) {
            noteUnmapped(n, "copy length");
            return;
        }
        for (i = 0; i < n; i++) {
            const u8 b = vmRead8(src + i);

            vmByte(dst + i, NULL, 1, b);
        }
        gevrRomWindowChanged(dst, n);
    } else {
        const u32 s = vmRead32(src), d = vmRead32(dst);

        if (t[1]) {
            vmWrite32(dst, asBits(op == 'A' ? asFloat(s) + asFloat(d) : asFloat(s) - asFloat(d)));
        } else {
            vmWrite32(dst, op == 'A' ? s + d : s - d);
        }
    }
}

/* the stage's music row in the cartridge's table (0x8004EB10, eight bytes a row) */
static void setStageXTrack(s32 track)
{
    u32 row;

    for (row = 0; row < 64; row++) {
        const u32 at = 0x8004eb10u + 8u * row;
        const s16 stage = (s16)((vmRead8(at) << 8) | vmRead8(at + 1));

        if (stage == 0) {
            return;
        }
        if (stage == selected_stage) {
            const u8 b[2] = { (u8)((u32)track >> 8), (u8)track };

            vmWrite(at + 6, b, 2);
            return;
        }
    }
}

/* the first commands of each stage, in the log */
static s32 g_Logged;

s32 gevrFootstepsCommand(const u8 *cmd)
{
    const s8 arg = (s8)cmd[1];
    const u8 *print = cmd[2] == 0xad ? cmd + 2 : NULL;

    if (g_Logged < 48) {
        g_Logged++;
        sysLogPrintf(LOG_NOTE, "footsteps: 2B %02X %.40s", cmd[1], print ? (const char *)print + 1 : "");
    }

    if (arg == 0) {
        /* 2B 00: the music volume level start saved (0x80700110) */
        const u8 b[2] = { vmRead8(0x80700110u), vmRead8(0x80700111u) };

        vmWrite(0x80040b58u, b, 2);
        return -1;
    }
    if (arg > 0) {
        setStageXTrack(arg);
        return -1;
    }
    switch (arg) {
    case -2:
        return print ? poke(print) : -1;
    case -3:
        if (print) {
            poke2(print);
        }
        return -1;
    case -4:
        if (print) {
            const u8 text[8] = { '0', '0', '0', '0', '0', '0', print[1], print[2] };
            const u32 item = hex8(text);

            if (item == 0xff) {
                s32 i;

                for (i = 0x20; i >= 2; i--) {
                    bondinvRemoveItemByID(i);
                }
            } else {
                bondinvRemoveItemByID(item);
            }
        }
        return -1;
    case -5:
        if (g_CurrentPlayer && !g_CurrentPlayer->bonddead) {
            bondviewKillCurrentPlayer();
        }
        return -1;
    default:
        /* closes the watch (player 0x1C8, 0x220); no Mario Characters list says it */
        noteUnmapped((u32)(u8)arg, "hook");
        return -1;
    }
}

/*
 * Level start (Mario Characters' code after init_watch_at_start_of_stage,
 * 0x7005C1E0): the music volume kept at 0x80700110 for 2B 00, the watch's
 * zoomed field of view (60) at 0x80700114, its unused switches at
 * 0x807000A0-0x807000BC cleared, the music silenced (unapplied) in a stage
 * whose level row's last float is 0, and the objectives' flags cleared.
 */
void gevrFootstepsStageStart(s32 stage)
{
    static const u8 zero[0x20];
    const u8 fov[4] = { 0x42, 0x70, 0x00, 0x00 };
    const u8 vol[2] = { (u8)(mTrack2Vol >> 8), (u8)mTrack2Vol };
    s32 i;

    if (!gevrRomFootsteps()) {
        return;
    }
    vmWrite(0x80700110u, vol, 2);
    vmWrite(0x80700114u, fov, 4);
    vmWrite(0x807000a0u, zero, sizeof(zero));
    for (i = 0; levelinfotable[i].levelID != LEVELID_MAX && i < 64; i++) {
        if (levelinfotable[i].levelID == stage) {
            if (levelinfotable[i].unknownfloat == 0.0f) {
                mTrack2Vol = 0;
            }
            break;
        }
    }
    memset(g_ObjectiveFlag, 0, sizeof(g_ObjectiveFlag));
    g_Logged = 0;
}

/*
 * AI 0x36 (GoldenEye's unused IFAlarmIsOn): flips objective i between its
 * difficulty and four more (bit 2 of its record's last difficulty byte, 0xF),
 * and its flag. 7F0362D8 and 0x8000DC30.
 */
void gevrFootstepsToggleObjective(s32 i)
{
    if (i < 0 || i >= OBJECTIVES_MAX) {
        return;
    }
    if (objective_ptrs[i]) {
        objective_ptrs[i]->difficulty ^= 4;
    }
    g_ObjectiveFlag[i] ^= 1;
}

/*
 * The briefing (7F016390) and the watch's objectives (7F015A84) ask Mario
 * Characters' code (0x80004E98) which difficulty an objective shows from:
 * one at 4 is hidden until its flag is set, any other is hidden once it is.
 */
s32 gevrFootstepsObjectiveDifficulty(s32 i, s32 difficulty)
{
    s32 want = 0;

    if (!gevrRomFootsteps() || i < 0 || i >= OBJECTIVES_MAX) {
        return difficulty;
    }
    if (difficulty == 4) {
        want = 1;
        difficulty = 0;
    }
    return g_ObjectiveFlag[i] != want ? 4 : difficulty;
}

/* ---- Mario Characters' cheats ------------------------------------------- */

extern CheatInfo g_CheatInfo[];
extern const s32 gevrCheatInfoCount;     /* cheat.c */

/*
 * Its cheat table rows (0x8003F80C, sixteen bytes a cheat): Play as Luigi and
 * Peach on cheats 8 and 9 (2x Health and Armour, now button codes on 1 and
 * 5), Mushroom Mode on 13, Extra Weapons on 16; their names and masks are its
 * own. The button codes stay the port's (they are its, unchanged).
 */
void gevrFootstepsBindCheats(void)
{
    s32 k, done = 0;

    if (!gevrRomFootsteps()) {
        return;
    }
    for (k = 0; k < gevrCheatInfoCount; k++) {
        const u8 *row = gevrRomWindowBytes(0x8003f80cu + 16u * (u32)k, 16);

        if (!row || !g_CheatInfo[k].cheat_id || row[0] != g_CheatInfo[k].cheat_id) {
            continue;
        }
        g_CheatInfo[k].text_bank_id = (u16)((row[8] << 8) | row[9]);
        g_CheatInfo[k].unk_a = (u16)((row[10] << 8) | row[11]);
        g_CheatInfo[k].maskfield = (s32)(((u32)row[12] << 24) | ((u32)row[13] << 16) | ((u32)row[14] << 8) | row[15]);
        done++;
    }
    sysLogPrintf(LOG_NOTE, "footsteps: %d cheat rows from the cartridge", done);
}

/*
 * What a cheat does when it comes on (cheatButtonHandleCheatsTurnedOn's
 * jump table, 0x800572B0): the case of the cheat given, or -1 for none.
 */
s32 gevrFootstepsCheatEffect(s32 cheat)
{
    if (!gevrRomFootsteps()) {
        return cheat;
    }
    switch (cheat) {
    case CHEAT_EXTRA_MP_CHARS:              return CHEAT_2X_HEALTH;     /* Super 2x Health's button code */
    case CHEAT_DEBUG_UNK5:                  return CHEAT_2X_ARMOR;      /* Super 2x Armour's */
    case CHEAT_DEACTIVATE_INVINCIBILITY:    return CHEAT_10X_HEALTH;    /* Super 10x Health's */
    case CHEAT_2X_HEALTH:                   return -1;                  /* Play as Luigi: its scripts */
    case CHEAT_2X_ARMOR:                    return -1;                  /* Play as Peach: its scripts */
    case CHEAT_EXTRA_WEAPONS:               return CHEAT_DEACTIVATE_INVINCIBILITY;   /* Mushroom Mode */
    case CHEAT_10X_HEALTH:                  return CHEAT_EXTRA_WEAPONS;
    default:                                return cheat;
    }
}

/* The cheat whose unlock a cheat takes (its jump table at 0x800518A0); the cheat itself otherwise */
s32 gevrFootstepsCheatUnlockAs(s32 cheat)
{
    if (!gevrRomFootsteps()) {
        return cheat;
    }
    switch (cheat) {
    case CHEAT_LINEMODE:
    case CHEAT_2X_HEALTH:      return CHEAT_MAGNUM;
    case CHEAT_2X_ARMOR:       return CHEAT_LASER;
    case CHEAT_EXTRA_WEAPONS:
    case CHEAT_10X_HEALTH:     return CHEAT_GOLDEN_GUN;
    default:                   return cheat;
    }
}

/* Cheat Options' rows a column and the first row's height (7F017D24-7F018354): 14 and 0x28, GoldenEye's 12 and 0x35 */
s32 gevrFootstepsCheatRows(void)
{
    return gevrRomFootsteps() ? 14 : 12;
}

s32 gevrFootstepsCheatTop(void)
{
    return gevrRomFootsteps() ? 0x28 : 0x35;
}

/* ---- its reworked AI commands and hooks ---------------------------------- */

#include <music.h>
#include <snd.h>
#include "chrai.h"
#include "propobj.h"

/*
 * AI 0xFB (GasLeakAndFadeFog, 7F03A104): the fog's fade without the gas.
 * The fade restarts (toxic_gas_sound_timer 0), takes 120 ticks and does
 * no harm, as GoldenEye's Egypt has it, from wherever the gas last was.
 */
void gevrFootstepsFogFade(void)
{
    toxic_gas_sound_timer = 0.0f;
    activate_gas_sound_timer = TRUE;
    gasTimeToFullOpacity = 120.0f;
    gasDoesDamageFlag = FALSE;
}

/*
 * A sound heard from a place, as its code plays them (sndPlaySfx, then
 * chrobjSndCreatePostEventDefault): AI 0xC4 on channel -2 at the guard
 * (0x80004AE0), and the pin of a guard's grenade, sound 0x117 (0x80004A90).
 */
void gevrFootstepsSoundAt(s32 sound, coord3d *pos)
{
    ALSoundState *state = sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, (s16)sound, NULL);

    if (state && pos) {
        chrobjSndCreatePostEventDefault(state, pos);
    }
}

/*
 * AI 0xBF (TRYGiveMeItem) where GoldenEye gives the item as asked (its
 * 0x80004B04, Footsteps' fix for reinforcements): with Enemy Rockets on in
 * Cheat Options, items 4 to 24 become the rocket launcher.
 */
s32 gevrFootstepsEnemyRockets(void)
{
    return gevrRomFootsteps() && g_CheatActivated[CHEAT_ENEMY_ROCKETS];
}

/*
 * AI 0xE9 (SwitchSky) in Mario Characters swaps the stage's two
 * environments as it switches (0x8005C14C, called from fogSwitchToSolosky2
 * when AI 0xE9 asks): the switch is drawn as GoldenEye's, and the next 0xE9
 * goes back.
 */
#include "bgfog.h"
extern EnvironmentRecord *g_EnvironmentMainp, *g_EnvironmentAltp;

void gevrFootstepsSwapSkies(void)
{
    EnvironmentRecord *main = g_EnvironmentMainp;

    g_EnvironmentMainp = g_EnvironmentAltp;
    g_EnvironmentAltp = main;
}

/*
 * GoldenEye's global AI lists as the cartridge has them (its table at
 * 0x8003744C): the Footsteps patch edits 0x000D, PersistentlyChaseAndAttack-
 * Bond, to the cancelled XBLA release's (a chasing guard holds his fire
 * until Bond is within 15 m). A list whose bytes differ from the port's is
 * run from the cartridge's copy, its commands the same bytes as the port's.
 */
extern AIListRecord g_GlobalAILists[];

static void bindGlobalLists(void)
{
    s32 i, k;

    for (i = 0; g_GlobalAILists[i].ailist; i++) {
        for (k = 0; k < 64; k++) {
            const u8 *row = gevrRomWindowBytes(0x8003744cu + 8u * (u32)k, 8);
            u32 ptr, id, len, mine;
            u8 *list;

            if (!row) {
                break;
            }
            ptr = ((u32)row[0] << 24) | ((u32)row[1] << 16) | ((u32)row[2] << 8) | row[3];
            id = ((u32)row[4] << 24) | ((u32)row[5] << 16) | ((u32)row[6] << 8) | row[7];
            if (!ptr) {
                break;
            }
            if ((s32)id != g_GlobalAILists[i].ID) {
                continue;
            }
            list = gevrRomWindowBytes(ptr, 1);
            len = list && gevrRomWindowBytes(ptr, 2048) ? gevrRomAiListLength(list, 2048) : 0;
            mine = gevrRomAiListLength((const u8 *)g_GlobalAILists[i].ailist, 4096);
            if (len && (len != mine || memcmp(list, g_GlobalAILists[i].ailist, len) != 0)) {
                g_GlobalAILists[i].ailist = (AIRecord *)list;
                sysLogPrintf(LOG_NOTE, "footsteps: global AI list %04x is the cartridge's (%u bytes)", id, len);
            }
            break;
        }
    }
}

void gevrFootstepsBind(void)
{
    if (!gevrRomFootsteps()) {
        return;
    }
    gevrFootstepsBindCheats();
    bindGlobalLists();
}
