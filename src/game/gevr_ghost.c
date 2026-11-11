/*
 * Ghost race (launcher Play > "Ghost race", VrGhostRace): race your best run
 * of a mission as a see-through Bond.
 *
 * Ported from Dab's Mod's Ghost Time Trials (src/game/modghost.c,
 * modghost.h): the same three independent parts, onto GoldenEye's own body
 * code.
 *
 * - Recording: 20 samples a second of where the run was, not what was
 *   pressed, so a replay never drifts from the physics: the ground position,
 *   the facing, the aim, the speeds the walk is chosen from, the crouch, the
 *   gun and whether it fired. Sample n is the run at n * 3 sixtieths of the
 *   mission clock (getMissiontimer, the debrief's), so a stretch the clock
 *   skipped (a cutscene, the watch) owes no samples and a long tick owes
 *   several.
 * - Saving: a completed mission that counts (the debrief's best-time rules,
 *   file.c end_of_mission_briefing) writes the run if it beats the best one
 *   on file for that mission and difficulty.
 * - Racing: that best run is replayed into a body of its own, chrAllocate'd
 *   with the outfit solo play gives Bond (gevrSoloBondBodyHead), no AI list,
 *   drawn see-through, walked by the multiplayer body's animation chooser
 *   (bondview2.c gevrPlayerBodyChooseAnim) from the recorded speeds, as Dab
 *   walks his with Perfect Dark's. Nothing collides with it, shoots it or
 *   aims at it (chrprop.c propIsOfCdType, chr.c chrTestHit).
 *
 * The split beside the speedrun timer is Dab's: the time the ghost took to
 * reach the sample nearest to where this run is, searched in a window round
 * the last match, so a route that crosses itself matches the right pass.
 *
 * The file (little endian, fixed size, hashed) is Dab's layout with this
 * port's magic and stamps: the header says what the run was set under
 * (stereo or flat, the art set, force pull, the ally, third person, the
 * movement speed, the campaign), shown on the launcher's row so a ghost is
 * raced knowingly. Each campaign keeps its own files (ge-, gf-, tnd-,
 * mirror-): a ROM hack's Dam is not GoldenEye's.
 *
 * Which run a mission races is the launcher's Ghosts tab's (vr_ghosts.cpp):
 * the best on file, or a run downloaded from the ghost boards (the lobby
 * Worker's ghosts.ts, Dab's ghost server), named in "<mission>-race.txt".
 */
#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "bondview.h"
#include "player.h"
#include "chr.h"
#include "gun.h"
#include "fs.h"
#include "system.h"
#include "gevr_ghost.h"

extern bool netIsActive(void);
extern s32 g_gevrStereo;
extern int VrGhostRace, VrSpeedrunTimer;   /* vr_settings_defaults.c */
extern int VrBeanModels, VrGexGuns, VrForcePull, VrAlly, VrThirdPerson, VrCampaign, VrMovementSpeed;
extern const char gevrBuildId[];

extern struct weapon_firing_animation_table *gevrPlayerBodyChooseAnim(struct player *pl, ChrRecord *chr, s32 crouchpos);
extern void gevrSoloBondBodyHead(s32 *body, s32 *head);
extern Model *retrieve_header_for_body_and_head(s32 body, s32 head, u32 bitflags);
extern PropRecord *chrAllocate(Model *model, coord3d *pos, f32 angle, StandTile *stan, AIRecord *ailist);
extern void chrpropActivateThisFrame(PropRecord *prop);
extern void chrpropEnable(PropRecord *prop);
extern s32 chrGetNumFree(void);
extern StandTile *stanFindGroundAtCyl(coord3d *pos, f32 radius, u8 *rooms, StandTile *prefer, f32 *groundy, s32 *incentre);
extern enum PROP getPropForHeldItem(ITEM_IDS item);
extern PropRecord *chrGiveWeapon(ChrRecord *self, s32 propid, ITEM_IDS item, s32 flags);
extern void chrSetWeaponFlag4(ChrRecord *chr, GUNHAND hand);
extern void objFreePermanently(struct ObjectRecord *obj, bool freeprop);
extern void chrSetFiring(ChrRecord *self, s32 hand, s32 firing);
extern void getsuboffset(Model *model, coord3d *offset);
extern void setsuboffset(Model *model, coord3d *offset);
extern void setsubroty(Model *model, f32 angle);
extern LEVELID bossGetStageNum(void);
extern DIFFICULTY lvlGetSelectedDifficulty(void);
extern s32 playerGetCrouchPos(s32 playernum);
extern s32 gevrBeanIsAvailable(void);

/* ---- the file ------------------------------------------------------------ */

