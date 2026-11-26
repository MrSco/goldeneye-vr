/*
 * One game per folder on PC (report f0743b24).
 *
 * GoldenEyeVR.exe and GoldenEyeVR-VR.exe sit in one folder and share
 * everything in it: goldeneye-vr.ini, the saves, gevr_prefs.txt (the crash
 * reporter's foreground marker), gevr.log and vr_debug.txt. Both open at once
 * took the flat game, still in its launcher, for a crash when the VR one got
 * past its wait for Link; the saves and settings of the two would overwrite
 * each other the same way.
 *
 * The run holds gevr_run.lock in its folder for as long as it lives: opened
 * without write sharing on Windows, flock'd on Linux. Both go with the process,
 * however it ends, so a crash or Task Manager leaves no stale lock. A second
 * start finds it held, brings the running game's window forward, says which
 * one is open if it is the other exe, and ends before it has touched a file.
 *
 * pcRestartSelf (the launcher's restart, an update) starts the new run before
 * the old one exits: it sets GEVR_RESTART_OF, and that run waits for the lock
 * instead of giving up. Folders are separate: the two-copy co-op test rig runs
 * each copy from its own.
 */

#if !defined(GEVR_IOS)   // an Apple app is one process already

#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#ifndef PC_INSTANCE_NO_UI   // port/tests/pc_services_native.cpp: the lock alone, no SDL
#include <SDL.h>
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#endif

namespace {

const char *const kInstanceLockFile = "gevr_run.lock";
const char *const kInstanceRestartEnv = "GEVR_RESTART_OF";

#ifdef _WIN32
HANDLE s_instanceLock = INVALID_HANDLE_VALUE;
#else
int s_instanceLock = -1;
#endif

std::string instanceExeName()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 4);
    std::wstring w(buf, n);
    const size_t slash = w.find_last_of(L"\\/");
    if (slash != std::wstring::npos) w = w.substr(slash + 1);
    char out[MAX_PATH * 4];
    const int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out, sizeof(out) - 1, nullptr, nullptr);
    return std::string(out, len > 0 ? (size_t)len : 0);
#else
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    std::string path = n > 0 ? std::string(buf, (size_t)n) : std::string("GoldenEyeVR");
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
#endif
}

int instancePid()
{
#ifdef _WIN32
    return (int)GetCurrentProcessId();
#else
    return (int)getpid();
#endif
}

}   // namespace

/* The lock, taken: true once this run holds it. Not held: *holderPid and
 * *holderExe say who does, as far as its file tells (0 and "" if unreadable). */
bool pcInstanceTryLock(const char *path, int *holderPid, std::string *holderExe)
{
    if (holderPid) *holderPid = 0;
    if (holderExe) holderExe->clear();
    const std::string info = std::to_string(instancePid()) + "\n" + instanceExeName() + "\n";
#ifdef _WIN32
    if (s_instanceLock != INVALID_HANDLE_VALUE) return true;
    // read sharing only: a second writer is refused while this handle lives
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        SetFilePointer(h, 0, nullptr, FILE_BEGIN);
        WriteFile(h, info.data(), (DWORD)info.size(), &written, nullptr);
        SetEndOfFile(h);
        FlushFileBuffers(h);
        s_instanceLock = h;
        return true;
    }
    if (GetLastError() != ERROR_SHARING_VIOLATION) return true;   // no lock to be had (read-only folder): run as before
    std::string text;
    HANDLE r = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (r != INVALID_HANDLE_VALUE) {
        char buf[512];
        DWORD got = 0;
        if (ReadFile(r, buf, sizeof(buf), &got, nullptr)) text.assign(buf, got);
        CloseHandle(r);
    }
#else
    if (s_instanceLock >= 0) return true;
    // O_CLOEXEC: pcRestartSelf's posix_spawn must not hand the new run this lock
    const int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return true;
    if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
        if (ftruncate(fd, 0) == 0 && pwrite(fd, info.data(), info.size(), 0) < 0) { /* the pid is only for the message */ }
        s_instanceLock = fd;
        return true;
    }
    if (errno != EWOULDBLOCK) {   // no lock to be had (a filesystem without flock): run as before
        close(fd);
        return true;
    }
    std::string text;
    char buf[512];
    const ssize_t got = pread(fd, buf, sizeof(buf), 0);
    if (got > 0) text.assign(buf, (size_t)got);
    close(fd);
#endif
    if (holderPid) *holderPid = atoi(text.c_str());
    const size_t nl = text.find('\n');
    if (holderExe && nl != std::string::npos) {
        const size_t end = text.find('\n', nl + 1);
        *holderExe = text.substr(nl + 1, end == std::string::npos ? std::string::npos : end - nl - 1);
    }
    return false;
}

