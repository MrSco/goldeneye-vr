/*
 * The Windows game's console (CMakeLists.txt links the exes -mwindows).
 *
 * A console exe opened a console window beside the game, and closing that
 * window ended the game with no exit: the crash-report marker stayed, and the
 * next start offered a crash report for a player who had only closed a window
 * (reports #27 and #28, v0.5.17, 2026-11-14). The game logs to gevr.log, so it
 * needs no console: the exes are Windows programs now.
 *
 * Started from a terminal, the game still writes there (it attaches to the
 * terminal's console). stdout already sent elsewhere (a pipe, a file) is left
 * as it is. A terminal closed under the game, or the session ending, is a
 * clean end for the reports, as closing the game's window is.
 */
#ifdef _WIN32
#include <windows.h>
#include <stdio.h>

void pcReportForeground(int active);   /* port/pc/pc_reports.cpp */

static BOOL WINAPI pcConsoleEnds(DWORD type)
{
    if (type == CTRL_CLOSE_EVENT || type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        pcReportForeground(0);
    }
    return FALSE;   /* Windows' own handling (the end of the process) follows */
}

void pcConsoleInit(void)
{
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    const int redirected = out && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN;
    if (!redirected && AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
    SetConsoleCtrlHandler(pcConsoleEnds, TRUE);
}
#endif
