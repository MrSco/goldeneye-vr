/*
 * Crash and debug-log reports on PC (docs/pc-platforms.md P3): a port of the
 * Quest's android/app/src/main/java/com/gevr/port/LogReporter.java and
 * CrashExitPolicy.java.
 *
 * The launcher (port/vr/vr_launcher.cpp gevrReportPage) reads reportStatus
 * ("idle", "offer" after a crash, "sending", "sent", "error:why") and sends
 * reportCommand ("offered", "send|note64|player64|build") through
 * pc_services.cpp, as it reaches MainActivity on the Quest.
 *
 * What Android gives LogReporter and the PC has to make for itself:
 * - Exit records (ApplicationExitInfo): Windows keeps none for us. crash.c's
 *   Windows handler (now built on PC) and sysFatalError write gevr_crash.txt
 *   beside the exe (pcCrashRecord): the time, the exception and its address in
 *   the exe (for the release's .debug file), and the stack walk. It stands in
 *   for the exit record and its tombstone (crash.getTraceInputStream).
 * - The foreground marker (SharedPreferences debug_reports): gevr_prefs.txt.
 *   Kept while the game runs, cleared on a clean exit (atexit) and before a
 *   restart; one left behind is a run that ended without either (killed, hung
 *   and ended from Task Manager, a crash the handler could not record).
 * - The install time (PackageInfo.lastUpdateTime): the exe's last write time;
 *   an update replaces the exe.
 * - Boot detection: the boot time (now - GetTickCount64, which counts sleep as
 *   the Quest's elapsedRealtime does); Windows has no boot count, so Lepton's
 *   bootCountChanged has no PC counterpart.
 * - The game log: gevr.log and gevr.prev.log beside the exe, rotated at start
 *   as on the Quest (port/src/system.c). logcat's place in the report is taken
 *   by the tail of vr_debug.txt (port/vr/vr_log.c, the launcher's log).
 *
 * The report goes to lobbies.goldeneyevr.com/v1/reports, or GEVR_REPORTS_URL
 * (tests point it at a local mock: a real report becomes a GitHub issue).
 */

#include <string>
#include <mutex>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#endif

#include "pc_http.h"
#include "../vr/miniz/miniz.h"

extern "C" void sysLogPrintf(int level, const char *fmt, ...);

using namespace pcnet;

namespace {

const char *const kEndpoint = "https://lobbies.goldeneyevr.com/v1/reports";
const char *const kCrashFile = "gevr_crash.txt";
const size_t kMaxLog = 2500000;
const size_t kMaxTrace = 1000000;

/* ---- CrashExitPolicy, for the PC's records ---- */

/* A crash of THIS install that has not been offered yet (CrashExitPolicy.qualifies). */
bool qualifies(long long exitAt, long long installAt, long long offeredAt)
{
    return exitAt > installAt && exitAt > offeredAt;
}

/* CrashExitPolicy.unexpectedExit: the marker stands in for missing records. */
bool unexpectedExit(long long markerAt, long long installAt, long long offeredAt, long long lastExitAt, long long bootAt)
{
    return markerAt > installAt && markerAt > offeredAt && lastExitAt < markerAt && markerAt >= bootAt;
}

/* One record as it appears in the log and the report's exit history (CrashExitPolicy.describe). */
std::string describe(long long exitAt, const std::string &summary, long long installAt)
{
    return isoUtc(exitAt) + " crash " + summary + (exitAt <= installAt ? " [before install]" : "");
}

/* ---- LogReporter.redact: no network addresses or signalling in a report ---- */

bool isDigit(char c) { return c >= '0' && c <= '9'; }

/* (?<![0-9.])(?!127\.)\d{1,3}(?:\.\d{1,3}){3}(?![0-9.]) -> [redacted address] */
std::string redactAddresses(const std::string &s)
{
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        if (!isDigit(s[i]) || (i > 0 && (isDigit(s[i - 1]) || s[i - 1] == '.'))) {
            out.push_back(s[i++]);
            continue;
        }
        size_t p = i;
        bool match = true;
        size_t firstLen = 0;
        for (int group = 0; group < 4 && match; group++) {
            size_t start = p;
            while (p < s.size() && isDigit(s[p])) p++;
            const size_t len = p - start;
            if (group == 0) firstLen = len;
            if (len < 1 || len > 3) { match = false; break; }
            if (group < 3) {
                if (p < s.size() && s[p] == '.') p++;
                else match = false;
            } else if (p < s.size() && s[p] == '.') {
                match = false;
            }
        }
        if (match && firstLen == 3 && s.compare(i, 4, "127.") == 0) match = false;
        if (match) {
            out += "[redacted address]";
            i = p;
        } else {
            // not an address starting here: the whole run of digits stays
            while (i < s.size() && isDigit(s[i])) out.push_back(s[i++]);
        }
    }
    return out;
}

