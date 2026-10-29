/*
 * The dedicated server in the internet lobby (docs/dedicated-server.md, P2).
 *
 * On a headset the launcher (vr_launcher.cpp gevrMultiplayerPage) and the
 * Java LobbyClient trade one-line messages: the launcher sends "create",
 * "refresh", "phase" and "answer" commands, and LobbyClient, which talks HTTPS
 * to lobbies.goldeneyevr.com, sends back CREATED, LOBBY_LOST, HOST_PEER and
 * ERROR events. Here the same lines go over two pipes to
 * tools/gevr_lobby_bridge.py, so the server needs no HTTPS of its own:
 *
 *   gevr-server --lobby-fd IN,OUT [--lobby-public]
 *
 * Joiners reach the server through ICE (net_ice.cpp), as they reach a headset
 * host: their offer comes in as HOST_PEER, the answer goes back.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <PR/ultratypes.h>

#include "system.h"
#include "net_core.h"
#include "net_ice.h"
#include "net_protocol.h"

#define LOBBY_MAX_PEERS 16
#define LOBBY_SDP_MAX 4096

static int s_in = -1, s_out = -1;
static bool s_public;
static char s_name[33];
static char s_buf[16384];
static size_t s_len;
static char s_peers[LOBBY_MAX_PEERS][64];   /* join ids whose answers may still come */
static int s_peerCount;
static char s_code[16];
static long long s_nextRefreshMs;
static int s_lastPhase = -1;

static long long lobbyNowMs(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* the launcher's gevrEncodeUrl64: URL-safe base64, no padding */
static void lobbyEncode(const char *in, char *out, size_t cap)
{
	static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	const unsigned char *p = (const unsigned char *)in;
	const size_t n = strlen(in);
	size_t o = 0;

	for (size_t i = 0; i < n && o + 5 < cap; i += 3) {
		unsigned v = (unsigned)p[i] << 16;
		if (i + 1 < n) v |= (unsigned)p[i + 1] << 8;
		if (i + 2 < n) v |= p[i + 2];
		out[o++] = alphabet[(v >> 18) & 63];
		out[o++] = alphabet[(v >> 12) & 63];
		if (i + 1 < n) out[o++] = alphabet[(v >> 6) & 63];
		if (i + 2 < n) out[o++] = alphabet[v & 63];
	}
	out[o] = '\0';
}

static int lobbyDigit(char c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '-' || c == '+') return 62;
	if (c == '_' || c == '/') return 63;
	return -1;
}

static void lobbyDecode(const char *in, char *out, size_t cap)
{
	unsigned v = 0;
	int bits = 0;
	size_t o = 0;

	for (; *in && o + 1 < cap; in++) {
		const int d = lobbyDigit(*in);
		if (d < 0) continue;
		v = (v << 6) | (unsigned)d;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out[o++] = (char)((v >> bits) & 0xff);
		}
	}
	out[o] = '\0';
}

static void lobbyCommand(const char *line)
{
	size_t n = strlen(line);
	if (s_out < 0) return;
	/* a pipe write of a few kilobytes is atomic and the bridge reads at once */
	if (write(s_out, line, n) < 0 || write(s_out, "\n", 1) < 0) {
		sysLogPrintf(LOG_WARNING, "lobby: the bridge is gone (%s)", strerror(errno));
		close(s_out);
		s_out = -1;
	}
}

static void lobbyCreate(void)
{
	char line[160];

	snprintf(line, sizeof(line), "create|%s|%s|%d|%d|%d|%d", s_public ? "public" : "private", s_name,
			GEVR_NET_VERSION, netGetLobbyStage(), netGetLobbyWeaponSet(), netGetMaxPlayers());
	lobbyCommand(line);
	s_nextRefreshMs = 0;
	s_lastPhase = -1;
}

bool serverLobbyStart(const char *fds, const char *name, bool visibleToAll)
{
	if (fds == NULL || sscanf(fds, "%d,%d", &s_in, &s_out) != 2 || s_in < 0 || s_out < 0) {
		s_in = s_out = -1;
		return false;
	}
	fcntl(s_in, F_SETFL, fcntl(s_in, F_GETFL) | O_NONBLOCK);
	s_public = visibleToAll;
	snprintf(s_name, sizeof(s_name), "%s", name);
	netIceStartHost();
	lobbyCreate();
	sysLogPrintf(LOG_NOTE, "lobby: registering a %s lobby through the bridge", s_public ? "public" : "private");
	return true;
}

