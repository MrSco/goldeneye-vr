/* Production save queries and co-op stage configuration, without the world. */
#include "net/netenet.h"
#include "net/net_core.h"
#include "net/net_protocol.h"
#include "net/net_coop.h"
#include "game/front.h"
#include "game/file2.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

save_data saves[SAVESLOTMAX];
s32 selected_folder_num, selected_stage, selected_num_players, briefingpage;
DIFFICULTY selected_difficulty;
GAMEMODE gamemode;
MENU menu_update, current_menu;
s32 g_StageNum;
s32 player_char[GEVR_MAX_PLAYERS], player_handicap[GEVR_MAX_PLAYERS], game_length;
static struct { u8 character[GEVR_MAX_PLAYERS]; } s_round;
static int s_max_players, s_lobby_max_players, stats_copies;
static bool s_coop_tally_valid;
static int unlocked_007;
static DIFFICULTY level_difficulty;

#define LOG_NOTE 0
#define NET_LOG(...) ((void)0)
void sysLogPrintf(int level, const char *fmt, ...) { (void)level; (void)fmt; }
/* Config also runs before game initialization, when save I/O is premature. */
void fileValidateSaves(void) { assert(!"save I/O in pre-start configuration"); }
s32 fileIs007ModeUnlocked(u32 folder) { assert(folder < MAX_FOLDER_COUNT); return unlocked_007; }
void init_mp_options_for_scenario(s32 count) { assert(count == NET_COOP_MAX_PLAYERS); }
void reset_mp_options_for_scenario(MPSCENARIOS scenario) { assert(scenario == SCENARIO_NORMAL); }
void do_extended_cast_display(s32 enabled) { assert(enabled); }
void lvlSetSelectedDifficulty(DIFFICULTY difficulty) { level_difficulty = difficulty; }
s32 pull_and_display_text_for_folder_a0(s32 mission) { return mission + 1; }
static void netCoopStatsToPlayerOne(void) { stats_copies++; }

/* INSERT_SAVE_FUNCTIONS */
/* INSERT_FOLDER_PREPARATION */
/* INSERT_COOP_CONFIG */

static void reset(void)
{
    memset(saves, 0, sizeof(saves));
    for (int i = 0; i < SAVESLOTMAX; i++) saves[i].completion_bitflags = SAVEFLAG_DORESET;
    stats_copies = 0;
    unlocked_007 = 0;
    menu_update = current_menu = MENU_INVALID;
}

/* Pack a known 10-bit value independently, one bit at a time in ROM order. */
static void put_time(save_data *save, int mission, int difficulty, int seconds)
{
    int offset = (difficulty * SP_LEVEL_MAX + mission) * 10;
    for (int bit = 0; bit < 10; bit++)
        if (seconds & (1 << (9 - bit))) save->times[(offset + bit) / 8] |= 1 << (7 - (offset + bit) % 8);
}

int main(int argc, char **argv)
{
    reset();
    if (argc > 1 && !strcmp(argv[1], "null-save")) {
        /* The report's Bunker I, Secret Agent call, including folder lookup. */
        assert(fileGetSaveStageDifficultyTime(fileGetSaveForFoldernum((u32)-1), SP_LEVEL_BUNKER1, DIFFICULTY_SECRET) == 0);
        puts("missing-save regression passed");
        return 0;
    }

    for (int mission = SP_LEVEL_DAM; mission < SP_LEVEL_MAX; mission++)
        for (int difficulty = DIFFICULTY_AGENT; difficulty < DIFFICULTY_MAX; difficulty++)
            assert(fileGetSaveStageDifficultyTime(NULL, mission, difficulty) == 0);

    save_data save = {0};
    for (int difficulty = 0; difficulty < DIFFICULTY_007; difficulty++)
        for (int mission = 0; mission < SP_LEVEL_MAX; mission++) {
            int expected = (mission * 37 + difficulty * 241) & 0x3ff;
            put_time(&save, mission, difficulty, expected);
        }
    for (int difficulty = 0; difficulty < DIFFICULTY_007; difficulty++)
        for (int mission = 0; mission < SP_LEVEL_MAX; mission++)
            assert(fileGetSaveStageDifficultyTime(&save, mission, difficulty) == ((mission * 37 + difficulty * 241) & 0x3ff));
    assert(fileGetSaveStageDifficultyTime(&save, SP_LEVEL_MAX, DIFFICULTY_AGENT) == 0);
    assert(fileGetSaveStageDifficultyTime(&save, SP_LEVEL_BUNKER1, DIFFICULTY_MAX) == 0);
    assert(fileGetSaveStageDifficultyTime(&save, SP_LEVEL_BUNKER1, DIFFICULTY_007) == 0);
    unlocked_007 = 1;
    assert(fileGetSaveStageDifficultyTime(&save, SP_LEVEL_BUNKER1, DIFFICULTY_007) == 0x3ff);

    NetMatchConfig config = {0};
    config.mode = NET_MODE_COOP;
    config.stage = LEVELID_BUNKER1;
    config.difficulty = DIFFICULTY_SECRET;
    selected_folder_num = -1;
    netApplyCoopConfig(&config);
    assert(selected_folder_num == FOLDER1);
    assert(g_StageNum == LEVELID_BUNKER1 && selected_stage == LEVELID_BUNKER1);
    assert(selected_num_players == NET_COOP_MAX_PLAYERS && gamemode == GAMEMODE_MULTI);
    assert(selected_difficulty == DIFFICULTY_SECRET && level_difficulty == DIFFICULTY_SECRET);
    assert(menu_update == MENU_MISSION_FAILED);

    /* Complete and return: Statistics gets a valid local folder and its time. */
    saves[0].completion_bitflags = FOLDER1;
    put_time(&saves[0], SP_LEVEL_BUNKER1, DIFFICULTY_SECRET, 340);
    config.stage = NET_COOP_FRONT_STAGE;
    netApplyCoopConfig(&config);
    assert(selected_folder_num == FOLDER1 && stats_copies == 1);
    assert(g_StageNum == LEVELID_TITLE && gamemode == GAMEMODE_SOLO && selected_num_players == 1);
    assert(fileGetSaveStageDifficultyTime(fileGetSaveForFoldernum(selected_folder_num), SP_LEVEL_BUNKER1, DIFFICULTY_SECRET) == 340);

    /* Each chosen local folder survives starts and returns. */
    for (int folder = FOLDER1; folder < MAX_FOLDER_COUNT; folder++) {
        selected_folder_num = folder;
        config.stage = LEVELID_BUNKER1;
        netApplyCoopConfig(&config);
        assert(selected_folder_num == folder);
        config.stage = NET_COOP_FRONT_STAGE;
        netApplyCoopConfig(&config);
        assert(selected_folder_num == folder);
    }
    /* Drop-in, invalid folder, or a return with no selection. */
    selected_folder_num = MAX_FOLDER_COUNT;
    config.stage = LEVELID_BUNKER1;
    netApplyCoopConfig(&config);
    assert(selected_folder_num == FOLDER1);
    selected_folder_num = -1;
    config.stage = NET_COOP_FRONT_STAGE;
    netApplyCoopConfig(&config);
    assert(selected_folder_num == FOLDER1);
    puts("co-op debrief: missing save, packed best times, start, return, local folder preservation and drop-in passed");
    return 0;
}