/* (?im)^(.*(?:lobbyCommand|launcher: lobbyCommand).*(?:offer|answer)\|).*$ -> $1[redacted signaling] */
std::string redactSignalling(const std::string &s)
{
    std::string out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find('\n', start);
        const bool last = end == std::string::npos;
        if (last) end = s.size();
        size_t lineEnd = end;
        // Java's '.' and '$' stop before any line terminator, '\r' included
        const size_t cr = s.find('\r', start);
        if (cr != std::string::npos && cr < lineEnd) lineEnd = cr;
        std::string line = s.substr(start, lineEnd - start);
        std::string lower = line;   // (?i)
        for (char &c : lower) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        const size_t cmd = lower.find("lobbycommand");
        if (cmd != std::string::npos) {
            const size_t afterCmd = cmd + 12;
            const size_t o = lower.rfind("offer|"), a = lower.rfind("answer|");
            size_t keep = std::string::npos;
            if (o != std::string::npos && o >= afterCmd) keep = o + 6;
            if (a != std::string::npos && a >= afterCmd && (keep == std::string::npos || a + 7 > keep)) keep = a + 7;
            if (keep != std::string::npos) line = line.substr(0, keep) + "[redacted signaling]";
        }
        out += line + s.substr(lineEnd, end - lineEnd);
        if (last) break;
        out += '\n';
        start = end + 1;
    }
    return out;
}

std::string redact(const std::string &value) { return redactAddresses(redactSignalling(value)); }

/* GZIPOutputStream: a gzip member around miniz's raw deflate. */
std::string gzip(const std::string &plain)
{
    size_t rawLen = 0;
    void *raw = tdefl_compress_mem_to_heap(plain.data(), plain.size(), &rawLen,
                                           (int)tdefl_create_comp_flags_from_zip_params(6, -MZ_DEFAULT_WINDOW_BITS, MZ_DEFAULT_STRATEGY));
    std::string out("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\xff", 10);
    if (raw) out.append((const char *)raw, rawLen);
    else out.append("\x03\x00", 2);   // an empty final block
    mz_free(raw);
    const unsigned crc = (unsigned)mz_crc32(MZ_CRC32_INIT, (const unsigned char *)plain.data(), plain.size());
    const unsigned size = (unsigned)plain.size();
    for (int i = 0; i < 4; i++) out.push_back((char)((crc >> (8 * i)) & 0xff));
    for (int i = 0; i < 4; i++) out.push_back((char)((size >> (8 * i)) & 0xff));
    return out;
}

/* The exe, for the install time. */
std::string exePath()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 4);
    return n ? narrow(std::wstring(buf, n)) : std::string("GoldenEyeVR.exe");
#else
    return "GoldenEyeVR";
#endif
}

long long bootTimeMs(long long *elapsed)
{
#ifdef _WIN32
    const long long up = (long long)GetTickCount64();
#else
    const long long up = nowMs();
#endif
    if (elapsed) *elapsed = up;
    return wallMs() - up;
}

