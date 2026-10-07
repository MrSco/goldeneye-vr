"""The in-match idle kick's activity: input and movement count, the move's state flags do not."""
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_reinforcements import block

ROOT = Path(__file__).resolve().parents[2]
sync = (ROOT / "port/src/net/net_player_sync.c").read_text(encoding="utf-8")
helper = block(sync, "static bool netSyncLocalActivity(")
assert "if (netSyncLocalActivity(pl->speedforwards, pl->speedsideways, &move)) netTouchLocalActivity();" in sync

fixture = r"""
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "net_core.h"
#include "net_protocol.h"
/* HELPER */
int main(void) {
    struct netplayermove m;
    memset(&m, 0, sizeof(m));
    m.pos.x = 100.0f; m.angles[0] = 30.0f;
    assert(netSyncLocalActivity(0, 0, &m));          /* the first pose */
    /* A headset left in a match: still, aiming, crouched. Nobody is there. */
    m.ucmd = UCMD_AIMVALID | UCMD_AIMVALID_LEFT | UCMD_DUCK;
    for (int i = 0; i < 100; i++) assert(!netSyncLocalActivity(0, 0, &m));
    /* Tracking jitter under the thresholds is not activity. */
    m.angles[0] += 1.5f; m.pos.x += 0.03f;
    assert(!netSyncLocalActivity(0, 0, &m));
    /* Firing, either hand. */
    m.ucmd = UCMD_AIMVALID | UCMD_FIRE;
    assert(netSyncLocalActivity(0, 0, &m));
    m.ucmd = UCMD_FIRE_LEFT;
    assert(netSyncLocalActivity(0, 0, &m));
    m.ucmd = UCMD_AIMVALID;
    assert(!netSyncLocalActivity(0, 0, &m));
    /* Walking, strafing, looking, moving. */
    assert(netSyncLocalActivity(0.5f, 0, &m));
    assert(netSyncLocalActivity(0, -0.5f, &m));
    m.angles[1] += 5.0f;
    assert(netSyncLocalActivity(0, 0, &m));
    assert(!netSyncLocalActivity(0, 0, &m));
    m.pos.z += 1.0f;
    assert(netSyncLocalActivity(0, 0, &m));
    assert(!netSyncLocalActivity(0, 0, &m));
    return 0;
}
""".replace("/* HELPER */", helper)

with tempfile.TemporaryDirectory(prefix="gevr-idle-activity-") as temp:
    source, exe = Path(temp) / "activity.c", Path(temp) / "activity.exe"
    source.write_text(fixture, encoding="utf-8")
    args = [shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-fms-extensions", "-Wno-builtin-declaration-mismatch"]
    args += ["-D" + v for v in ("GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1", "VERSION=2", "VERSION_US=1", "LANG_US=1")]
    args += ["-I" + str(ROOT / v) for v in (".", "port/include", "include", "src", "src/game", "port/src", "port/src/net")]
    result = subprocess.run(args + [str(source), "-lm", "-o", str(exe)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    subprocess.run([str(exe)], check=True)
print("PASS: idle activity is input and movement, not the aim or crouch state")
