/*
 * The Footsteps patch's script commands (src/game/gevr_footsteps.c) on a
 * real Mario Characters V3.17 cartridge: romcampaign_native.c binds it,
 * then this runs commands from its own level scripts and prints what they
 * changed in the port's tables. test_footsteps.py checks the lines after
 * "== footsteps". No ROM data is kept here.
 *
 * usage: footsteps_native <mario.z64> 4 <commands.txt>
 *   commands.txt: every 2B command of its setups, "<hex byte> <PRINT text>" a line
 */
#define main romcampaign_main
#include "romcampaign_native.c"
#undef main

#include "bondview.h"
#include "cheat.h"
#include "objective_status.h"
#include "gevr_footsteps.h"

static struct player s_player;
struct player *g_CurrentPlayer = &s_player;
u8 g_CheatPlayerTextRelated[0x4C];
u8 g_CheatActivated[CHEAT_MAX];
struct objective_entry *objective_ptrs[OBJECTIVES_MAX];
OBJECTIVESTATUS objectiveStatuses[OBJECTIVES_MAX];
u16 mTrack2Vol = 0x7fff;
s32 selected_stage = LEVELID_DAM;
f32 g_GlobalTimerDelta = 1.0f;
s32 g_ClockTimer = 2;
static s32 s_removed, s_killed;

void bondinvRemoveItemByID(ITEM_IDS weaponnum)
{
	(void)weaponnum;
	s_removed++;
}

void bondviewKillCurrentPlayer(void)
{
	s_killed++;
}

static s32 run(u8 arg, const char *text)
{
	u8 cmd[300];

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = 0x2b;
	cmd[1] = arg;
	if (text) {
		cmd[2] = 0xad;
		strncpy((char *)cmd + 3, text, sizeof(cmd) - 4);
	}
	return gevrFootstepsCommand(cmd);
}

static s32 musicX(s32 stage)
{
	s32 i;

	for (i = 0; music_setup_entries[i].stage_id; i++) {
		if (music_setup_entries[i].stage_id == stage) {
			return music_setup_entries[i].xtrack;
		}
	}
	return -1;
}

int main(int argc, char **argv)
{
	static struct objective_entry obj;
	FILE *f;
	char line[300];
	s32 n = 0;

	if (romcampaign_main(argc, argv) != 0 || argc < 4) {
		return 1;
	}
	printf("== footsteps\n");

	/* "Gun 02 uses Gknife1Z": item 2's file renamed in its data segment, as the item reads it */
	printf("fs item2 %s\n", gitem_structs[2].item_file_name);
	run(0xfe, "800537A0+C6666666A");
	printf("fs item2poked %s\n", gitem_structs[2].item_file_name);
	run(0xfe, "800537A0+C6665315A");
	printf("fs item2back %s\n", gitem_structs[2].item_file_name);

	/* Tiny Mode: its solo slot's scale (character 122, row + 8) */
	run(0xfe, "80703950+C3F080000");
	printf("fs scale122 %.5f\n", c_item_entries[122].scale);
	run(0xfe, "80703950+C3F800000");
	printf("fs scale122back %.4f\n", c_item_entries[122].scale);

	/* POV Height */
	run(0xfe, "800552FC+C3D6147AE");
	printf("fs headpos %.4f\n", g_GevrHeadPosOut);

	/* a cheat's bit copied to its own word, then tested (Play as Luigi: cheat 8) */
	g_CheatActivated[8] = 1;
	run(0xfd, "800696A8++8004E657+C00000001");
	printf("fs luigi %d\n", run(0xfe, "8004E654+E00000001+R07"));
	g_CheatActivated[8] = 0;
	run(0xfd, "800696A8++8004E657+C00000001");
	printf("fs noluigi %d\n", run(0xfe, "8004E654+E00000001+R07"));

	/* Bond's fields through g_CurrentPlayer */
	s_player.bondhealth = 0.5f;
	run(0xfe, "8007A0B0+P000000DC+C3F800000");
	printf("fs health %.3f\n", s_player.bondhealth);
	run(0xfe, "8007A0B0+P0000041C+C00000006");
	printf("fs bondtype %d\n", s_player.bondtype);
	s_player.bondarmour = 0.25f;
	printf("fs armourlow %d\n", run(0xfe, "8007A0B0+P000000E0+L3F000000+R05"));
	run(0xfd, "8007A0B0+P000000DC++8007A0B0+P000000E0+AF");
	printf("fs armoursum %.3f\n", s_player.bondarmour);
	run(0xfe, "8007A0B0+P000000E0+A3F800000FT");
	printf("fs armourtick %.3f\n", s_player.bondarmour);

	/* the stage's X track, and the music volume back */
	run(0x31, NULL);
	printf("fs xtrack %d\n", musicX(LEVELID_DAM));
	mTrack2Vol = 0x7fff;
	gevrFootstepsStageStart(LEVELID_DAM);
	mTrack2Vol = 0;
	run(0x00, NULL);
	printf("fs volume %d\n", mTrack2Vol);

	/* objectives: AI 0x36 flips one, the briefing then counts it */
	objective_ptrs[2] = &obj;
	obj.difficulty = 0;
	printf("fs objshown %d %d\n", gevrFootstepsObjectiveDifficulty(2, 0), gevrFootstepsObjectiveDifficulty(2, 4));
	gevrFootstepsToggleObjective(2);
	printf("fs objflipped %d %d %d\n", gevrFootstepsObjectiveDifficulty(2, 0), gevrFootstepsObjectiveDifficulty(2, 4),
			obj.difficulty);
	run(0xfe, "80075D60+A00000001");
	printf("fs objstatus %d\n", objectiveStatuses[2]);

	/* items and Bond's death */
	run(0xfc, "FF");   /* the cartridge reads hex in capitals */
	run(0xfb, NULL);
	printf("fs removed %d killed %d\n", s_removed, s_killed);

	/* every command its levels have, once each */
	f = fopen(argv[3], "r");
	if (!f) {
		return 2;
	}
	while (fgets(line, sizeof(line), f)) {
		unsigned arg;
		char text[280] = "";

		line[strcspn(line, "\r\n")] = 0;
		if (sscanf(line, "%x %279[^\n]", &arg, text) >= 1) {
			run((u8)arg, text[0] ? text : NULL);
			n++;
		}
	}
	fclose(f);
	printf("fs ran %d\n", n);
	return 0;
}
