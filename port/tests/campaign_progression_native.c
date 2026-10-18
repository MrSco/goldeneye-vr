#include <ultra64.h>
#include <bondconstants.h>
#include "gevr_romlayout.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct { int complete[20][4]; int flag_007; } save_data;
static save_data storage;
static int have_save = 1;
static save_data *fileGetSaveForFoldernum(int folder) { return folder == 0 && have_save ? &storage : NULL; }
static int fileIsFolderValid(int folder) { return folder == 0; }
static int fileGetSaveStageCompletedForDifficulty(save_data *save, int mission, int difficulty)
{
    assert(mission >= 0 && mission < 20 && difficulty >= 0 && difficulty < 4);
    return save->complete[mission][difficulty];
}
static int get_debug_enable_agent_levels_flag(void) { return 0; }
static int get_debug_enable_all_levels_flag(void) { return 0; }
/* INSERT_FILE */

static struct { int mission_num, stage_id, folder_text_preset; } mission_folder_setup_entries[48];
static int briefingpage, selected_stage, chosen_menu, chosen_flag, cursor, full_actor_intro;
static void frontChangeMenu(int menu, int flag) { chosen_menu = menu; chosen_flag = flag; }
static void set_cursor_to_stage_solo(int mission) { cursor = mission; }
static void completion(void)
{
    int entry;
    /* INSERT_COMPLETION */
}
static void return_from_credits(void)
{
    /* INSERT_CREDITS */
}
static void reset_save(void) { memset(&storage, 0, sizeof(storage)); have_save = 1; }
static void completed_through(int last, int difficulty)
{
    for (int mission = 0; mission <= last; mission++) storage.complete[mission][difficulty] = 1;
}
static void unlocks(void)
{
    /* Check both original 20-mission layouts as well as TND's 14-mission layout. */
    const int campaigns[] = {GEVR_CAMPAIGN_GE, GEVR_CAMPAIGN_GF64, GEVR_CAMPAIGN_TND64, GEVR_CAMPAIGN_MIRROR};
    for (int c = 0; c < (int)(sizeof(campaigns) / sizeof(campaigns[0])); c++)
    {
        int first_bonus = c == 2 ? 11 : 18;
        int second_bonus = c == 2 ? 12 : 19, count = c == 2 ? 14 : 20;
        gevrRomCampaignSet(campaigns[c]);
        reset_save();
        assert(!fileIsCradleCompletedForFolder(0));
        assert(!fileIsAztecCompletedOnSecretOr00ForFolder(0));
        assert(!fileIsEgyptCompletedOn00ForFolder(0));
        for (int d = 0; d < 4; d++)
        {
            assert(fileIsStageUnlockedAtDifficulty(0, 0, d) == STAGESTATUS_UNLOCKED);
            assert(fileIsStageUnlockedAtDifficulty(0, 1, d) == STAGESTATUS_LOCKED);
        }
        completed_through(first_bonus - 1, DIFFICULTY_AGENT);
        assert(fileIsStageUnlockedAtDifficulty(0, first_bonus, DIFFICULTY_AGENT) == STAGESTATUS_LOCKED);
        assert(fileIsStageUnlockedAtDifficulty(0, second_bonus, DIFFICULTY_AGENT) == STAGESTATUS_LOCKED);
        assert(fileIsCradleCompletedForFolder(0));
        assert(!fileIs007ModeUnlocked(0));

        reset_save();
        completed_through(first_bonus - 1, DIFFICULTY_SECRET);
        assert(fileIsStageUnlockedAtDifficulty(0, first_bonus, DIFFICULTY_SECRET) == STAGESTATUS_UNLOCKED);
        assert(fileIsStageUnlockedAtDifficulty(0, second_bonus, DIFFICULTY_SECRET) == STAGESTATUS_LOCKED);
        storage.complete[first_bonus][DIFFICULTY_SECRET] = 1;
        assert(fileIsAztecCompletedOnSecretOr00ForFolder(0));
        assert(!fileIsEgyptCompletedOn00ForFolder(0));

        reset_save();
        completed_through(second_bonus - 1, DIFFICULTY_00);
        assert(fileIsStageUnlockedAtDifficulty(0, second_bonus, DIFFICULTY_00) == STAGESTATUS_UNLOCKED);
        assert(!fileIs007ModeUnlocked(0));
        completed_through(count - 1, DIFFICULTY_00);
        assert(fileIs007ModeUnlocked(0));
        assert(fileIsEgyptCompletedOn00ForFolder(0));
        assert(fileGetHighestStageUnlockedForFolder(0) == count - 1);
        LEVEL_SOLO_SEQUENCE highest;
        DIFFICULTY difficulty;
        if (c == 2) storage.complete[19][DIFFICULTY_007] = 1; /* Ignore unused storage slots. */
        fileGetHighestStageDifficultyCompletedForFolder(0, &highest, &difficulty);
        assert(highest == count - 1 && difficulty == DIFFICULTY_00);
        assert(fileIsStageUnlockedAtDifficulty(0, count, DIFFICULTY_AGENT) == STAGESTATUS_LOCKED);
        have_save = 0;
        assert(fileIsStageUnlockedAtDifficulty(0, 0, DIFFICULTY_AGENT) == STAGESTATUS_UNLOCKED);
        assert(fileIsStageUnlockedAtDifficulty(0, 1, DIFFICULTY_AGENT) == STAGESTATUS_LOCKED);
        printf("PASS: campaign %d bonus gates, completion flags, save bounds and 007 unlock\n", campaigns[c]);
    }
}
static void ending(void)
{
    const int campaigns[] = {GEVR_CAMPAIGN_GE, GEVR_CAMPAIGN_GF64, GEVR_CAMPAIGN_TND64, GEVR_CAMPAIGN_MIRROR};
    for (int c = 0; c < (int)(sizeof(campaigns) / sizeof(campaigns[0])); c++)
    {
        int final = c == 2 ? 9 : 17, advance_limit = c == 2 ? 13 : 18;
        gevrRomCampaignSet(campaigns[c]);
        memset(mission_folder_setup_entries, 0, sizeof(mission_folder_setup_entries));
        briefingpage = 0;
        mission_folder_setup_entries[0].mission_num = final;
        selected_stage = -1;
        completion();
        assert(chosen_menu == MENU_RUN_STAGE && chosen_flag == TRUE && selected_stage == LEVELID_CUBA);
        cursor = -1;
        full_actor_intro = 1;
        return_from_credits();
        assert(chosen_menu == MENU_MISSION_SELECT && cursor == final && full_actor_intro == 0);

        mission_folder_setup_entries[0].mission_num = final - 1;
        mission_folder_setup_entries[1].folder_text_preset = 1;
        mission_folder_setup_entries[1].stage_id = -1; /* Skip a folder heading. */
        mission_folder_setup_entries[2].folder_text_preset = 1;
        mission_folder_setup_entries[2].stage_id = LEVELID_DAM;
        completion();
        assert(chosen_menu == MENU_BRIEFING && briefingpage == 2 && selected_stage == LEVELID_DAM);
        if (c == 2)
        {
            /* TND still advances from missions 10-12, including its bonuses. */
            for (int mission = 10; mission < 13; mission++)
            {
                briefingpage = 0;
                mission_folder_setup_entries[0].mission_num = mission;
                completion();
                assert(chosen_menu == MENU_BRIEFING && briefingpage == 2);
            }
        }
        for (int mission = advance_limit; mission < (c == 2 ? 14 : 20); mission++)
        {
            briefingpage = 0;
            mission_folder_setup_entries[0].mission_num = mission;
            completion();
            assert(chosen_menu == MENU_MISSION_SELECT && chosen_flag == FALSE && cursor == mission);
        }
        printf("PASS: campaign %d ending stage, credits return and next-mission transitions\n", campaigns[c]);
    }
}
int main(void)
{
    /* TEST_MODE */
    return 0;
}