/* Let it go (the tests; a run keeps its lock until it ends). */
void pcInstanceUnlock(void)
{
#ifdef _WIN32
    if (s_instanceLock != INVALID_HANDLE_VALUE) CloseHandle(s_instanceLock);
    s_instanceLock = INVALID_HANDLE_VALUE;
#else
    if (s_instanceLock >= 0) close(s_instanceLock);
    s_instanceLock = -1;
#endif
}

/* pcRestartSelf, before it starts the new run: that run waits for this one. */
void pcInstanceMarkRestart(void)
{
    const std::string pid = std::to_string(instancePid());
#ifdef _WIN32
    SetEnvironmentVariableA(kInstanceRestartEnv, pid.c_str());   // CreateProcessW's child inherits it
#else
    setenv(kInstanceRestartEnv, pid.c_str(), 1);
#endif
}


#ifndef PC_INSTANCE_NO_UI

#ifdef _WIN32
namespace {
struct WindowSearch { DWORD pid; HWND found; };
BOOL CALLBACK findWindowOf(HWND w, LPARAM p)
{
    WindowSearch *f = (WindowSearch *)p;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid == f->pid && IsWindowVisible(w) && GetWindow(w, GW_OWNER) == nullptr) {
        f->found = w;
        return FALSE;
    }
    return TRUE;
}
}   // namespace
#endif

extern "C" int sysArgCheck(const char *arg);   // s32 in port/include/system.h

namespace {
std::string lowerCase(std::string s)
{
    for (char &c : s) c = (char)tolower((unsigned char)c);
    return s;
}
}   // namespace

/* main.c, before anything is read or written: this folder's game, or none. */
extern "C" void pcSingleInstance(void)
{
    const char *restartEnv = getenv(kInstanceRestartEnv);
    const std::string restartOf = restartEnv ? restartEnv : "";   // a copy: unsetenv frees getenv's string
    const bool restarting = !restartOf.empty();
    if (restarting) {
        // this run's own restarts set it again; a later second start must not inherit the wait
#ifdef _WIN32
        SetEnvironmentVariableA(kInstanceRestartEnv, nullptr);
#else
        unsetenv(kInstanceRestartEnv);
#endif
    }
    // the old run of a restart can take a while to close (the session's shutdown,
    // the settings flush); a double click's twin starts within a moment
    const int waitMs = restarting ? 20000 : 1500;
    int holder = 0;
    std::string holderExe;
    for (int waited = 0;; waited += 100) {
        if (pcInstanceTryLock(kInstanceLockFile, &holder, &holderExe)) return;
        if (waited >= waitMs) break;
        SDL_Delay(100);
    }
    if (restarting) {
        // the old run cleared its marker and is only ending: a hung exit (an
        // OpenXR teardown) must not leave the player with no game, so start as
        // restarts did before the lock
        printf("pc: the run being restarted (pid %s) still holds the folder after %d s; starting anyway\n",
               restartOf.c_str(), waitMs / 1000);
        return;
    }

    const std::string me = instanceExeName();
    const bool otherExe = !holderExe.empty() && lowerCase(holderExe) != lowerCase(me);
    printf("pc: %s is already running from this folder (pid %d); this start ends\n",
           holderExe.empty() ? "GoldenEye VR" : holderExe.c_str(), holder);

    // a scripted run says nothing on the desktop and raises no window (the owner uses the PC meanwhile)
    const bool quiet = sysArgCheck("--hidden") || sysArgCheck("--input-script");
    bool focused = false;
#ifdef _WIN32
    if (!quiet && holder > 0) {
        WindowSearch f = { (DWORD)holder, nullptr };
        EnumWindows(findWindowOf, (LPARAM)&f);
        if (f.found) {
            if (IsIconic(f.found)) ShowWindow(f.found, SW_RESTORE);
            focused = SetForegroundWindow(f.found) != 0;
        }
    }
#endif
    if (!quiet && (otherExe || !focused)) {
        const bool vrOpen = lowerCase(holderExe).find("-vr") != std::string::npos;
        std::string text;
        if (otherExe) {
            text = std::string(vrOpen ? "The VR game" : "The flat game") + " (" + holderExe + ") is already open from this folder";
            text += vrOpen ? ", perhaps still waiting for the headset.\n\n" : ".\n\n";
            text += "Close it first: the two share the settings, saves and logs here, so only one can run at a time.";
        } else {
            text = "GoldenEye VR is already running from this folder";
            if (holder > 0) text += " (process " + std::to_string(holder) + ")";
#ifdef _WIN32
            text += ".\n\nClose it first, or end it in Task Manager if it no longer answers.";
#else
            text += ".\n\nClose it first, or end the process if it no longer answers.";
#endif
        }
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "GoldenEye VR", text.c_str(), nullptr);
    }
    exit(1);
}

#endif   // !PC_INSTANCE_NO_UI

#endif   // !GEVR_IOS
