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
 * page pc_mods.cpp, crash and log reports pc_reports.cpp, friends and invites
 * pc_friends.cpp and the update check pc_updater.cpp (which the launcher
 * reaches through gevrUpdaterStatus / gevrUpdaterCommand).
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
std::string pcReportStatus(void);            // pc_reports.cpp: LogReporter
void pcReportCommand(const char *cmd);
std::string pcFriendsEvent(void);            // pc_friends.cpp: FriendsClient
void pcFriendsCommand(const char *cmd);
std::string pcUpdaterStatus(void);           // pc_updater.cpp: UpdateChecker
void pcUpdaterCommand(const char *cmd);

std::string pcServiceString(const char *method)
{
    if (!strcmp(method, "lobbyEvent")) {
        return pcLobbyEvent();
    }
    if (!strcmp(method, "modsStatus")) {
        return pcModsStatus();
    }
    if (!strcmp(method, "reportStatus")) {
        return pcReportStatus();
    }
    if (!strcmp(method, "friendsEvent")) {
        return pcFriendsEvent();
    }
    if (!strcmp(method, "updaterStatus")) {
        return pcUpdaterStatus();
    }
    return std::string();
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
    if (!strcmp(method, "updaterCommand")) {
        pcUpdaterCommand(arg);
        return;
    }
    if (!strcmp(method, "friendsCommand")) {
        pcFriendsCommand(arg);
        return;
    }
    if (!strcmp(method, "reportCommand")) {
        pcReportCommand(arg);   // not logged: it holds the player's note and name
        return;
    }
    if (!strcmp(method, "requestVoicePermission")) {
        // a desktop's microphone needs no runtime permission
        netVoicePermissionResult(1);
        return;
    }
    sysLogPrintf(0, "services: %s is not on PC yet (%s)", method, arg);
}
