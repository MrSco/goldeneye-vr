/*
 * The PC's answers to what the Quest asks of Java (docs/pc-platforms.md P3,
 * docs/mobile-platforms.md M2).
 *
 * On the Quest the launcher and the friends list talk to MainActivity in one
 * line messages: a method name and a string each way (gevrJavaString,
 * gevrJavaCommand; vr_friends.cpp javaString, javaCommand). Behind them are
 * LobbyClient (HTTPS to lobbies.goldeneyevr.com), ModManager, LogReporter and
 * the friends service. The PC keeps the same messages and answers them here,
 * so the launcher's pages are the Quest's. Native versions of those services
 * land here one by one (P3); until then each reports that it isn't on PC yet,
 * and LAN and direct-address play, which need no Java, work as on the Quest.
 */

#include <string>
#include <cstring>

extern "C" {
void sysLogPrintf(int level, const char *fmt, ...);
void netVoicePermissionResult(int granted);   // net_voice.c
}

std::string pcServiceString(const char *method)
{
    (void)method;   // modsStatus (no packs), reportStatus, lobbyEvent, friends*: nothing pending
    return std::string();
}

void pcServiceCommand(const char *method, const char *arg)
{
    if (!strcmp(method, "requestVoicePermission")) {
        // a desktop's microphone needs no runtime permission
        netVoicePermissionResult(1);
        return;
    }
    sysLogPrintf(1, "services: %s is not on PC yet (%s)", method, !strcmp(method, "reportCommand") ? "..." : arg);
}
