/*
 * The PC's answers to what the Quest asks of Java (docs/pc-platforms.md P3,
 * docs/mobile-platforms.md M2).
 *
 * On the Quest the launcher and the friends list talk to MainActivity in one
 * line messages: a method name and a string each way (gevrJavaString,
 * gevrJavaCommand; vr_friends.cpp javaString, javaCommand). Behind them are
 * LobbyClient (HTTPS to lobbies.goldeneyevr.com), ModManager, LogReporter and
 * the friends service. The PC keeps the same messages and answers them here,
 * so the launcher's pages are the Quest's. The native versions of those
 * services (P3) share pc_http.h: the internet lobby is pc_lobby.cpp, the Mods
 * page pc_mods.cpp; the rest report that they aren't on PC yet.
 */

#include <string>
#include <cstring>

extern "C" {
void sysLogPrintf(int level, const char *fmt, ...);
void netVoicePermissionResult(int granted);   // net_voice.c
}

std::string pcLobbyEvent(void);              // pc_lobby.cpp: LobbyClient, ported
void pcLobbyCommand(const char *cmd);
std::string pcModsStatus(void);              // pc_mods.cpp: ModManager
void pcModsCommand(const char *cmd);

std::string pcServiceString(const char *method)
{
    if (!strcmp(method, "lobbyEvent")) {
        return pcLobbyEvent();
    }
    if (!strcmp(method, "modsStatus")) {
        return pcModsStatus();
    }
    return std::string();   // reportStatus, friends*: nothing pending
}

void pcServiceCommand(const char *method, const char *arg)
{
    if (!strcmp(method, "lobbyCommand")) {
        pcLobbyCommand(arg);
        return;
    }
    if (!strcmp(method, "modsCommand")) {
        pcModsCommand(arg);
        return;
    }
    if (!strcmp(method, "requestVoicePermission")) {
        // a desktop's microphone needs no runtime permission
        netVoicePermissionResult(1);
        return;
    }
    sysLogPrintf(0, "services: %s is not on PC yet (%s)", method, !strcmp(method, "reportCommand") ? "..." : arg);
}
