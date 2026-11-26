/*
 * The Quest launcher (port/vr/vr_launcher.cpp) in the PC window
 * (docs/pc-platforms.md, P1).
 *
 * The launcher draws itself with ImGui into a texture and hands it to the
 * headset's screen quad (vr_screen_present_tex2d); here that texture goes into
 * the window. Its input on the Quest is the Touch pointer and sticks; here it
 * is the mouse (mapped from the window onto the launcher's canvas), the
 * keyboard (arrows, Return, Escape, Tab) and a pad. What the Quest asks of
 * Java goes through pc_services.cpp, and the file pickers are Windows' own.
 */

#include <string>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <SDL.h>

#include "../fast3d/glad/glad.h"
#include "../vr/imgui/imgui.h"
#include "pc_testinput.h"
#include "gevr_romimport.h"

#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#else
#include <filesystem>
#include <string>
#include <vector>
#include <spawn.h>      // pcRestartSelf
#include <sys/wait.h>   // pclose's status
extern char **environ;
#endif

extern SDL_Window *wnd;   // pc_window.cpp

extern "C" {
const char *fsFullPath(const char *relPath);   // port/src/fs.c
void sysLogPrintf(int level, const char *fmt, ...);
#ifdef GEVR_IOS
int gevrIosChooseFile(const char *title, char *out, int outLen);   // port/ios/ios_picker.m
#endif
}

#ifdef _WIN32
enum PickResult { PICK_CHOSEN, PICK_CANCELLED, PICK_FAILED };

/*
 * Windows' Open dialog: the file chosen, in *file. A test script
 * (pc_testinput.c "pick") answers instead, so a scripted run never leaves a
 * modal dialog on the desktop; with no answer waiting the pick is cancelled.
 */
static PickResult pcChooseFile(const wchar_t *filter, const wchar_t *title, std::wstring *file)
{
    if (pcTestInputActive()) {
        char scripted[400];
        if (!pcTestInputTakePick(scripted, (s32)sizeof(scripted)) || !strcmp(scripted, "-")) {
            sysLogPrintf(0, "launcher: scripted file picker: cancelled");
            return PICK_CANCELLED;
        }
        wchar_t w[400];
        MultiByteToWideChar(CP_UTF8, 0, scripted, -1, w, 400);
        *file = w;
        sysLogPrintf(0, "launcher: scripted file picker: %s", scripted);
        return PICK_CHOSEN;
    }
    wchar_t buf[MAX_PATH * 4] = L"";
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 4;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) {
        // a closed dialog is no error; one that could not open is (CommDlgExtendedError)
        return CommDlgExtendedError() ? PICK_FAILED : PICK_CANCELLED;
    }
    *file = buf;
    return PICK_CHOSEN;
}

static std::string pcNarrow(const std::wstring &w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

/*
 * The Open dialog taking several files (the main ROM picker, gevr_romimport.c).
 * A test script answers with the paths joined by '|'.
 */
static PickResult pcChooseFiles(const wchar_t *filter, const wchar_t *title, std::vector<std::wstring> *files)
{
    if (pcTestInputActive()) {
        std::wstring one;
        const PickResult r = pcChooseFile(filter, title, &one);
        if (r != PICK_CHOSEN) return r;
        size_t at = 0;
        for (size_t bar; (bar = one.find(L'|', at)) != std::wstring::npos; at = bar + 1) files->push_back(one.substr(at, bar - at));
        files->push_back(one.substr(at));
        return PICK_CHOSEN;
    }
    std::vector<wchar_t> buf(1 << 16, L'\0');
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) {
        return CommDlgExtendedError() ? PICK_FAILED : PICK_CANCELLED;
    }
    // one file: its path; several: the folder, then each name, each ending in a NUL
    const std::wstring first = buf.data();
    const wchar_t *p = buf.data() + first.size() + 1;
    if (!*p) {
        files->push_back(first);
        return PICK_CHOSEN;
    }
    for (; *p; p += wcslen(p) + 1) files->push_back(first + L"\\" + p);
    return PICK_CHOSEN;
}
#else
enum PickResult { PICK_CHOSEN, PICK_CANCELLED, PICK_FAILED };

