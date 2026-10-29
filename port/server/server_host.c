/*
 * The dedicated server's launcher (docs/dedicated-server.md).
 *
 * On a headset, port/vr/vr_launcher.cpp runs before the ROM loads: the
 * player picks a game and presses Start Hosting, then Launch. Here the same
 * calls are made from goldeneye-vr.ini with nobody pressing anything:
 *
 *   gevr-server [--basedir DIR] [--port N] [--name NAME] [--beacon-to IP] [--seconds N] [--bots-only]
 *               [--lobby-fd IN,OUT [--lobby-public]]
 *   gevr-server --connect HOST[:PORT] [--seconds N]
 *
 * --lobby-fd is for tools/gevr_lobby_bridge.py, which starts the server and
 * puts it in the internet lobby (server_lobby.c).
 *
 * The data folder holds ge.z64 and goldeneye-vr.ini, as on the headset. The
 * match is the ini's Host page: stage, scenario, weapons, bots and so on.
 *
 * The server's own seat is a spectator for good (net_core.c netDedicatedSeat).
 * A round starts 5 s after a player has loaded in, or at once with
 * --bots-only (test runs with nobody to join).
 *
 * --connect joins a server instead, as a player who stands still: a headless
 * client for testing joins, late joins and rounds from this machine.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include "net_discovery.h"
#include "game/player.h"
#include "game/gevr_bot.h"

extern int VrMpChr, VrMpLoadout[4];
extern s32 g_StageNum;
extern u32 gevrRetraceClockFrames;   /* gevr_engine_shim.c: game frames handed out */
int gevrVrPumpBegin(void);
void gevrVrPumpEnd(void);
bool serverLobbyStart(const char *fds, const char *name, bool visibleToAll);   /* server_lobby.c */
void serverLobbyTick(void);
void serverLobbyStop(void);

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

/* SIGTERM (systemctl stop, kill) or SIGINT (Ctrl+C): stop at the next game frame */
static volatile sig_atomic_t s_stopSignal;

static void serverStopHandler(int sig)
{
	s_stopSignal = sig;
}

static void serverFpeHandler(int sig)
{
	void *frames[48];
	const int n = backtrace(frames, 48);
	fprintf(stderr, "server: signal %d\n", sig);
	backtrace_symbols_fd(frames, n, 2);
	_exit(1);
}

/*
 * --connect: what the launcher does for a player who joins (vr_launcher.cpp
 * gevrMultiplayerPage): connect, wait in the lobby until the host's match is
 * under way, then load it with the host's config.
 */
