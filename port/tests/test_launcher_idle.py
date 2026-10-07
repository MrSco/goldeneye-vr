"""The host's launcher idle clock: a sleeping headset's time counts, other pages and matches do not."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
launcher = (ROOT / "port/vr/vr_launcher.cpp").read_text(encoding="utf-8")
start = launcher.index("static uint64_t s_launcherHostActMs = 0;")
helper = launcher.index("static bool gevrLauncherHostIdleTick(")
depth, end = 0, launcher.index("{", helper)
while True:
    depth += (launcher[end] == "{") - (launcher[end] == "}")
    end += 1
    if depth == 0:
        break
code = launcher[start:end]
assert "s_launcherFrame++;" in launcher, "the launcher loop counts its frames"
assert "s_launcherHostFrame = 0;" in launcher[launcher.index('extern "C" void gevrLobbyGameTick(void)'):], \
    "a match starts the launcher's idle clock again"
assert "gevrLauncherHostIdleTick(sysGetMicroseconds() / 1000, true, act)" in launcher, "the page uses the wall clock"

fixture = r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
/* CODE */
static const uint64_t MIN = 60ull * 1000;
/* One launcher frame with the multiplayer page on screen. */
static bool page(uint64_t now, bool hosting, bool act) { s_launcherFrame++; return gevrLauncherHostIdleTick(now, hosting, act); }
int main() {
    uint64_t t = 1000000;
    /* Hosting with nobody touching anything: ten minutes, then hosting stops. */
    assert(!page(t, true, false));
    assert(!page(t + 10 * MIN - 1, true, false));
    assert(page(t + 10 * MIN, true, false));
    /* Activity keeps it alive. */
    t += 20 * MIN;
    assert(!page(t, true, false));
    for (int i = 1; i <= 30; i++) assert(!page(t + i * MIN, true, true));
    t += 30 * MIN;
    /* A sleep: no frames for 20 minutes. Waking moves the pointer on the first
       frame back; the sleep still counts, and hosting stops. */
    assert(page(t + 20 * MIN, true, true));
    /* A short sleep keeps hosting, and the next activity restarts the clock. */
    t += 40 * MIN;
    assert(!page(t, true, false));
    assert(!page(t + 3 * MIN, true, true));
    assert(!page(t + 12 * MIN, true, false));
    assert(page(t + 13 * MIN, true, false));
    /* Another launcher page for an hour: the clock starts again on return. */
    t += 60 * MIN;
    assert(!page(t, true, false));
    s_launcherFrame += 5000;
    assert(!page(t + 60 * MIN, true, false));
    assert(!page(t + 69 * MIN, true, false));
    assert(page(t + 70 * MIN, true, false));
    /* A match (gevrLobbyGameTick) between two page frames: the same. */
    t += 200 * MIN;
    assert(!page(t, true, false));
    s_launcherHostFrame = 0;
    assert(!page(t + 45 * MIN, true, false));
    /* Not hosting: never stops, and the next hosting session starts fresh. */
    t += 100 * MIN;
    assert(!page(t, false, false));
    assert(!page(t + 30 * MIN, false, false));
    assert(!page(t + 31 * MIN, true, false));
    assert(!page(t + 40 * MIN, true, false));
    assert(page(t + 41 * MIN, true, false));
    std::puts("PASS: launcher host idle: wall clock, sleep counts, wake movement, other pages, matches, new sessions");
    return 0;
}
""".replace("/* CODE */", code)

with tempfile.TemporaryDirectory(prefix="gevr-launcher-idle-") as temp:
    source, exe = Path(temp) / "idle.cpp", Path(temp) / "idle.exe"
    source.write_text(fixture, encoding="utf-8")
    subprocess.run([shutil.which("g++") or "g++", "-std=c++17", "-O2", str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