int myPid()
{
#ifdef _WIN32
    return (int)GetCurrentProcessId();
#else
    return 0;
#endif
}

/* Build.MANUFACTURER + " " + Build.MODEL's place: the PC and its Windows. */
std::string device()
{
#ifdef _WIN32
    typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW *);
    OSVERSIONINFOW v;
    memset(&v, 0, sizeof(v));
    v.dwOSVersionInfoSize = sizeof(v);
    RtlGetVersionFn get = (RtlGetVersionFn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    if (get) get(&v);
    char buf[96];
    snprintf(buf, sizeof(buf), "PC Windows %lu.%lu.%lu x64", (unsigned long)v.dwMajorVersion,
             (unsigned long)v.dwMinorVersion, (unsigned long)v.dwBuildNumber);
    return buf;
#else
    return "PC";
#endif
}

struct CrashRecord {
    long long at = 0;
    std::string summary, trace;
};

/* gevr_crash.txt: "<ms since 1970> <summary>", then the trace. */
bool readCrashRecord(CrashRecord *c)
{
    std::string text;
    if (!readFile(kCrashFile, &text, kMaxTrace + 4096)) return false;
    const size_t nl = text.find('\n');
    std::string first = text.substr(0, nl);
    if (!first.empty() && first.back() == '\r') first.pop_back();
    char *end = nullptr;
    c->at = strtoll(first.c_str(), &end, 10);
    if (c->at <= 0) return false;
    c->summary = end && *end == ' ' ? std::string(end + 1) : std::string();
    c->trace = nl == std::string::npos ? std::string() : text.substr(nl + 1);
    if (c->trace.size() > kMaxTrace) c->trace.resize(kMaxTrace);
    return true;
}

class LogReporter {
public:
    LogReporter()
    {
        installTime_ = fileTimeMs(exePath());
        const long long offered = prefGetInt("debug_reports.offered_crash", 0);
        long long lastExit = 0;
        sysLogPrintf(0, "reports: install %s version %s%s", isoUtc(installTime_).c_str(), appVersion(),
                     offered ? (", last offered crash " + isoUtc(offered)).c_str() : "");
        CrashRecord record;
        if (readCrashRecord(&record)) {
            const std::string line = describe(record.at, record.summary, installTime_);
            sysLogPrintf(0, "reports: previous crash: %s", line.c_str());
            exitHistory_ += line + "\n";
            lastExit = record.at;
            if (qualifies(record.at, installTime_, offered)) {
                crash_ = record;
                hasCrash_ = true;
                crashTimestamp_ = record.at;
                state_ = "offer";
            }
        }
        // The marker stands in for the exit records Windows doesn't keep for us. A
        // marker older than the install is the update replacing a running game; one
        // followed by a crash record already has its reason; one from before this
        // boot ended with the PC shutting down or restarting, not a game crash.
        const long long previousRun = prefGetInt("debug_reports.foreground_run", 0);
        long long currentElapsed = 0;
        const long long bootAt = bootTimeMs(&currentElapsed);
        const long long previousBoot = prefGetInt("debug_reports.foreground_boot", 0);
        const long long previousElapsed = prefGetInt("debug_reports.foreground_elapsed", 0);
        const bool rebooted = previousRun < bootAt
            || (previousBoot != 0 && llabs(bootAt - previousBoot) > 5000)
            || (previousElapsed != 0 && currentElapsed < previousElapsed);
        if (previousRun) {
            exitHistory_ += isoUtc(previousRun) + " foreground run without a clean exit" + (rebooted ? " (before a restart of the PC)" : "") + "\n";
        }
        if (!hasCrash_ && !rebooted && unexpectedExit(previousRun, installTime_, offered, lastExit, bootAt)
            && prefGetInt("debug_reports.run_pid", 0) != myPid()) {
            crashTimestamp_ = previousRun;
            unexpectedExit_ = true;
            state_ = "offer";
            sysLogPrintf(0, "reports: the run from %s ended unexpectedly", isoUtc(previousRun).c_str());
        }
        foreground(true);
    }