static std::string pcShellQuote(const std::string &s)
{
    std::string q = "'";
    for (char c : s) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return q + "'";
}

/*
 * Linux: the desktop's own open dialog, zenity (GNOME and most others) or
 * kdialog (KDE), whichever is installed. patterns: "*.z64 *.v64 *.n64", or
 * "" for any file. A test script answers as on Windows.
 */
static PickResult pcChooseFile(const char *title, const char *patterns, std::string *file)
{
    if (pcTestInputActive()) {
        char scripted[400];
        if (!pcTestInputTakePick(scripted, (s32)sizeof(scripted)) || !strcmp(scripted, "-")) {
            sysLogPrintf(0, "launcher: scripted file picker: cancelled");
            return PICK_CANCELLED;
        }
        if (strcmp(scripted, "!") != 0) {
            *file = scripted;
            sysLogPrintf(0, "launcher: scripted file picker: %s", scripted);
            return PICK_CHOSEN;
        }
        // "pick !": the real picker (an Apple TV's upload dialog, port/tvos)
        sysLogPrintf(0, "launcher: scripted file picker: the real one");
    }
#ifdef GEVR_IOS
    {
        // iOS: the system's document picker (port/ios/ios_picker.m), any file
        char chosen[4096];
        const int r = gevrIosChooseFile(title, chosen, (int)sizeof(chosen));
        if (r > 0) *file = chosen;
        return r > 0 ? PICK_CHOSEN : r == 0 ? PICK_CANCELLED : PICK_FAILED;
    }
#endif
    const std::string zenity = "zenity --file-selection --title=" + pcShellQuote(title)
        + (patterns[0] ? " --file-filter=" + pcShellQuote(patterns) : std::string()) + " 2>/dev/null";
    const std::string kdialog = "kdialog --title " + pcShellQuote(title) + " --getopenfilename . "
        + pcShellQuote(patterns[0] ? patterns : "*") + " 2>/dev/null";
    for (const std::string &cmd : { zenity, kdialog }) {
        FILE *p = popen(cmd.c_str(), "r");
        if (!p) continue;
        char line[4096] = "";
        const bool read = fgets(line, sizeof(line), p) != nullptr;
        const int status = pclose(p);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 127) continue;   // not installed
        std::string chosen = read ? line : "";
        while (!chosen.empty() && (chosen.back() == '\n' || chosen.back() == '\r')) chosen.pop_back();
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && !chosen.empty()) {
            *file = chosen;
            return PICK_CHOSEN;
        }
        return PICK_CANCELLED;
    }
    sysLogPrintf(1, "launcher: no file dialog (install zenity or kdialog)");
    return PICK_FAILED;
}

/* Several files (the main ROM picker); a test script joins them with '|'. iOS: one. */
static PickResult pcChooseFiles(const char *title, const char *patterns, std::vector<std::string> *files)
{
    std::string joined;
    PickResult r;
#ifndef GEVR_IOS
    if (!pcTestInputActive()) {
        const std::string zenity = "zenity --file-selection --multiple --separator='|' --title=" + pcShellQuote(title)
            + (patterns[0] ? " --file-filter=" + pcShellQuote(patterns) : std::string()) + " 2>/dev/null";
        const std::string kdialog = "kdialog --title " + pcShellQuote(title) + " --multiple --separate-output --getopenfilename . "
            + pcShellQuote(patterns[0] ? patterns : "*") + " 2>/dev/null";
        r = PICK_FAILED;
        for (const std::string &cmd : { zenity, kdialog }) {
            FILE *p = popen(cmd.c_str(), "r");
            if (!p) continue;
            std::string out;
            char line[4096];
            while (fgets(line, sizeof(line), p)) out += line;
            const int status = pclose(p);
            if (WIFEXITED(status) && WEXITSTATUS(status) == 127) continue;   // not installed
            for (char &c : out) if (c == '\n') c = '|';
            while (!out.empty() && (out.back() == '|' || out.back() == '\r')) out.pop_back();
            r = WIFEXITED(status) && WEXITSTATUS(status) == 0 && !out.empty() ? PICK_CHOSEN : PICK_CANCELLED;
            joined = out;
            break;
        }
        if (r == PICK_FAILED) sysLogPrintf(1, "launcher: no file dialog (install zenity or kdialog)");
    } else
#endif
    {
        r = pcChooseFile(title, patterns, &joined);
    }
    if (r != PICK_CHOSEN) return r;
    size_t at = 0;
    for (size_t bar; (bar = joined.find('|', at)) != std::string::npos; at = bar + 1)
        if (bar > at) files->push_back(joined.substr(at, bar - at));
    if (at < joined.size()) files->push_back(joined.substr(at));
    return files->empty() ? PICK_CANCELLED : PICK_CHOSEN;
}
#endif