#define GHOST_RATE60     3            /* a sample every 3/60 s: twenty a second */
#define GHOST_MAXSAMPLES 65536        /* fifty minutes, longer than any mission */
#define GHOST_GROWBY     (20 * 60 / GHOST_RATE60)
#define GHOST_DIR        "$S/ghosts"
#define GHOST_EXT        ".gvg"
#define GHOST_MAGIC      "GEVRGHO"
#define GHOST_VERSION    1
#define GHOST_MAXCOORD   1000000.0f   /* a bound on what a float from a file may be */

/* sample flags */
#define GSF_FIRE_RIGHT 0x01
#define GSF_FIRE_LEFT  0x02
#define GSF_DEAD       0x04
#define GSF_DUAL       0x08           /* the left hand held the same gun */

/* header flags: what the run was set under (gevr_ghost.h GEVR_GHF_*) */

struct gevrghostsample {
    /*0x00*/ f32 x;
    /*0x04*/ f32 y;
    /*0x08*/ f32 z;
    /*0x0c*/ u16 theta;               /* the facing, a turn in 65536ths */
    /*0x0e*/ s16 aimx;                /* the aim's pitch and yaw, radians in pi/32768ths */
    /*0x10*/ s8 forwards;             /* hundredths of the player's speeds */
    /*0x11*/ s8 sideways;
    /*0x12*/ s8 turn;
    /*0x13*/ u8 crouch;
    /*0x14*/ u8 weapon;
    /*0x15*/ u8 flags;
    /*0x16*/ s16 aimy;
};

struct gevrghostheader {
    /*0x00*/ char magic[8];
    /*0x08*/ u32 version;
    /*0x0c*/ u32 headersize;
    /*0x10*/ u32 samplesize;
    /*0x14*/ u32 numsamples;
    /*0x18*/ u32 time60;
    /*0x1c*/ u32 rate60;
    /*0x20*/ u8 stage;
    /*0x21*/ u8 difficulty;
    /*0x22*/ u8 campaign;
    /*0x23*/ u8 flags;
    /*0x24*/ u32 hash;
    /*0x28*/ u32 timestamp;
    /*0x2c*/ u8 body;
    /*0x2d*/ u8 head;
    /*0x2e*/ u16 pad;
    /*0x30*/ char player[32];
    /*0x50*/ char build[32];
    /*0x70*/ char owner[16];
};

_Static_assert(sizeof(struct gevrghostsample) == 24, "ghost sample layout is the file format");
_Static_assert(sizeof(struct gevrghostheader) == 128, "ghost header layout is the file format");