    void foreground(bool active)
    {
        long long elapsed = 0;
        const long long boot = bootTimeMs(&elapsed);
        prefSetInt("debug_reports.foreground_run", active ? wallMs() : 0);
        prefSetInt("debug_reports.foreground_boot", active ? boot : 0);
        prefSetInt("debug_reports.foreground_elapsed", active ? elapsed : 0);
        prefSetInt("debug_reports.run_pid", myPid());
    }

    std::string status()
    {
        std::lock_guard<std::mutex> lock(lock_);
        return state_;
    }

    void command(const std::string &command)
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (command == "offered") {
            if (crashTimestamp_ != 0) prefSetInt("debug_reports.offered_crash", crashTimestamp_);
            if (state_ == "offer") state_ = "idle";
            return;
        }
        if (command.compare(0, 5, "send|") != 0) return;
        if (state_ == "sending") return;
        // split("\\|", 4): the build is the rest
        const size_t a = command.find('|', 5);
        const size_t b = a == std::string::npos ? a : command.find('|', a + 1);
        if (b == std::string::npos) { state_ = "error:Invalid report"; return; }
        const std::string note = redact(decode64(command.substr(5, a - 5)));
        const std::string player = redact(decode64(command.substr(a + 1, b - a - 1)));
        const std::string build = command.substr(b + 1);
        if (note.size() > 500 || player.size() > 64 || build.size() > 64) {
            state_ = "error:Report details too long";
            return;
        }
        const bool withCrash = hasCrash_ || unexpectedExit_;
        state_ = "sending";
        worker_.post([this, withCrash, note, player, build] { send(withCrash, note, player, build); });
    }