#ifndef _WIN32
static std::string pcSame(const std::string &s) { return s; }
#endif

// Several ROMs chosen at once: each copied into data/picked-import/ under its
// own name, where the launcher sorts them (gevr_romimport.c).
static std::string pcImportName(const std::string &path)
{
    const size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    if (name.empty() || name[0] == '.') name = "file" + name;
    if (name.size() >= 5 && name.compare(name.size() - 5, 5, ".part") == 0) name += "_";
    return name;
}

void pcModsImportOwn(const char *id, const std::string &path);   // pc_mods.cpp
void pcModsApplyCe(const std::string &path);

// The pick was only the XBLA copy: what the launcher says (MainActivity.pickResult).
static std::string s_pickNote;

std::string pcTakePickResult(void)
{
    std::string out;
    out.swap(s_pickNote);
    return out;
}

/*
 * The Play page's picker takes the XBLA copy too: the first one among the
 * files chosen goes to the Mods page's Bean import, read where it is, as its
 * own picker does (openXblaPicker); gevrRomImportIsXblaCopy tells it apart.
 */
#ifdef _WIN32
static bool pcIsXblaCopy(const std::wstring &path)
{
    FILE *f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    unsigned char head[6];
    const size_t n = fread(head, 1, sizeof(head), f);
    _fseeki64(f, 0, SEEK_END);
    const long long size = _ftelli64(f);
    fclose(f);
    return gevrRomImportIsXblaCopy(head, (unsigned long)n, size < 0 ? 0 : (unsigned long long)size);
}
#else
static bool pcIsXblaCopy(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char head[6];
    const size_t n = fread(head, 1, sizeof(head), f);
    fseeko(f, 0, SEEK_END);
    const long long size = (long long)ftello(f);
    fclose(f);
    return gevrRomImportIsXblaCopy(head, (unsigned long)n, size < 0 ? 0 : (unsigned long long)size);
}
#endif

template <typename Path>
static void pcTakeXblaCopy(std::vector<Path> &files, std::string (*narrow)(const Path &))
{
    for (size_t i = 0; i < files.size(); i++) {
        if (!pcIsXblaCopy(files[i])) continue;
        sysLogPrintf(0, "launcher: the main picker was given the XBLA copy; importing Bean");
        pcModsImportOwn("ge007-bean", narrow(files[i]));   // the Play page shows how far it is
        if (files.size() == 1) s_pickNote = "Importing Bean from your GoldenEye XBLA copy.";
        files.erase(files.begin() + (long)i);
        return;
    }
}

/*
 * The Quest's file pickers (MainActivity.openRomPicker and friends) copy the
 * chosen file to data/picked*.z64, which the launcher adopts. Here Windows'
 * Open dialog does the same, at once.
 *
 * The Mods page's XBLA / Bean (MainActivity.openXblaPicker / openCePicker):
 * nothing is copied; the chosen file goes to the Mods page's import
 * (ModManager.importOwn / applyCe, pc_mods.cpp), whose progress the Bean row
 * shows. As on the Quest, true means the picker opened, chosen or not.
 */
