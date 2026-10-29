/*
 * The dedicated server's launcher (docs/dedicated-server.md).
 *
 * On a headset, port/vr/vr_launcher.cpp runs before the ROM loads: the
 * player picks a game and presses Start Hosting, then Launch. Here the same
 * calls are made from goldeneye-vr.ini with nobody pressing anything:
 *
 *   gevr-server [--basedir DIR] [--port N] [--seconds N]
 *
 * The data folder holds ge.z64 and goldeneye-vr.ini, as on the headset. The
 * match is the ini's Host page: stage, scenario, weapons, bots and so on.
 *
 * Phase P0: the server still takes slot 0 as a player who never moves.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>
#include <fenv.h>
#include <signal.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include <PR/ultratypes.h>

#include "system.h"
#include "boss.h"
#include "net_core.h"
#include "net_match.h"
#include "net_game.h"
#include "game/player.h"
#include "game/gevr_bot.h"

extern int VrMpChr, VrMpLoadout[4];
extern s32 g_StageNum;
extern u32 gevrRetraceClockFrames;   /* gevr_engine_shim.c: game frames handed out */
int gevrVrPumpBegin(void);
void gevrVrPumpEnd(void);

static long long s_stopAtNs;   /* --seconds: exit after this long (measurement runs) */

static long long serverNowNs(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000000000ll + ts.tv_nsec;
}

static double serverCpuSeconds(void)
{
	struct rusage ru;
	getrusage(RUSAGE_SELF, &ru);
	return ru.ru_utime.tv_sec + ru.ru_stime.tv_sec + (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1e6;
}

static void serverFpeHandler(int sig)
{
	void *frames[48];
	const int n = backtrace(frames, 48);
	fprintf(stderr, "server: signal %d\n", sig);
	backtrace_symbols_fd(frames, n, 2);
	_exit(1);
}

void gevrLauncherRun(void)
{
	const int port = sysArgGetInt("--port", GEVR_DEFAULT_PORT);
	const int seconds = sysArgGetInt("--seconds", 0);

	/* GEVR_FPE=1: stop at the first NaN or division by zero, with a backtrace
	 * (addr2line -f -e gevr-server <+offsets>). Found the libultra cosf alias. */
	if (getenv("GEVR_FPE")) {
		signal(SIGFPE, serverFpeHandler);
		feenableexcept(FE_INVALID | FE_DIVBYZERO);
	}
	/* The headset's first frame loads goldeneye-vr.ini (gevr_engine_shim.c gevrVrFrameBegin). */
	gevrVrPumpBegin();
	gevrVrPumpEnd();

	netSetDedicated(true);
	if (!netHostStart((uint16_t)port)) {
		sysFatalError("server: could not open UDP port %d", port);
	}
	netSetGameName("Dedicated server");
	netLobbySetCharacter((uint8_t)VrMpChr);
	{
		uint8_t items[4];
		for (int i = 0; i < 4; i++)
			items[i] = (uint8_t)(netItemIndexOf(VrMpLoadout[i]) >= 0 ? VrMpLoadout[i] : netItem(0)->item);
		netLobbySetLoadout(items);
	}
	{
		const NetMatchConfig c = netMatchConfigFromSettings();
		netLobbySetConfig(&c);
	}

	/* Launch, as the launcher's Launch button does. */
	if (!netLobbyHostLaunchMatch()) {
		sysFatalError("server: the match could not launch");
	}
	netApplyMatchConfig();
	if (netGetMatchConfig()->mode != NET_MODE_COOP) {
		bossSetLoadedStage(g_StageNum);
	}

	if (seconds > 0) {
		s_stopAtNs = serverNowNs() + seconds * 1000000000ll;
	}
	sysLogPrintf(LOG_NOTE, "server: hosting on UDP %d, stage %d, %d bots%s", port,
			netGetMatchConfig()->stage, netGetMatchConfig()->bot_mode ? netGetMatchConfig()->bot_count : 0,
			seconds > 0 ? " (timed run)" : "");
}

/*
 * Once a game frame (boss.c, after netPoll). Every 10 seconds: the game
 * frames run, the process's CPU use (100% = one core), the players, and each
 * player's kills and distance travelled (the game's own end-of-match stats).
 */
void gevrLobbyGameTick(void)
{
	static long long lastNs, warmupNs;
	static double lastCpu;
	static u32 lastFrames;
	const long long now = serverNowNs();

	/*
	 * P0: nobody presses Start Round, so the server does after 5 s of warmup,
	 * as the host's row in the pause menu does (mpmenu.c rowStartStep).
	 */
	if (netGetPhase() == NET_PHASE_WARMUP && netHostCanStartRound()) {
		if (warmupNs == 0) {
			warmupNs = now;
		} else if (warmupNs > 0 && now - warmupNs >= 5000000000ll) {
			sysLogPrintf(LOG_NOTE, "server: starting the round");
			netHostStartRoundNow();
			warmupNs = -1;
		}
	}

	if (lastNs == 0) {
		lastNs = now;
		lastCpu = serverCpuSeconds();
		lastFrames = gevrRetraceClockFrames;
	} else if (now - lastNs >= 10000000000ll) {
		const double cpu = serverCpuSeconds();
		const double wall = (now - lastNs) / 1e9;
		struct rusage ru;
		getrusage(RUSAGE_SELF, &ru);
		sysLogPrintf(LOG_NOTE, "server: %.1f game frames/s, CPU %.1f%% of a core, %d players, phase %d, rss %ld MB",
				(gevrRetraceClockFrames - lastFrames) / wall, 100.0 * (cpu - lastCpu) / wall,
				netGetConnectedPlayerCount(), (int)netGetPhase(), ru.ru_maxrss / 1024);
		for (int i = 0; i < netGetConnectedPlayerCount() && i < MAX_PLAYER_COUNT; i++) {
			const struct player *pl = g_playerPointers[i];
			s32 route = -1;   /* GEVR_NAVPROBE=1: a bot route from here to the next player (2: through glass) */

			if (getenv("GEVR_NAVPROBE") && pl && pl->prop && pl->prop->stan) {
				const struct player *o = g_playerPointers[(i + 1) % netGetConnectedPlayerCount()];
				GevrBotRoute r;

				if (o && o->prop && o->prop->stan) {
					route = gevrBotNavPlan(&r, pl->prop->stan, &pl->prop->pos, o->prop->stan, &o->prop->pos);
					route += route && r.glass;
				}
			}
			sysLogPrintf(LOG_NOTE, "server:   player %d: %d kills, %.0f travelled, at %.0f %.0f %.0f room %d%s; route %d",
					i, g_playerPlayerData[i].kill_count, g_playerPlayerData[i].distance_traveled,
					pl && pl->prop ? pl->prop->pos.x : 0.f, pl && pl->prop ? pl->prop->pos.y : 0.f,
					pl && pl->prop ? pl->prop->pos.z : 0.f, pl && pl->prop && pl->prop->stan ? pl->prop->stan->room : -1,
					pl && pl->bonddead ? ", dead" : "", route);
		}
		lastNs = now;
		lastCpu = cpu;
		lastFrames = gevrRetraceClockFrames;
	}

	if (s_stopAtNs && now >= s_stopAtNs) {
		sysLogPrintf(LOG_NOTE, "server: timed run over");
		exit(0);
	}
}

void gevrLobbySessionStopped(void) { }

/* The headset goes back to its launcher; the server ends, and its manager starts the next one. */
void gevrRestartToLauncher(void)
{
	sysLogPrintf(LOG_NOTE, "server: the session ended");
	exit(0);
}