static void serverJoin(const char *target, int port)
{
	char host[128];
	const char *colon;
	long long deadline = serverNowNs() + 30000000000ll;

	if (target == NULL || !*target) {
		sysFatalError("client: --connect needs HOST or HOST:PORT");
	}
	snprintf(host, sizeof(host), "%s", target);
	colon = strrchr(host, ':');
	if (colon) {
		port = atoi(colon + 1);
		host[colon - host] = '\0';
	}
	netLobbySetCharacter((uint8_t)VrMpChr);
	if (!netConnect(host, (uint16_t)port)) {
		sysFatalError("client: could not connect to %s:%d", host, port);
	}
	sysLogPrintf(LOG_NOTE, "client: joining %s:%d", host, port);
	while (netIsActive() && netGetState() != NET_STATE_INGAME) {
		struct timespec ts = { 0, 10000000 };

		netPoll();
		nanosleep(&ts, NULL);
		if (serverNowNs() > deadline) {
			sysFatalError("client: no match from %s:%d within 30 s (state %d)", host, port, (int)netGetState());
		}
	}
	if (!netIsActive()) {
		sysFatalError("client: the server turned us away");
	}
	netApplyMatchConfig();
	if (netGetMatchConfig()->mode != NET_MODE_COOP) {
		bossSetLoadedStage(g_StageNum);
	}
	netLobbySetReady(true);   /* as a player ticks Ready: the host's round start waits for it */
	sysLogPrintf(LOG_NOTE, "client: in the match as slot %d, stage %d", netGetLocalSlot(), netGetMatchConfig()->stage);
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

	if (seconds > 0) {
		s_stopAtNs = serverNowNs() + seconds * 1000000000ll;
	}
	if (sysArgCheck("--connect")) {
		serverJoin(sysArgGetString("--connect"), port);
		return;
	}

	netSetDedicated(true);
	if (!netHostStart((uint16_t)port)) {
		sysFatalError("server: could not open UDP port %d", port);
	}
	{
		/* the game's name in the LAN and internet lists (15 characters at most) */
		const char *name = sysArgCheck("--name") ? sysArgGetString("--name") : "GEVR Server";

		netSetGameName(name);
		/* on the LAN as a headset host is (vr_launcher.cpp Start Hosting); a
		 * server in WSL2 also sends each beacon to Windows (--beacon-to), for
		 * tools/gevr_udp_relay.py to put on the LAN */
		netDiscoveryInit();
		netDiscoveryStartBroadcasting(name, (uint16_t)port);
		if (sysArgCheck("--beacon-to")) {
			netDiscoveryAlsoSendTo(sysArgGetString("--beacon-to"));
		}
	}
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

	/* the internet lobby, through tools/gevr_lobby_bridge.py (server_lobby.c) */
	if (sysArgCheck("--lobby-fd")) {
		if (!serverLobbyStart(sysArgGetString("--lobby-fd"), netGetGameName(), sysArgCheck("--lobby-public"))) {
			sysFatalError("server: --lobby-fd wants IN,OUT (the bridge's pipes)");
		}
		atexit(serverLobbyStop);
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
	static long long lastNs, warmupNs, emptyNs;
	static double lastCpu;
	static u32 lastFrames;
	const long long now = serverNowNs();
	const bool botsOnly = sysArgCheck("--bots-only");

	/*
	 * Nobody presses Start Round on a server, so it does, as the host's row
	 * in the pause menu does (mpmenu.c rowStartStep): 10 s into a warmup with
	 * a player in it (or --bots-only), players playing meanwhile. The host's
	 * own checks then wait for everyone to load and the teams to be whole.
	 */
	if (netIsDedicated() && netGetPhase() == NET_PHASE_WARMUP && netHostCanStartRound() && !netHostStartRequested()
	    && (netGetHumanPlayerCount() > 0 || botsOnly)) {
		if (warmupNs == 0) {
			warmupNs = now;
		} else if (now - warmupNs >= 10000000000ll) {
			sysLogPrintf(LOG_NOTE, "server: starting the round (%d players, %d connected)",
					netGetHumanPlayerCount(), netGetConnectedPlayerCount());
			netHostStartRoundNow();
			warmupNs = 0;
		}
	} else {
		warmupNs = 0;
	}

	/*
	 * The last player gone, a round of bots alone goes back to warmup: the
	 * next to join plays at once instead of spectating it to its end (a
	 * headset that joined a bots' round was carried round as a spectator,
	 * 2026-10-29).
	 */
	if (netIsDedicated() && !botsOnly && netGetPhase() == NET_PHASE_IN_PROGRESS && netGetHumanPlayerCount() == 0) {
		if (emptyNs == 0) {
			emptyNs = now;
		} else if (now - emptyNs >= 3000000000ll) {
			sysLogPrintf(LOG_NOTE, "server: nobody left in the round: back to warmup");
			netHostRestartWarmup();
			emptyNs = 0;
		}
	} else {
		emptyNs = 0;
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

	serverLobbyTick();
	if (s_stopAtNs && now >= s_stopAtNs) {
		sysLogPrintf(LOG_NOTE, "server: timed run over");
		exit(0);
	}
	{
		/* from the first game frame: something in the start-up after the
		 * launcher (not SDL, told to leave signals alone) sets them back */
		static bool handlers;

		if (!handlers) {
			handlers = true;
			signal(SIGTERM, serverStopHandler);
			signal(SIGINT, serverStopHandler);
		}
	}
	if (s_stopSignal) {
		sysLogPrintf(LOG_NOTE, "server: stopping (signal %d)", (int)s_stopSignal);
		netDisconnect();   /* players see the host leave at once, not after a timeout */
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