private:
    void setState(const std::string &s)
    {
        std::lock_guard<std::mutex> lock(lock_);
        state_ = s;
    }

    void send(bool withCrash, const std::string &note, const std::string &player, const std::string &build)
    {
        try {
            std::string text;
            const char *ini = isFile("data/goldeneye-vr.ini") ? "data/goldeneye-vr.ini" : "goldeneye-vr.ini";
            if (isFile(ini)) text += "=== goldeneye-vr.ini ===\n" + tailFile(ini, 50000) + "\n";
            /* Gun fit's gadget poses (bondview2.c gevrGadgetFitEnd): "item left up fwd rx ry rz scale fist" */
            if (isFile("gevr_itempose.txt")) text += "=== gevr_itempose.txt ===\n" + tailFile("gevr_itempose.txt", 20000) + "\n";
            CrashRecord crash;
            bool hasCrash;
            bool unexpected;
            long long crashAt;
            {
                std::lock_guard<std::mutex> lock(lock_);
                crash = crash_;
                hasCrash = hasCrash_;
                unexpected = unexpectedExit_;
                crashAt = crashTimestamp_;
            }
            text += "=== exit history ===\ninstall " + isoUtc(installTime_) + " version " + appVersion() + "\n"
                + (exitHistory_.empty() ? std::string("(no records)\n") : exitHistory_) + "\n";
            text += "=== previous game run ===\n" + tailFile("gevr.prev.log", 1200000);
            text += "\n=== current game run ===\n" + tailFile("gevr.log", 800000);
            text += "\n=== vr_debug.txt ===\n" + tailFile("vr_debug.txt", 600000);   // the Quest's app logcat
            std::string plain = redact(text);
            if (plain.size() > kMaxLog) plain = plain.substr(plain.size() - kMaxLog);
            const std::string compressed = gzip(plain);
            const std::string trace = withCrash && hasCrash ? crash.trace : std::string();
            std::string crashSummary = withCrash && hasCrash ? redact(describe(crash.at, crash.summary, installTime_))
                : unexpected ? "Previous foreground run from " + isoUtc(crashAt) + " ended unexpectedly" : "";
            if (crashSummary.size() > 500) crashSummary.resize(500);
            const std::string body = std::string("{\"kind\":") + q(withCrash ? "crash" : "manual")
                + ",\"version\":" + q(appVersion()) + ",\"build\":" + q(build)
                + ",\"device\":" + q(device())
                + ",\"player\":" + q(player) + ",\"note\":" + q(note)
                + ",\"crash_summary\":" + q(crashSummary)
                + ",\"log_gz_b64\":" + q(encodeBase64(compressed))
                + ",\"tombstone_b64\":" + q(encodeBase64(trace)) + "}";
            HttpRequest r;
            r.method = "POST";
            r.url = envOr("GEVR_REPORTS_URL", kEndpoint);
            r.body = &body;
            r.connectMs = 10000;
            r.readMs = 20000;
            r.headers.push_back("Content-Type: application/json");
            const HttpReply reply = httpSend(r);
            if (reply.status < 200 || reply.status >= 300) throw HttpError{reply.status, "Server returned HTTP " + std::to_string(reply.status)};
            sysLogPrintf(0, "reports: sent a %s report (%zu bytes of log)", withCrash ? "crash" : "manual", plain.size());
            std::lock_guard<std::mutex> lock(lock_);
            state_ = "sent";
            hasCrash_ = false;
        } catch (const HttpError &e) {
            sysLogPrintf(1, "reports: report failed: %s", e.message.c_str());
            setState("error:" + e.message);
        } catch (...) {
            setState("error:Report failed");
        }
    }

    std::mutex lock_;
    std::string state_ = "idle";
    long long crashTimestamp_ = 0;
    CrashRecord crash_;
    bool hasCrash_ = false;
    bool unexpectedExit_ = false;
    /** When this exe was written (installed or updated); crashes before it belong to another build. */
    long long installTime_ = 0;
    /** The records as logged at startup, for the report's exit history section. */
    std::string exitHistory_;
    SerialWorker worker_;   // last
};

LogReporter *s_reporter;
std::atomic<bool> s_recorded{false};

void reporterAtExit(void)
{
    // a clean exit: the marker goes (MainActivity.onDestroy's reporter.foreground(false))
    if (s_reporter) s_reporter->foreground(false);
}

LogReporter &reporter()
{
    if (!s_reporter) {
        s_reporter = new LogReporter();   // lives as long as the process, as the Quest's
        atexit(reporterAtExit);
    }
    return *s_reporter;
}

}   // namespace

std::string pcReportStatus(void) { return reporter().status(); }
void pcReportCommand(const char *cmd) { reporter().command(cmd ? cmd : ""); }

/* Before the game ends itself to start again (pcRestartSelf, the updater): the
 * run ended cleanly, as MainActivity.restartToLauncher's foreground(false). */
void pcReportCleanExit(void)
{
    if (s_reporter) s_reporter->foreground(false);
}

extern "C" {

/* At start (crash.c crashInit): read the last run's records before this run's marker. */
void pcReportStartup(void)
{
    reporter();
}

/* A crash or a fatal error (crash.c's handler, sysFatalError): gevr_crash.txt.
 * The first record of a run wins: the handler's has the trace, the fatal
 * error it ends in does not. Plain C stdio: the process is going down. */
void pcCrashRecord(const char *summary, const char *trace)
{
    if (s_recorded.exchange(true)) return;
    FILE *f = fopen(kCrashFile, "wb");
    if (!f) return;
    std::string one = summary ? summary : "";
    for (char &c : one) if (c == '\n' || c == '\r') c = ' ';
    fprintf(f, "%lld %s\n%s", wallMs(), one.c_str(), trace ? trace : "");
    fclose(f);
}

}