static void lobbyEvent(char *line)
{
	char *f[8] = { 0 };
	int n = 0;

	for (char *p = line; n < 8; ) {
		f[n++] = p;
		p = strchr(p, '|');
		if (!p) break;
		*p++ = '\0';
	}
	if (!strcmp(f[0], "CREATED") && n >= 3) {
		snprintf(s_code, sizeof(s_code), "%s", f[1]);
		s_lastPhase = -1;
		/* the clients keep the code and token: one they elect host if this
		 * server goes takes the lobby over (net_core.c netHostLost) */
		netSetLobbyHandoff(f[1], f[2]);
		sysLogPrintf(LOG_NOTE, "lobby: online as %s (%s)", f[1], s_public ? "public" : "private, join by code");
	} else if (!strcmp(f[0], "LOBBY_LOST") && n >= 2) {
		sysLogPrintf(LOG_NOTE, "lobby: %s lost (%s); registering again", f[1], n >= 3 ? f[2] : "");
		s_code[0] = '\0';
		s_peerCount = 0;
		lobbyCreate();
	} else if (!strcmp(f[0], "HOST_PEER") && n >= 5) {
		static char offer[LOBBY_SDP_MAX];

		lobbyDecode(f[2], offer, sizeof(offer));
		if (netIcePeerCount() < netIceMaxPeers() && s_peerCount < LOBBY_MAX_PEERS
		    && netIceAddHostPeer(f[1], offer, f[3], f[4])) {
			snprintf(s_peers[s_peerCount++], sizeof(s_peers[0]), "%s", f[1]);
			sysLogPrintf(LOG_NOTE, "lobby: a player is joining (%s)", f[1]);
		}
	} else if (!strcmp(f[0], "ERROR") && n >= 2) {
		sysLogPrintf(LOG_WARNING, "lobby: %s", f[1]);
	}
}

/* Once a game frame: the bridge's events in, answers and the state out. */
void serverLobbyTick(void)
{
	if (s_in < 0) return;

	for (;;) {
		ssize_t got = read(s_in, s_buf + s_len, sizeof(s_buf) - 1 - s_len);
		if (got <= 0) break;
		s_len += (size_t)got;
	}
	for (char *nl; (nl = memchr(s_buf, '\n', s_len)) != NULL; ) {
		*nl = '\0';
		lobbyEvent(s_buf);
		s_len -= (size_t)(nl + 1 - s_buf);
		memmove(s_buf, nl + 1, s_len);
	}
	if (s_len == sizeof(s_buf) - 1) s_len = 0;   /* a line too long for the buffer: dropped */

	for (int i = 0; i < s_peerCount; ) {
		static char sdp[LOBBY_SDP_MAX], line[LOBBY_SDP_MAX * 2];
		static char enc[LOBBY_SDP_MAX * 2];
		bool done = true;

		if (netIceTakeDescription(s_peers[i], sdp, sizeof(sdp))) {
			lobbyEncode(sdp, enc, sizeof(enc));
			snprintf(line, sizeof(line), "answer|%s|%s", s_peers[i], enc);
			lobbyCommand(line);
		} else if (!netIceStatus(s_peers[i])) {
			done = false;   /* still gathering; a status means it failed */
		}
		if (done) memmove(s_peers[i], s_peers[i + 1], sizeof(s_peers[0]) * (size_t)(--s_peerCount - i));
		else i++;
	}
	netIcePoll();

	/*
	 * As the launcher does in a match (gevrLobbyGameTick): the phase when it
	 * changes, otherwise every 5 s the live players (the server's own seat and
	 * the connected people, never bots) and whether there is room. The lobby
	 * service wants two for a match in progress, so a round the players have
	 * left reads as warmup.
	 */
	if (s_code[0]) {
		const int players = netGetLivePlayerCount();
		const int max = netGetMaxPlayers();
		const int phase = netGetPhase() == NET_PHASE_IN_PROGRESS && players >= 2 ? 2 : 1;
		char line[96];

		if (phase != s_lastPhase) {
			snprintf(line, sizeof(line), "phase|%s|%d", phase == 2 ? "in_progress" : "warmup", players);
			lobbyCommand(line);
			s_lastPhase = phase;
			s_nextRefreshMs = lobbyNowMs() + 5000;
		} else if (lobbyNowMs() >= s_nextRefreshMs) {
			snprintf(line, sizeof(line), "refresh|%d|%d|%d", players, players < max ? 1 : 0, max);
			lobbyCommand(line);
			s_nextRefreshMs = lobbyNowMs() + 5000;
		}
	}
}

void serverLobbyStop(void)
{
	if (s_out >= 0) lobbyCommand("stop");
}
