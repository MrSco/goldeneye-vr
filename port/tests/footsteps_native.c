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
#include "chrai.h"
#include "propobj.h"
#include <snd.h>

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
static s32 s_removed, s_killed, s_sounds;

/* what the engine calls in the game, stood in for */
f32 toxic_gas_sound_timer = 5.0f;
s32 activate_gas_sound_timer;
f32 gasTimeToFullOpacity = 3600.0f;
u32 gasDoesDamageFlag = 1;
ALBank *g_musicSfxBufferPtr;
EnvironmentRecord *g_EnvironmentMainp, *g_EnvironmentAltp;
static u8 s_retail0D[] = { 0x04 };   /* stands in for GoldenEye's own list 0x000D: not the cartridge's */
AIListRecord g_GlobalAILists[] = { { (AIRecord *)s_retail0D, 0x0d }, { NULL, 0 } };
CheatInfo g_CheatInfo[17];   /* cheats 1-16 in their rows, as cheat.c has them; the end row */
const s32 gevrCheatInfoCount = 17;

ALSoundState *sndPlaySfx(struct ALBankAlt_s *soundBank, s16 soundIndex, ALSoundState *pendingState)
{
	(void)soundBank;
	(void)soundIndex;
	(void)pendingState;
	s_sounds++;
	return NULL;
}

void chrobjSndCreatePostEventDefault(ALSoundState *state, coord3d *pos)
{
	(void)state;
	(void)pos;
}

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

	/* its cheat rows, its global list 0x000D, its cheat rewiring, AI 0xFB */
	{
		s32 i;

		for (i = 0; i < 16; i++) {
			g_CheatInfo[i].cheat_id = (u8)(i + 1);
		}
	}
	gevrFootstepsBind();
	printf("fs cheats %04x %x %04x %x %04x %x %04x %x\n", g_CheatInfo[7].text_bank_id, g_CheatInfo[7].maskfield,
			g_CheatInfo[8].text_bank_id, g_CheatInfo[8].maskfield, g_CheatInfo[12].text_bank_id, g_CheatInfo[12].maskfield,
			g_CheatInfo[15].text_bank_id, g_CheatInfo[15].maskfield);
	printf("fs list0d %d\n", g_GlobalAILists[0].ailist != (AIRecord *)s_retail0D
			&& g_GlobalAILists[0].ailist == (AIRecord *)gevrRomWindowBytes(0x800372e0u, 1));
	printf("fs effects %d %d %d %d %d %d %d\n", gevrFootstepsCheatEffect(1), gevrFootstepsCheatEffect(5),
			gevrFootstepsCheatEffect(6), gevrFootstepsCheatEffect(8), gevrFootstepsCheatEffect(9),
			gevrFootstepsCheatEffect(13), gevrFootstepsCheatEffect(16));
	printf("fs unlocks %d %d %d %d %d %d\n", gevrFootstepsCheatUnlockAs(7), gevrFootstepsCheatUnlockAs(8),
			gevrFootstepsCheatUnlockAs(9), gevrFootstepsCheatUnlockAs(13), gevrFootstepsCheatUnlockAs(16),
			gevrFootstepsCheatUnlockAs(14));
	gevrFootstepsFogFade();
	printf("fs fogfade %.1f %d %.1f %u\n", toxic_gas_sound_timer, activate_gas_sound_timer, gasTimeToFullOpacity,
			gasDoesDamageFlag);

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

	/*
	 * Co-op (net_coop.c NET_MSG_COOP_POKES): what the commands wrote, as a
	 * teammate gets it. A fresh bind is the teammate's memory; the host's bytes
	 * copied in give the same hash. Every command of every level writes 153
	 * ranges, which the log since the last send holds (no overflow: 0 taken
	 * with no room given, -1 would mean "send everything").
	 */
	{
		static u32 st[2048], ln[2048];
		s32 ranges = gevrFootstepsTouchedWrites(st, ln, 2048), i, total = 0, taken;
		u32 host = gevrFootstepsWrittenHash(0, 0xffffffffu), fresh, mate;
		u8 *saved, *at;

		taken = gevrFootstepsTakeWrites(st, ln, 0);
		gevrFootstepsTouchedWrites(st, ln, 2048);
		for (i = 0; i < ranges; i++) {
			total += (s32)ln[i];
		}
		saved = at = (u8 *)malloc((size_t)total + 1);
		for (i = 0; i < ranges; i++) {
			memcpy(at, gevrRomWindowBytes(st[i], ln[i]), ln[i]);
			at += ln[i];
		}
		if (romcampaign_main(argc, argv) != 0) {
			return 5;
		}
		gevrFootstepsBind();
		fresh = gevrFootstepsWrittenHash(0, 0xffffffffu);
		for (i = 0, at = saved; i < ranges; i++) {
			gevrFootstepsApplyWrite(st[i], at, ln[i]);
			at += ln[i];
		}
		mate = gevrFootstepsWrittenHash(0, 0xffffffffu);
		printf("fs pokesync %d %d %d %d %d\n", ranges, total, taken, host == mate, fresh != host);
		free(saved);
	}

	/* every list of its setups, classified: a player-only cheat list or not (argv[5]) */
	if (argc > 5) {
		static char row[200000];
		static u8 bytes[100000];
		FILE *lf = fopen(argv[5], "r");
		s32 lists = 0, mine = 0;

		while (lf && fgets(row, sizeof(row), lf)) {
			char setup[40], *hex;
			unsigned id, k = 0;

			if (sscanf(row, "%39s %x", setup, &id) != 2 || !(hex = strrchr(row, ' '))) {
				continue;
			}
			for (hex++; hex[0] && hex[1] && hex[0] != '\n' && k < sizeof(bytes); hex += 2) {
				unsigned v;

				sscanf(hex, "%2x", &v);
				bytes[k++] = (u8)v;
			}
			/* the cache is by address: one buffer, so classify fresh each time */
			gevrFootstepsStageStart(LEVELID_DAM);
			if (gevrFootstepsPlayerList(bytes)) {
				printf("fs mine %s %04x\n", setup, id);
				mine++;
			}
			lists++;
		}
		if (lf) {
			fclose(lf);
		}
		printf("fs lists %d mine %d\n", lists, mine);
	}

	/* Temple's console list (page 2 of the mission select), in its order, on a freshly bound cartridge */
	if (argc > 4) {
		s32 i;

		if (romcampaign_main(argc, argv) != 0) {
			return 3;
		}
		f = fopen(argv[4], "r");
		if (!f) {
			return 4;
		}
		while (fgets(line, sizeof(line), f)) {
			unsigned arg;
			char text[280] = "";

			line[strcspn(line, "\r\n")] = 0;
			if (sscanf(line, "%x %279[^\n]", &arg, text) >= 1) {
				run((u8)arg, text[0] ? text : NULL);
			}
		}
		fclose(f);
		for (i = 0; mission_folder_setup_entries[i].folder_text_preset; i++) {
			printf("fs page2 %d stage %d type %d mission %d text %04x %s\n", i, mission_folder_setup_entries[i].stage_id,
					mission_folder_setup_entries[i].type, mission_folder_setup_entries[i].mission_num,
					mission_folder_setup_entries[i].folder_text_preset,
					mission_folder_setup_entries[i].string_ptr ? mission_folder_setup_entries[i].string_ptr : "-");
		}
	}
	return 0;
}
