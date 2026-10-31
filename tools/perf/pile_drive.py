"""A pile of ragdolls in front of Bond, unattended, for body-pile performance.

Launches the installed build (the benchmark variant launches with the
controllers asleep), jumps to a level, warps Bond, then stands guards in front
of him and kills them as a blast would (gevr_cheat.txt spawn6 / kill<R>,
bondview2.c) until BODIES ragdolls exist, then turns full circles. Streams
logcat to OUT/logcat-TAG.txt; pile_summary.py reads it.

usage: python tools/perf/pile_drive.py OUT TAG
env: LEVEL (bunker2), WARP (gevr_warp.txt line, "guard 500"), BODIES (40),
     KILL (400), MINUTES (6), TURNS (3)
Pin WARP to an "at x y z deg" spot to compare builds on the same view, e.g.
Bunker II: WARP="at -3085 65 4452 347" (2026-10-30, Bean skin sharing A/B).
Uses the game's Bodies stay, ragdoll, dismemberment, Bean and Show stats
settings as they are; leaves the app stopped, its control files removed and
the headset asleep.
"""
import os
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import quest_profile as qp  # noqa: E402

out = Path(sys.argv[1]); tag = sys.argv[2]
out.mkdir(parents=True, exist_ok=True)
LEVEL = os.environ.get('LEVEL', 'bunker2')
WARP = os.environ.get('WARP', 'guard 500')
BODIES = int(os.environ.get('BODIES', '40'))
KILL = int(os.environ.get('KILL', '400'))
MINUTES = float(os.environ.get('MINUTES', '6'))
TURNS = int(os.environ.get('TURNS', '3'))
logpath = out / f'logcat-{tag}.txt'


def note(what):
    print(time.strftime('%H:%M:%S'), what, flush=True)


def bodies():
    """the highest ragdoll serial this run (ragdoll: body N, ...)"""
    text = logpath.read_text(errors='replace') if logpath.exists() else ''
    found = re.findall(r'ragdoll: body (\d+), ', text)
    return max(map(int, found)) if found else 0


def launch():
    qp.clear_stale_launch_checks()
    qp.adb('shell', 'am', 'start', '-n', qp.PACKAGE + '/.MainActivity')
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if qp.LAUNCH_CHECK in qp.resumed_activity():
            raise SystemExit('controllers-required launch dialog')
        qp.TARGET_PID = qp.adb('shell', 'pidof', qp.PACKAGE, check=False)
        if qp.TARGET_PID:
            break
        time.sleep(.5)
    else:
        raise SystemExit('app did not start')
    time.sleep(4)
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline and 'Session state changed: 5' not in qp.recent():
        qp.wake()
        time.sleep(1)
    qp.put('gevr_level.txt', LEVEL + ' 0')
    qp.press('1000', wait=4)
    for _ in range(16):
        if 'levelhook:' in qp.recent():
            break
        qp.press('8000', wait=2)
        qp.wake()
    else:
        raise SystemExit('level hook did not fire')
    note('levelhook fired')
    qp.press('1000', wait=8)
    for _ in range(6):
        qp.press('8000', wait=2)
        if re.search(r'intro-mode: \d+ -> 4', qp.recent()):
            break
    qp.put('gevr_cheat.txt', 'invincible allguns infammo')
    time.sleep(3)
    note('in level')


log = None
try:
    qp.adb('shell', 'am', 'force-stop', qp.PACKAGE)
    time.sleep(1)
    log = subprocess.Popen(['adb', 'logcat', '-v', 'threadtime'], stdout=open(logpath, 'wb'), stderr=subprocess.DEVNULL)
    qp.wake()
    launch()
    qp.put('gevr_warp.txt', WARP)
    time.sleep(3)
    note('warped: ' + WARP)
    end = time.monotonic() + MINUTES * 60
    last = -1
    while time.monotonic() < end:
        qp.wake()
        qp.put('gevr_cheat.txt', 'spawn6')
        time.sleep(1.5)
        qp.put('gevr_cheat.txt', f'kill{KILL}')
        time.sleep(2.5)
        n = bodies()
        if n != last:
            note(f'{n} ragdolls')
            last = n
        if n >= BODIES:
            break
    note(f'pile done: {bodies()} ragdolls')
    for t in range(TURNS):
        qp.wake()
        note(f'turn {t + 1}')
        qp.press('0', frames=1, turn=60, wait=9, ms=9000)
        time.sleep(1)
    note('done')
finally:
    for f in ['gevr_level.txt', 'gevr_input.txt', 'gevr_warp.txt', 'gevr_cheat.txt']:
        qp.adb('shell', 'rm', '-f', qp.FILES + '/' + f, check=False)
    qp.adb('shell', 'am', 'force-stop', qp.PACKAGE, check=False)
    time.sleep(1)
    if log:
        log.terminate()
    qp.adb('shell', 'input', 'keyevent', 'KEYCODE_SLEEP', check=False)