bool pcOpenPicker(const char *method)
{
#ifdef _WIN32
    const bool xbla = !strcmp(method, "openXblaPicker"), ce = !strcmp(method, "openCePicker");
    if (xbla || ce) {
        std::wstring file;
        const PickResult r = xbla
            ? pcChooseFile(L"GoldenEye XBLA (.7z, .zip or the Xbox 360 package)\0*.*\0", L"Choose your copy of GoldenEye XBLA", &file)
            : pcChooseFile(L"Community Edition updater (CommunityEditionUpdaterV6.zip)\0*.zip\0All files\0*.*\0",
                           L"Choose the GoldenEye XBLA Community Edition updater", &file);
        if (r == PICK_CHOSEN) {
            sysLogPrintf(0, "launcher: %s: a file chosen", xbla ? "XBLA copy" : "Community Edition");
            if (xbla) pcModsImportOwn("ge007-bean", pcNarrow(file));
            else pcModsApplyCe(pcNarrow(file));
        }
        return r != PICK_FAILED;
    }
#else
    const bool xbla = !strcmp(method, "openXblaPicker"), ce = !strcmp(method, "openCePicker");
    if (xbla || ce) {
        std::string file;
        const PickResult r = xbla
            ? pcChooseFile("Choose your copy of GoldenEye XBLA", "", &file)
            : pcChooseFile("Choose the GoldenEye XBLA Community Edition updater", "*.zip", &file);
        if (r == PICK_CHOSEN) {
            sysLogPrintf(0, "launcher: %s: a file chosen", xbla ? "XBLA copy" : "Community Edition");
            if (xbla) pcModsImportOwn("ge007-bean", file);
            else pcModsApplyCe(file);
        }
        return r != PICK_FAILED;
    }
#endif
    const char *dest = !strcmp(method, "openRomPicker") ? "picked.z64"
                     : !strcmp(method, "openGexPicker") ? "picked-gex.z64"
                     : !strcmp(method, "openGoldfingerPicker") ? "picked-goldfinger.z64"
                     : !strcmp(method, "openTndPicker") ? "picked-tnd.z64"
                     : !strcmp(method, "openMirrorPicker") ? "picked-mirror.z64"
                     : !strcmp(method, "openMarioPicker") ? "picked-mario.z64" : nullptr;
    if (!dest) return false;
#ifdef _WIN32
    std::wstring file;
    if (!strcmp(dest, "picked.z64")) {
        // the main picker: one ROM, several, or a .zip of them
        std::vector<std::wstring> files;
        if (pcChooseFiles(L"ROMs, a .zip of them, or the XBLA copy\0*.z64;*.v64;*.n64;*.zip;*.7z\0All files (the Xbox 360 package has no extension)\0*.*\0",
                          L"Choose the ROM (or several, or a .zip of them)", &files) != PICK_CHOSEN) {
            return false;
        }
        pcTakeXblaCopy<std::wstring>(files, pcNarrow);
        if (files.empty()) return true;   // only the XBLA copy: s_pickNote ends the pick
        if (files.size() > 1) {
            const std::string dir = std::string(fsFullPath(GEVR_ROM_IMPORT_DIR));
            wchar_t wdir[MAX_PATH * 2];
            MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, wdir, MAX_PATH * 2);
            CreateDirectoryW(wdir, nullptr);
            int copied = 0;
            for (const std::wstring &f : files) {
                const std::string to = dir + "/" + pcImportName(pcNarrow(f));
                wchar_t wto[MAX_PATH * 2];
                MultiByteToWideChar(CP_UTF8, 0, to.c_str(), -1, wto, MAX_PATH * 2);
                copied += CopyFileW(f.c_str(), wto, FALSE) != 0;
            }
            sysLogPrintf(copied ? 0 : 1, "launcher: picked %d files, %d copied for sorting", (int)files.size(), copied);
            return copied > 0;
        }
        file = files[0];
    } else if (pcChooseFile(L"N64 ROMs (*.z64;*.v64;*.n64)\0*.z64;*.v64;*.n64\0All files\0*.*\0", L"Choose the ROM", &file) != PICK_CHOSEN) {
        return false;
    }
    char to[MAX_PATH * 2];
    snprintf(to, sizeof(to), "%s", fsFullPath(dest));
    wchar_t wto[MAX_PATH * 2];
    MultiByteToWideChar(CP_UTF8, 0, to, -1, wto, MAX_PATH * 2);
    const bool ok = CopyFileW(file.c_str(), wto, FALSE) != 0;
    sysLogPrintf(ok ? 0 : 1, "launcher: picked a file for %s (%s)", dest, ok ? "copied" : "copy failed");
    return ok;
