"""The PC game's crash handler on Linux writes gevr_crash.txt and still dies.

v0.5.27: a Linux player crashed several times in a match full of proximity
mines and throwing knives, and no report reached the reports repo. crash.c's
Linux handler sat under PLATFORM_LINUX, which the build never defines, so
the Linux game had no handler at all: no FATAL line, no gevr_crash.txt, no
trace, only the foreground marker's "ended unexpectedly" (if sent).

This builds port/src/crash.c as the Linux PC game does (GEVR_PC on Linux)
with the real pcCrashRecord from port/pc/pc_reports.cpp, and crashes it four
ways in child processes:

- a null write (SIGSEGV): the record, a backtrace with this binary's
  offsets, and the process still ends by SIGSEGV;
- a double free, glibc's own abort on a damaged heap (SIGABRT), where
  anything that calls malloc in the handler can hang: it must finish;
- a fault inside the renderer's context log (gevrGfxCrashContext): the
  record still comes, the process still ends;
- a stack overflow: the handler runs on its own stack.

Linux only (the Windows handler is SEH); elsewhere it says so and passes.
"""
from pathlib import Path
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


FIXTURE = r"""
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

void crashInit(void);
static int s_logfd = -1;

void sysLogPrintf(int level, const char *fmt, ...)
{
    char line[16384];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    (void)level;
    if (n < 0) return;
    if (n > (int)sizeof(line) - 2) n = (int)sizeof(line) - 2;
    line[n++] = '\n';
    if (s_logfd >= 0) { ssize_t w = write(s_logfd, line, (size_t)n); (void)w; }
}

void sysFatalError(const char *fmt, ...) { (void)fmt; abort(); }
void pcReportStartup(void) { }

/* gfx_pc.cpp's context log: faults when the test asks it to */
int gevrGfxCrashContext(const void *fault)
{
    (void)fault;
    if (getenv("FAULT_IN_CONTEXT")) { volatile int *volatile p = (volatile int *)16; *p = 1; }
    return 0;
}

__attribute__((noinline)) static int deep(int n)
{
    volatile char pad[512];
    pad[0] = (char)n;
    return deep(n + 1) + pad[0];
}

__attribute__((noinline)) void crashTestNullWrite(void)
{
    volatile int *volatile p = NULL;   /* a store the compiler must make */
    *p = 42;
}

int main(int argc, char **argv)
{
    s_logfd = 1;   /* the game log: stdout */
    crashInit();
    const char *mode = argc > 1 ? argv[1] : "segv";
    if (!strcmp(mode, "segv")) {
        crashTestNullWrite();
    } else if (!strcmp(mode, "doublefree")) {
        char *volatile a = malloc(64);
        free(a);
        free(a);
    } else if (!strcmp(mode, "overflow")) {
        return deep(0);
    }
    return 3;   /* not reached */
}
"""

RECORD = r"""
#include <atomic>
#include <string>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
namespace {
const char *const kCrashFile = "gevr_crash.txt";
std::atomic<bool> s_recorded{false};
long long wallMs() { return 1764000000000LL; }
FILE *openFile(const std::string &path, const char *mode) { return fopen(path.c_str(), mode); }
}
extern "C" {
/* RECORD_FUNCTION */
}
"""


def main():
    if not sys.platform.startswith("linux"):
        print("test_crash_linux: skipped (the Linux handler; run it in WSL or on Linux)")
        return
    cc = shutil.which("clang") or shutil.which("gcc")
    cxx = shutil.which("clang++") or shutil.which("g++")
    assert cc and cxx, "no C/C++ compiler"
    reports = (ROOT / "port/pc/pc_reports.cpp").read_text(encoding="utf-8")
    record = function(reports, "void pcCrashRecord(const char *summary, const char *trace)")
    posix = record.split("#endif")[0]
    assert "write(fd" in posix and not any(word in posix for word in ("std::string", "fopen", "openFile")), \
        "the Linux record path must not allocate"
    crash = (ROOT / "port/src/crash.c").read_text(encoding="utf-8")
    assert "defined(GEVR_PC) && defined(__linux__)" in crash, "the handler must build for the Linux PC game"

    with tempfile.TemporaryDirectory(prefix="gevr-crash-linux-") as temp:
        temp = Path(temp)
        (temp / "sdl").mkdir()
        (temp / "sdl" / "SDL.h").write_text("/* crash.c needs nothing of SDL */\n", encoding="utf-8")
        (temp / "fixture.c").write_text(FIXTURE, encoding="utf-8")
        (temp / "record.cpp").write_text(RECORD.replace("/* RECORD_FUNCTION */", record), encoding="utf-8")
        exe = temp / "crashtest"
        common = ["-O1", "-g", "-fno-omit-frame-pointer", "-DGEVR_PC=1", "-D_GNU_SOURCE", "-D_LANGUAGE_C=1",
                  "-I" + str(temp / "sdl"), "-I" + str(ROOT / "include"), "-I" + str(ROOT / "port/include")]
        objs = []
        for src, compiler, extra in ((ROOT / "port/src/crash.c", cc, ["-std=gnu11"]),
                                      (temp / "fixture.c", cc, ["-std=gnu11"]),
                                      (temp / "record.cpp", cxx, ["-std=c++17"])):
            obj = temp / (src.stem + ".o")
            result = subprocess.run([compiler, *common, *extra, "-c", str(src), "-o", str(obj)],
                                    capture_output=True, text=True)
            assert result.returncode == 0, result.stderr
            objs.append(str(obj))
        result = subprocess.run([cxx, *objs, "-rdynamic", "-ldl", "-o", str(exe)], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr

        cases = (("segv", {}, signal.SIGSEGV),
                 ("doublefree", {}, signal.SIGABRT),
                 ("segv", {"FAULT_IN_CONTEXT": "1"}, signal.SIGSEGV),
                 ("overflow", {}, signal.SIGSEGV))
        for mode, env, expected in cases:
            work = temp / ("run-" + mode + ("-ctx" if env else ""))
            work.mkdir()
            run = subprocess.run([str(exe), mode], cwd=work, env={**os.environ, **env},
                                 capture_output=True, text=True, timeout=20)
            label = mode + (" with a fault in the context log" if env else "")
            assert run.returncode == -expected, "%s: ended with %d, not signal %d\n%s%s" % (
                label, run.returncode, expected, run.stdout, run.stderr)
            record_file = work / "gevr_crash.txt"
            assert record_file.exists(), "%s: no gevr_crash.txt\n%s" % (label, run.stdout)
            text = record_file.read_text(encoding="utf-8", errors="replace")
            first, _, trace = text.partition("\n")
            assert re.match(r"^1764000000000 SIGNAL %d at 0x[0-9a-f]+ \(module 0x[0-9a-f]+ \+ 0x[0-9a-f]+\)" % expected,
                            first), "%s: record line %r" % (label, first)
            assert "BACKTRACE:" in trace and "crashtest+0x" in trace, "%s: trace\n%s" % (label, trace)
            if mode == "segv" and not env:
                assert "crashTestNullWrite" in trace, "the faulting function by name\n" + trace
            assert "FATAL: Crashed" in run.stdout, "%s: no FATAL line in the log\n%s" % (label, run.stdout)
            print("  ok   %s: signal %d, record %r, %d trace lines" % (
                label, expected, first[:60], trace.count("\n")))
    print("test_crash_linux: passed")


if __name__ == "__main__":
    main()