/* FNV-1a over the samples: tells a truncated copy from a whole one */
static u32 ghostHash(const void *data, u32 len)
{
    const u8 *p = data;
    u32 h = 2166136261u;
    u32 i;

    for (i = 0; i < len; i++)
    {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

static const char *ghostCampaignTag(s32 campaign)
{
    static const char *const tags[] = { "ge", "gf", "tnd", "mirror" };

    return campaign >= 0 && campaign < (s32)(sizeof(tags) / sizeof(tags[0])) ? tags[campaign] : "ge";
}

void gevrGhostFilePath(char *out, int n, int campaign, int stage, int difficulty, const char *what)
{
    snprintf(out, n, GHOST_DIR "/%s-s%02d-d%d-%s%s", ghostCampaignTag(campaign), stage, difficulty, what,
             strchr(what, '.') != NULL ? "" : GHOST_EXT);
}

static void ghostPath(char *out, size_t n, s32 campaign, s32 stage, s32 difficulty)
{
    gevrGhostFilePath(out, (int)n, campaign, stage, difficulty, "best");
}

void gevrGhostEnsureDir(void)
{
    if (fsFileSize(GHOST_DIR) < 0)
    {
        fsCreateDir(GHOST_DIR);
    }
}

/*
 * Read a ghost file, checking everything before believing any of it (Dab's
 * modGhostReadFile): the sizes against the file, the samples against the
 * hash, every position against a bound. Samples are read at the file's own
 * stride, so a later version with a longer sample still replays.
 */
static s32 ghostRead(const char *rel, struct gevrghostheader *hdr, struct gevrghostsample **out)
{
    FILE *f = fsFileOpenRead(rel);
    struct gevrghostsample *samples = NULL;
    u8 skip[64];
    u32 i;

    *out = NULL;
    if (f == NULL)
    {
        return FALSE;
    }
    if (fread(hdr, sizeof(*hdr), 1, f) != 1 || memcmp(hdr->magic, GHOST_MAGIC, sizeof(GHOST_MAGIC)) != 0
        || hdr->headersize < sizeof(*hdr) || hdr->headersize > 4096
        || hdr->samplesize < sizeof(struct gevrghostsample) || hdr->samplesize > sizeof(struct gevrghostsample) + sizeof(skip)
        || hdr->numsamples < 2 || hdr->numsamples > GHOST_MAXSAMPLES || hdr->rate60 == 0 || hdr->rate60 > 60)
    {
        fclose(f);
        return FALSE;
    }
    if (hdr->headersize > sizeof(*hdr) && fseek(f, hdr->headersize, SEEK_SET) != 0)
    {
        fclose(f);
        return FALSE;
    }
    samples = malloc(hdr->numsamples * sizeof(*samples));
    if (samples == NULL)
    {
        fclose(f);
        return FALSE;
    }
    for (i = 0; i < hdr->numsamples; i++)
    {
        if (fread(&samples[i], sizeof(*samples), 1, f) != 1
            || (hdr->samplesize > sizeof(*samples) && fread(skip, hdr->samplesize - sizeof(*samples), 1, f) != 1)
            || !(fabsf(samples[i].x) < GHOST_MAXCOORD && fabsf(samples[i].y) < GHOST_MAXCOORD && fabsf(samples[i].z) < GHOST_MAXCOORD))
        {
            break;
        }
    }
    fclose(f);
    if (i != hdr->numsamples
        || (hdr->samplesize == sizeof(*samples) && ghostHash(samples, i * sizeof(*samples)) != hdr->hash))
    {
        free(samples);
        return FALSE;
    }
    *out = samples;
    return TRUE;
}

/* ---- the launcher's (gevr_ghost.h) -------------------------------------- */

int gevrGhostFileInfo(const char *rel, GevrGhostInfo *out)
{
    struct gevrghostheader hdr;
    struct gevrghostsample *samples;

    memset(out, 0, sizeof(*out));
    if (!ghostRead(rel, &hdr, &samples))
    {
        return 0;
    }
    free(samples);
    out->time60 = (int)hdr.time60;
    out->numsamples = (int)hdr.numsamples;
    out->stage = hdr.stage;
    out->difficulty = hdr.difficulty;
    out->campaign = hdr.campaign;
    out->flags = hdr.flags;
    out->timestamp = hdr.timestamp;
    memcpy(out->owner, hdr.owner, sizeof(hdr.owner));
    out->owner[sizeof(hdr.owner)] = '\0';
    return 1;
}

/*
 * Dab's ghosts say which account set them, and only that account may publish
 * one (pdghostd's /upload). Here the account is the Friends identity, so the
 * owner is the friend code, written into a run of this headset's before its
 * first upload: the hash is over the samples, so the run is unchanged.
 */
int gevrGhostStampOwner(const char *rel, const char *friendCode)
{
    struct gevrghostheader hdr;
    struct gevrghostsample *samples;
    FILE *f;
    s32 ok;

    if (friendCode == NULL || friendCode[0] == '\0' || strlen(friendCode) >= sizeof(hdr.owner)
        || !ghostRead(rel, &hdr, &samples))
    {
        return 0;
    }
    if (hdr.owner[0] != '\0')
    {
        free(samples);
        return strncmp(hdr.owner, friendCode, sizeof(hdr.owner)) == 0;
    }
    /* this build's own shape only: a later one's fields would be lost on the rewrite */
    if (hdr.headersize != sizeof(hdr) || hdr.samplesize != sizeof(*samples))
    {
        free(samples);
        return 0;
    }
    strncpy(hdr.owner, friendCode, sizeof(hdr.owner) - 1);
    f = fsFileOpenWrite(rel);
    ok = f != NULL && fwrite(&hdr, sizeof(hdr), 1, f) == 1
         && fwrite(samples, sizeof(*samples), hdr.numsamples, f) == hdr.numsamples;
    if (f != NULL)
    {
        fclose(f);
    }
    free(samples);
    return ok;
}

/* a file name in the ghosts folder, and nothing that leaves it */
static s32 ghostSafeName(const char *name)
{
    size_t len = strlen(name);
    size_t i;

    if (len < 5 || len > 64 || strcmp(name + len - 4, GHOST_EXT) != 0 || strstr(name, "..") != NULL)
    {
        return FALSE;
    }
    for (i = 0; i < len; i++)
    {
        char c = name[i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
        {
            return FALSE;
        }
    }
    return TRUE;
}

void gevrGhostGetRace(int campaign, int stage, int difficulty, char *out, int n)
{
    char rel[128];
    FILE *f;

    out[0] = '\0';
    gevrGhostFilePath(rel, sizeof(rel), campaign, stage, difficulty, "race.txt");
    f = fsFileOpenRead(rel);
    if (f == NULL)
    {
        return;
    }
    if (fgets(out, n, f) == NULL)
    {
        out[0] = '\0';
    }
    fclose(f);
    out[strcspn(out, "\r\n")] = '\0';
    if (out[0] != '\0' && !ghostSafeName(out))
    {
        out[0] = '\0';
    }
}

/* "" back to the best run: an empty choice rather than a deleted file */
void gevrGhostSetRace(int campaign, int stage, int difficulty, const char *fileName)
{
    char rel[128];
    FILE *f;

    if (fileName == NULL || (fileName[0] != '\0' && !ghostSafeName(fileName)))
    {
        return;
    }
    gevrGhostEnsureDir();
    gevrGhostFilePath(rel, sizeof(rel), campaign, stage, difficulty, "race.txt");
    f = fsFileOpenWrite(rel);
    if (f != NULL)
    {
        fprintf(f, "%s\n", fileName);
        fclose(f);
    }
}

/* ---- recording --------------------------------------------------------- */

static struct gevrghostsample *s_rec;
static s32 s_recCount;
static s32 s_recCap;
static s32 s_recFull;
static s32 s_recStage = -1;
static s32 s_recDifficulty = -1;
static s32 s_recCampaign;
static u8 s_recFlags;

static s8 ghostSpeed(f32 v)
{
    s32 i = (s32)lroundf(v * 100.0f);
    return (s8)(i > 127 ? 127 : i < -127 ? -127 : i);
}

static s16 ghostRadians(f32 r)
{
    s32 i = (s32)lroundf(r * (32768.0f / M_PI_F));
    return (s16)(i > 32767 ? 32767 : i < -32767 ? -32767 : i);
}

static f32 ghostUnRadians(s16 v)
{
    return v * (M_PI_F / 32768.0f);
}

/* the short way round a 65536th turn */
static u16 ghostLerpTurn(u16 a, u16 b, f32 frac)
{
    s16 d = (s16)(b - a);
    return (u16)(a + (s32)lroundf(d * frac));
}

/* A solo mission, with Bond in it: online and split screen have no ghost */
static s32 ghostEligible(void)
{
    return !netIsActive() && getPlayerCount() == 1 && g_CurrentPlayer != NULL && g_CurrentPlayer->prop != NULL;
}

/* what changes a run, stamped into its file */
static u8 ghostRunFlags(void)
{
    u8 f = 0;

    if (g_gevrStereo) f |= GEVR_GHF_STEREO;
    if (VrBeanModels && gevrBeanIsAvailable()) f |= GEVR_GHF_BEAN;
    if (VrGexGuns) f |= GEVR_GHF_GEX;
    if (VrForcePull) f |= GEVR_GHF_FORCEPULL;
    if (VrAlly) f |= GEVR_GHF_ALLY;
    if (VrThirdPerson && !g_gevrStereo) f |= GEVR_GHF_THIRD;
    /* the launcher's Movement speed (net_rules.h NET_MOVE_NORMAL is 0) */
    if (VrMovementSpeed != 0) f |= GEVR_GHF_SPEED;
    return f;
}

static s32 ghostGrow(void)
{
    struct gevrghostsample *n;
    s32 cap;

    if (s_recCount < s_recCap)
    {
        return TRUE;
    }
    if (s_recCap >= GHOST_MAXSAMPLES)
    {
        return FALSE;
    }
    cap = s_recCap + GHOST_GROWBY;
    if (cap > GHOST_MAXSAMPLES)
    {
        cap = GHOST_MAXSAMPLES;
    }
    n = realloc(s_rec, cap * sizeof(*n));
    if (n == NULL)
    {
        return FALSE;
    }
    s_rec = n;
    s_recCap = cap;
    return TRUE;
}

/*
 * Once a tick, after Bond moved (bondview2.c, with the mission clock). The
 * debt of samples the clock has run up is paid with the run's state now:
 * standing still is what a missed sample looked like anyway (Dab).
 */
void gevrGhostRecord(void)
{
    struct player *pl = g_CurrentPlayer;
    s32 time60;

    if (s_recFull || !(VrGhostRace || VrSpeedrunTimer) || !ghostEligible())
    {
        return;
    }
    if (s_recStage < 0)
    {
        s_recStage = bossGetStageNum();
        s_recDifficulty = lvlGetSelectedDifficulty();
        s_recCampaign = VrCampaign;
        s_recFlags = ghostRunFlags();
    }
    time60 = getMissiontimer();
    while (s_recCount * GHOST_RATE60 <= time60)
    {
        struct gevrghostsample *s;
        ITEM_IDS right;
        ITEM_IDS left;

        if (!ghostGrow())
        {
            s_recFull = TRUE;
            sysLogPrintf(LOG_WARNING, "ghost: recording full at %d samples", s_recCount);
            return;
        }
        s = &s_rec[s_recCount++];
        s->x = pl->prop->pos.x;
        s->y = pl->prop->pos.y;
        s->z = pl->prop->pos.z;
        s->theta = (u16)(s32)lroundf(pl->vv_theta * (65536.0f / 360.0f));
        s->aimx = ghostRadians(pl->field_2A08);
        s->aimy = ghostRadians(pl->field_2A0C);
        s->forwards = ghostSpeed(pl->speedforwards);
        s->sideways = ghostSpeed(pl->speedsideways);
        s->turn = ghostSpeed(pl->speedtheta);
        s->crouch = (u8)playerGetCrouchPos(get_cur_playernum());
        right = getCurrentPlayerWeaponId(GUNRIGHT);
        left = getCurrentPlayerWeaponId(GUNLEFT);
        s->weapon = (u8)((s32)right > 0 && (s32)right < 256 ? right : 0);
        s->flags = 0;
        if (left == right && (s32)left > ITEM_UNARMED) s->flags |= GSF_DUAL;
        if (pl->hands[GUNRIGHT].field_87D) s->flags |= GSF_FIRE_RIGHT;
        if (pl->hands[GUNLEFT].field_87D) s->flags |= GSF_FIRE_LEFT;
        if (pl->bonddead) s->flags |= GSF_DEAD;
    }
}

/*
 * The run is over and counts (file.c end_of_mission_briefing, under the
 * debrief's own best-time rules: no progress-blocking cheats, not 007). It
 * is kept if it beats the ghost on file for that mission and difficulty.
 * The clock is read here, where the debrief reads it.
 */
static void ghostSave(s32 force)
{
    struct gevrghostheader hdr;
    struct gevrghostheader old;
    struct gevrghostsample *oldsamples;
    char rel[96];
    s32 time60 = getMissiontimer();
    s32 body = 0;
    s32 head = 0;
    FILE *f;

    if (s_recCount < 2 || s_recStage < 0 || time60 < 1)
    {
        return;
    }
    ghostPath(rel, sizeof(rel), s_recCampaign, s_recStage, s_recDifficulty);
    if (!force && ghostRead(rel, &old, &oldsamples))
    {
        free(oldsamples);
        if ((s32)old.time60 <= time60)
        {
            sysLogPrintf(LOG_NOTE, "ghost: %d:%02d does not beat the ghost on file (%d:%02d)",
                         time60 / 3600, (time60 / 60) % 60, old.time60 / 3600, (old.time60 / 60) % 60);
            return;
        }
    }
    gevrGhostEnsureDir();

    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, GHOST_MAGIC, sizeof(GHOST_MAGIC));
    hdr.version = GHOST_VERSION;
    hdr.headersize = sizeof(hdr);
    hdr.samplesize = sizeof(struct gevrghostsample);
    hdr.numsamples = s_recCount;
    hdr.time60 = time60;
    hdr.rate60 = GHOST_RATE60;
    hdr.stage = (u8)s_recStage;
    hdr.difficulty = (u8)s_recDifficulty;
    hdr.campaign = (u8)s_recCampaign;
    hdr.flags = s_recFlags;
    hdr.hash = ghostHash(s_rec, s_recCount * sizeof(*s_rec));
    hdr.timestamp = (u32)time(NULL);
    gevrSoloBondBodyHead(&body, &head);
    hdr.body = (u8)(body >= 0 && body < 256 ? body : 0);
    hdr.head = (u8)(head >= 0 && head < 256 ? head : 0);
    strncpy(hdr.player, "Bond", sizeof(hdr.player) - 1);
    strncpy(hdr.build, gevrBuildId, sizeof(hdr.build) - 1);

    f = fsFileOpenWrite(rel);
    if (f == NULL)
    {
        sysLogPrintf(LOG_ERROR, "ghost: could not write %s", fsFullPath(rel));
        return;
    }
    if (fwrite(&hdr, sizeof(hdr), 1, f) != 1 || fwrite(s_rec, sizeof(*s_rec), s_recCount, f) != (size_t)s_recCount)
    {
        sysLogPrintf(LOG_ERROR, "ghost: %s was not written in full", fsFullPath(rel));
    }
    else
    {
        sysLogPrintf(LOG_NOTE, "ghost: saved %s (%d:%02d.%02d, %d samples)", fsFullPath(rel),
                     time60 / 3600, (time60 / 60) % 60, ((time60 % 60) * 100) / 60, s_recCount);
    }
    fclose(f);
}

void gevrGhostMissionComplete(void)
{
    ghostSave(FALSE);
}

/* test hook (bondview2.c gevr_cheat.txt "ghostsave"): keep the run so far */
void gevrGhostSaveNow(void)
{
    ghostSave(TRUE);
}

/* ---- racing ------------------------------------------------------------ */

#define GHOST_ALPHA          0x70
#define GHOST_SPLITWINDOW    (8 * 60 / GHOST_RATE60)
#define GHOST_SPLITMAXDISTSQ (1000.0f * 1000.0f)
#define GHOST_RADIUS         20.0f

static struct {
    struct gevrghostsample *samples;
    s32 count;
    s32 rate60;
    s32 time60;
    s32 body;
    s32 head;
    u8 flags;
    ChrRecord *chr;
    s32 chrnum;
    s32 held[2];
    s32 loaded;                 /* the field for this stage was read */
    s32 splitIdx;
    s32 split60;
    s32 splitValid;
} s_race;

/* the multiplayer chooser's state for the ghost's body (players_cur_animation, the lean) */
static struct player s_ghostPlayer;

s32 gevrGhostIsChr(ChrRecord *chr)
{
    return chr != NULL && chr == s_race.chr;
}

s32 gevrGhostRacing(void)
{
    return VrGhostRace && s_race.samples != NULL && ghostEligible();
}

s32 gevrGhostHasSplit(void)
{
    return gevrGhostRacing() && s_race.splitValid;
}

s32 gevrGhostSplit60(void)
{
    return s_race.split60;
}

static void ghostFreeRun(void)
{
    free(s_race.samples);
    s_race.samples = NULL;
    s_race.count = 0;
}

/*
 * The run ended, or racing was turned off: the body is hidden, not removed.
 * Removing it (CHRHIDDEN_REMOVE) freed the gun in its hand while objTick
 * still had it (2026-11-11, SIGSEGV in getinstsize the tick the ghost's
 * Dam run ended). Hidden, it stays out of every test (propIsOfCdType,
 * chrTestHit) and goes with the stage's memory; racing again shows it.
 */
static void ghostRetire(void)
{
    if (s_race.chr != NULL)
    {
        s_race.chr->chrflags |= CHRFLAG_HIDDEN;
    }
}

/*
 * A new stage: the body came out of the old one's memory, so it is dropped,
 * not removed. A mission (not the menus, where the debrief saves the run
 * that just ended) starts a fresh recording and reads its ghost again, so a
 * retry races the best run, saved or not a minute ago (Dab: "a time trial is
 * the same mission over and over").
 */
void gevrGhostStageReset(s32 stage)
{
    s_race.chr = NULL;
    s_race.chrnum = -1;
    s_race.loaded = FALSE;
    s_race.splitIdx = 0;
    s_race.splitValid = FALSE;
    if (stage >= 0 && stage < LEVELID_MAX)
    {
        s_recCount = 0;
        s_recFull = FALSE;
        s_recStage = -1;
    }
}

/* a run for this mission: one recorded or downloaded for another is never raced here */
static s32 ghostReadFor(const char *rel, s32 stage, s32 difficulty, struct gevrghostheader *hdr, struct gevrghostsample **samples)
{
    if (!ghostRead(rel, hdr, samples))
    {
        return FALSE;
    }
    if (hdr->stage != stage || hdr->difficulty != difficulty || hdr->campaign != VrCampaign)
    {
        sysLogPrintf(LOG_WARNING, "ghost: %s is a run of another mission (stage %d difficulty %d campaign %d)", rel,
                     hdr->stage, hdr->difficulty, hdr->campaign);
        free(*samples);
        *samples = NULL;
        return FALSE;
    }
    return TRUE;
}

static void ghostLoad(void)
{
    struct gevrghostheader hdr;
    struct gevrghostsample *samples;
    char rel[128];
    char chosen[80];
    s32 stage = bossGetStageNum();
    s32 difficulty = lvlGetSelectedDifficulty();

    s_race.loaded = TRUE;
    ghostFreeRun();
    /* the run the launcher's Ghosts tab chose for this mission, else the best on file */
    gevrGhostGetRace(VrCampaign, stage, difficulty, chosen, sizeof(chosen));
    snprintf(rel, sizeof(rel), GHOST_DIR "/%s", chosen);
    if (chosen[0] == '\0' || !ghostReadFor(rel, stage, difficulty, &hdr, &samples))
    {
        ghostPath(rel, sizeof(rel), VrCampaign, stage, difficulty);
        if (!ghostReadFor(rel, stage, difficulty, &hdr, &samples))
        {
            return;
        }
    }
    s_race.samples = samples;
    s_race.count = hdr.numsamples;
    s_race.rate60 = hdr.rate60;
    s_race.time60 = hdr.time60;
    s_race.body = hdr.body;
    s_race.head = hdr.head;
    s_race.flags = hdr.flags;
    s_race.held[0] = s_race.held[1] = -1;
    sysLogPrintf(LOG_NOTE, "ghost: racing %s (%d:%02d.%02d, %d samples, flags %02x)", rel,
                 hdr.time60 / 3600, (hdr.time60 / 60) % 60, ((hdr.time60 % 60) * 100) / 60, hdr.numsamples, hdr.flags);
}

/* the run at time60, position, facing and aim eased between its samples */
static void ghostSampleAt(struct gevrghostsample *dst, s32 time60)
{
    const struct gevrghostsample *a;
    const struct gevrghostsample *b;
    s32 index;
    f32 frac;

    if (time60 < 0)
    {
        time60 = 0;
    }
    index = time60 / s_race.rate60;
    if (index >= s_race.count - 1)
    {
        *dst = s_race.samples[s_race.count - 1];
        return;
    }
    a = &s_race.samples[index];
    b = &s_race.samples[index + 1];
    frac = (time60 - index * s_race.rate60) / (f32)s_race.rate60;
    *dst = *a;
    dst->x = a->x + (b->x - a->x) * frac;
    dst->y = a->y + (b->y - a->y) * frac;
    dst->z = a->z + (b->z - a->z) * frac;
    dst->theta = ghostLerpTurn(a->theta, b->theta, frac);
    dst->aimx = (s16)ghostLerpTurn((u16)a->aimx, (u16)b->aimx, frac);
    dst->aimy = (s16)ghostLerpTurn((u16)a->aimy, (u16)b->aimy, frac);
}

/* The body a run put back where the chooser expects it: nothing hits it */
static void ghostSetFlags(ChrRecord *chr)
{
    chr->chrflags |= CHRFLAG_INVINCIBLE | CHRFLAG_NO_AUTOAIM | CHRFLAG_NO_SHADOW | CHRFLAG_INIT;
    chr->fadealpha = GHOST_ALPHA;
}

/* the floor under the ghost: the last tile's room first, as a remote player's (net_player_sync.c) */
static StandTile *ghostTile(PropRecord *prop, coord3d *pos)
{
    u8 rooms[PROPRECORD_STAN_ROOM_LEN + 2];
    StandTile *prev = prop != NULL ? prop->stan : NULL;
    f32 floory;
    s32 incentre;
    s32 n = 0;
    s32 i;

    if (prev != NULL)
    {
        rooms[n++] = prev->room;
    }
    for (i = 0; prop != NULL && i < PROPRECORD_STAN_ROOM_LEN && prop->rooms[i] != 0xff; i++)
    {
        rooms[n++] = prop->rooms[i];
    }
    rooms[n] = 0xff;
    return stanFindGroundAtCyl(pos, GHOST_RADIUS, n ? rooms : NULL, prev, &floory, &incentre);
}

static s32 ghostBuild(const struct gevrghostsample *s)
{
    coord3d pos;
    StandTile *stan;
    Model *model;
    PropRecord *prop;
    s32 body = s_race.body;
    s32 head = s_race.head;

    if (chrGetNumFree() < 3)
    {
        return FALSE;
    }
    pos.x = s->x;
    pos.y = s->y;
    pos.z = s->z;
    stan = ghostTile(NULL, &pos);
    if (stan == NULL)
    {
        return FALSE;
    }
    if (body == 0 && head == 0)
    {
        gevrSoloBondBodyHead(&body, &head);
    }
    model = retrieve_header_for_body_and_head(body, head, 0);
    if (model == NULL)
    {
        return FALSE;
    }
    prop = chrAllocate(model, &pos, (360.0f - s->theta * (360.0f / 65536.0f)) * (M_TAU_F / 360.0f), stan, NULL);
    if (prop == NULL || prop->chr == NULL)
    {
        return FALSE;
    }
    chrpropActivateThisFrame(prop);
    chrpropEnable(prop);
    s_race.chr = prop->chr;
    s_race.chrnum = prop->chr->chrnum;
    s_race.held[0] = s_race.held[1] = -1;
    prop->chr->actiontype = ACT_BONDMULTI;
    prop->chr->act_bondmulti.unk2c = NULL;
    prop->chr->maxdamage = 1000.0f;
    ghostSetFlags(prop->chr);
    memset(&s_ghostPlayer, 0, sizeof(s_ghostPlayer));
    sysLogPrintf(LOG_NOTE, "ghost: body %d head %d built at %.0f %.0f %.0f", body, head, pos.x, pos.y, pos.z);
    return TRUE;
}

/* the gun the run held in a hand, swapped as a remote player's (net_player_sync.c netSyncCopyHand) */
static void ghostSetHand(ChrRecord *chr, s32 hand, s32 weapon)
{
    s32 hasmodel;

    if (s_race.held[hand] == weapon)
    {
        return;
    }
    hasmodel = weapon > ITEM_UNARMED && weapon < ITEM_IDS_MAX && (s32)getPropForHeldItem((ITEM_IDS)weapon) >= 0;
    chrSetWeaponFlag4(chr, hand);
    if (chr->weapons_held[hand] != NULL && chr->weapons_held[hand]->obj != NULL)
    {
        objFreePermanently(chr->weapons_held[hand]->obj, 1);
    }
    if (hasmodel)
    {
        chrGiveWeapon(chr, getPropForHeldItem((ITEM_IDS)weapon), (ITEM_IDS)weapon, hand == GUNLEFT ? PROPFLAG_WEAPON_LEFTHANDED : 0);
    }
    s_race.held[hand] = weapon;
}

/*
 * Pose the body for this moment of the run, in playerTick's order for a
 * multiplayer body: the walk chosen from the speeds, the root at the sample,
 * the facing with the chooser's lean, then the body's own tick draws it.
 */
static void ghostPose(const struct gevrghostsample *s)
{
    ChrRecord *chr = s_race.chr;
    PropRecord *prop = chr->prop;
    struct weapon_firing_animation_table *table;
    StandTile *tile;
    coord3d pos;
    coord3d off;
    f32 theta = s->theta * (360.0f / 65536.0f);

    ghostSetHand(chr, GUNRIGHT, s->weapon);
    ghostSetHand(chr, GUNLEFT, (s->flags & GSF_DUAL) ? s->weapon : ITEM_UNARMED);

    pos.x = s->x;
    pos.y = s->y;
    pos.z = s->z;
    tile = ghostTile(prop, &pos);
    if (tile != NULL)
    {
        prop->stan = tile;
    }
    prop->pos = pos;
    chrpropActivateThisFrame(prop);

    s_ghostPlayer.speedforwards = s->forwards / 100.0f;
    s_ghostPlayer.speedsideways = s->sideways / 100.0f;
    s_ghostPlayer.speedtheta = s->turn / 100.0f;
    s_ghostPlayer.field_2A08 = ghostUnRadians(s->aimx);
    s_ghostPlayer.field_2A0C = ghostUnRadians(s->aimy);
    /* never the death pose: the chooser picks it with randomGetNext, and a
     * ghost must not move the game's random numbers (guards' rolls, routes)
     * away from a run without one. A saved run never died anyway. */
    s_ghostPlayer.bonddead = FALSE;
    s_ghostPlayer.headanim = 0;
    s_ghostPlayer.bodyModel = chr->model;
    table = gevrPlayerBodyChooseAnim(&s_ghostPlayer, chr, s->crouch);

    getsuboffset(chr->model, &off);
    off.x = pos.x;
    off.z = pos.z;
    setsuboffset(chr->model, &off);
    setsubroty(chr->model, ((360.0f - theta + s_ghostPlayer.field_1280) * M_TAU_F) / 360.0f);

    chr->chrflags &= ~CHRFLAG_HIDDEN;
    chr->actiontype = ACT_BONDMULTI;
    chr->act_bondmulti.unk2c = (f32 *)table;
    chrSetFiring(chr, GUNRIGHT, (s->flags & GSF_FIRE_RIGHT) != 0);
    chrSetFiring(chr, GUNLEFT, (s->flags & GSF_FIRE_LEFT) != 0);
    ghostSetFlags(chr);
}

/* Dab's split: how long the ghost took to reach the sample nearest to here */
static void ghostUpdateSplit(s32 time60)
{
    const coord3d *pos = &g_CurrentPlayer->prop->pos;
    f32 bestdist = GHOST_SPLITMAXDISTSQ;
    s32 best = -1;
    s32 from = s_race.splitIdx - GHOST_SPLITWINDOW;
    s32 to = s_race.splitIdx + GHOST_SPLITWINDOW;
    s32 i;

    if (from < 0) from = 0;
    if (to >= s_race.count) to = s_race.count - 1;
    for (i = from; i <= to; i++)
    {
        f32 dx = s_race.samples[i].x - pos->x;
        f32 dy = s_race.samples[i].y - pos->y;
        f32 dz = s_race.samples[i].z - pos->z;
        f32 d = dx * dx + dy * dy + dz * dz;

        if (d < bestdist)
        {
            bestdist = d;
            best = i;
        }
    }
    if (best < 0)
    {
        s_race.splitValid = FALSE;
        return;
    }
    s_race.splitIdx = best;
    s_race.split60 = time60 - best * s_race.rate60;
    s_race.splitValid = TRUE;
}

/* Once a tick, after the recording (bondview2.c) */
void gevrGhostTick(void)
{
    struct gevrghostsample sample;
    s32 time60;

    /* a body freed under the ghost (a chr slot reused) is never posed */
    if (s_race.chr != NULL && (s_race.chr->chrnum != s_race.chrnum || s_race.chr->prop == NULL
                               || s_race.chr->prop->chr != s_race.chr || s_race.chr->model == NULL))
    {
        s_race.chr = NULL;
    }
    if (!VrGhostRace || !ghostEligible())
    {
        ghostRetire();
        return;
    }
    if (!s_race.loaded)
    {
        ghostLoad();
    }
    if (s_race.samples == NULL)
    {
        return;
    }
    /* the clock stopped: a cutscene's camera, the watch, controls locked */
    if (g_CameraMode != CAMERAMODE_FP || !is_timer_active)
    {
        return;
    }
    time60 = getMissiontimer();
    ghostUpdateSplit(time60);
    /* the ghost's run is over: it goes, rather than stand in a doorway */
    if (time60 >= s_race.time60)
    {
        ghostRetire();
        return;
    }
    ghostSampleAt(&sample, time60);
    if (s_race.chr == NULL && !ghostBuild(&sample))
    {
        return;
    }
    ghostPose(&sample);
}