#else
    std::string file;
    if (!strcmp(dest, "picked.z64")) {
        // the main picker: one ROM, several, or a .zip of them
        std::vector<std::string> files;
        if (pcChooseFiles("Choose the ROM (or several, or a .zip of them)", "", &files) != PICK_CHOSEN) {
            return false;
        }
        pcTakeXblaCopy<std::string>(files, pcSame);
        if (files.empty()) return true;   // only the XBLA copy: s_pickNote ends the pick
        if (files.size() > 1) {
            const std::filesystem::path dir = fsFullPath(GEVR_ROM_IMPORT_DIR);
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            int copied = 0;
            for (const std::string &f : files) {
                ec.clear();
                std::filesystem::copy_file(f, dir / pcImportName(f), std::filesystem::copy_options::overwrite_existing, ec);
                copied += !ec;
            }
            sysLogPrintf(copied ? 0 : 1, "launcher: picked %d files, %d copied for sorting", (int)files.size(), copied);
            return copied > 0;
        }
        file = files[0];
    } else if (pcChooseFile("Choose the ROM", "*.z64 *.v64 *.n64", &file) != PICK_CHOSEN) {
        return false;
    }
    std::error_code ec;
    std::filesystem::copy_file(file, fsFullPath(dest), std::filesystem::copy_options::overwrite_existing, ec);
    sysLogPrintf(ec ? 1 : 0, "launcher: picked a file for %s (%s)", dest, ec ? "copy failed" : "copied");
    return !ec;
#endif
}

void pcReportCleanExit(void);         // pc_reports.cpp
extern "C" void gevrLogClose(void);   // port/src/system.c

/* The Quest restarts its activity into the launcher; the PC runs itself again. */
void pcRestartSelf(void)
{
    pcReportCleanExit();   // before the new run looks for this one's marker (MainActivity.restartToLauncher)
    gevrLogClose();        // and before it rotates gevr.log to gevr.prev.log
#ifdef _WIN32
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (CreateProcessW(nullptr, GetCommandLineW(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
#else
    // the same command line again (/proc/self/cmdline), started before this one exits
    std::string cmdline;
    if (FILE *f = fopen("/proc/self/cmdline", "rb")) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) cmdline.append(buf, n);
        fclose(f);
    }
    std::vector<char *> argv;
    for (size_t i = 0; i < cmdline.size(); i += strlen(&cmdline[i]) + 1) argv.push_back(&cmdline[i]);
    argv.push_back(nullptr);
    pid_t pid;
#ifndef GEVR_IOS   // an iOS app cannot start itself: it closes, and the player opens it again
    // The exe by its path, not /proc/self/exe: after an update that is the
    // old file, moved to NAME.old and deleted, and it started the old version
    // again (Linux update test, 2026-11-09)
    char self[4096];
    const ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    std::string path = n > 0 ? std::string(self, (size_t)n) : std::string("/proc/self/exe");
    const std::string deleted = " (deleted)";
    if (path.size() > deleted.size() && path.compare(path.size() - deleted.size(), deleted.size(), deleted) == 0)
        path.resize(path.size() - deleted.size());
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".old") == 0) path.resize(path.size() - 4);
    if (argv.size() > 1) posix_spawn(&pid, path.c_str(), nullptr, nullptr, argv.data(), environ);
#endif
#endif
    exit(0);
}

void pcQuitApp(void)
{
    pcReportCleanExit();
    gevrLogClose();
    exit(0);
}
