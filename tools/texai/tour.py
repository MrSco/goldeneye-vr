#!/usr/bin/env python3
"""
Headset texture tour: every level, every pad, with files/gevr_packdump on, so
the texture dump (gfx_pc.cpp) records each texture the installed pack lacks.

    python tools/texai/tour.py --fork <fork_dir> [--levels facility,runway,...]
                               [--from-pad N] [--first-in-level] [--stride 1] [--turn 1]

The game must be running on the headset (adb), at mode select or later, with a
build that has the input hook's turn field (4907a80+). It drives the PC test
hooks: gevr_level.txt jumps to a mission, START starts it, gevr_cheat.txt makes
Bond invincible, gevr_warp.txt stands him on each pad in turn, and the turn
field spins the stereo view where a warp found a real gap (a texture the fork
lacks) and every eighth stand. A level is left through the watch's abort.
  - Pads in an exit zone end the mission (Surface 2's pads 2-9): skipped with
    a re-entry, further each time in a run of them.
  - A warp before play starts (intro mode 4) also ended Surface 2.
  - prox_close is resent every 40 warps (it lapsed once and the headset slept).
About 10 minutes a level; all 20 found ~380 real gaps on 2026-09-27. Then pull
files/texture-dump (index.tsv first) and run batch.py on it.
"""
import argparse
import os
import queue
import re
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import batch  # noqa: E402

FILES = '/sdcard/Android/data/com.gevr.port/files'
ORDER = ['facility', 'runway', 'surface', 'bunker', 'silo', 'frigate', 'surface2', 'bunker2', 'statue',
         'archives', 'streets', 'depot', 'train', 'jungle', 'control', 'caverns', 'cradle', 'aztec', 'egypt',
         'dam']   # Dam last: the user's own play dumped it already


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def sh(cmd):
    return subprocess.run(['adb', 'shell', cmd], capture_output=True, text=True).stdout


def put(name, text):
    sh("echo '%s' > %s/%s; chmod 666 %s/%s" % (text, FILES, name, FILES, name))


class Logcat:
    def __init__(self):
        subprocess.run(['adb', 'logcat', '-c'])
        self.p = subprocess.Popen(['adb', 'logcat', '-v', 'brief', '-s', 'GoldenEye:*'], stdout=subprocess.PIPE,
                                  text=True, encoding='utf-8', errors='replace')
        self.q = queue.Queue()
        threading.Thread(target=self._pump, daemon=True).start()

    def _pump(self):
        for line in self.p.stdout:
            self.q.put(line.rstrip())

    def drain(self):
        out = []
        while True:
            try:
                out.append(self.q.get_nowait())
            except queue.Empty:
                return out

    def wait(self, pattern, timeout):
        rx = re.compile(pattern)
        end = time.time() + timeout
        while time.time() < end:
            try:
                line = self.q.get(timeout=max(0.05, end - time.time()))
            except queue.Empty:
                break
            m = rx.search(line)
            if m:
                return m
        return None


def dumped():
    try:
        return int(sh('cat %s/texture-dump/index.tsv | wc -l' % FILES).strip() or 0)
    except ValueError:
        return -1


def key3(n):
    f = n.upper().split('#')
    return (f[1], f[2], f[3][:1])


class Gaps:
    """New index.tsv lines that are real gaps: not in the fork, 16+ texels, not rejected."""

    def __init__(self, fork):
        self.have = set()
        for root, _, files in os.walk(os.path.join(fork, 'GOLDENEYE')):
            if 'Hacks' in os.path.normpath(root).split(os.sep):
                continue
            self.have.update(key3(f[:-4]) for f in files if f.lower().endswith('.png'))
        self.rej = batch.rejected()
        self.n = dumped()
        self.found = set()

    def new(self):
        out = sh('tail -n +%d %s/texture-dump/index.tsv' % (self.n + 1, FILES))
        got = 0
        for line in out.splitlines():
            p = line.rstrip(chr(13)).split(chr(9))
            if len(p) < 10:
                continue
            self.n += 1
            k = key3(p[0])
            if k in self.have or k[0] in self.rej or min(int(p[1]), int(p[2])) < 16 or p[0] in self.found:
                continue
            self.found.add(p[0])
            got += 1
        return got


def press(mask, x=0, y=0, frames=4, gap=1.0):
    put('gevr_input.txt', '%s %d %d %d' % (mask, x, y, frames))
    time.sleep(gap)


def abort_level(lc):
    """In a level: watch (it opens on Mission Status, reset each level), A on the
    abort row, stick right to Confirm, A. The title stage (90) loads when it took."""
    lc.drain()
    press('1000', gap=3.5)   # the watch takes a while to come up; input before that goes astray
    for attempt in range(2):
        press('8000', gap=1.5)
        press('0', 80, 0, 8, gap=1.5)
        press('8000', gap=0.1)
        if lc.wait(r'stage: switching to 90', 5):
            time.sleep(2)
            return True
        log('abort attempt %d did not take' % (attempt + 1))
        press('0', -80, 0, 8, gap=2.5)   # a stray right turned the page: back to Mission Status
    return False


def to_level(lc, name):
    """From the report / mission select: the level hook, START, skip the intro."""
    lc.drain()
    put('gevr_level.txt', '%s 0' % name)
    for _ in range(8):   # A through whatever report pages are up until the hook fires
        if lc.wait(r'levelhook: ', 2.5):
            break
        press('8000', gap=0.5)
    else:
        log('level hook never fired for', name)
        return None
    time.sleep(1.5)
    press('1000', gap=0.2)
    m = lc.wait(r'setup: (\d+) pads, (\d+) bound pads', 30)
    if not m:
        log('no setup line for', name)
        return None
    pads, bound = int(m.group(1)), int(m.group(2))
    time.sleep(2.5)
    # skip the intro, and wait for play (intro mode 4): a warp during the intro's
    # last part (mode 3) ended Surface 2 at once, every time
    for _ in range(4):
        press('8000', gap=0.1)
        if lc.wait(r'intro-mode: \d+ -> 4', 5.0):
            break
    else:
        log('%s: no intro mode 4 seen; going on' % name)
    put('gevr_cheat.txt', 'invincible')
    time.sleep(1.5)
    return pads, bound


SPIN = '0 0 0 190 100'   # the input hook's turn field: 120 deg/s, a little over a full turn


ENDED = r'stage: switching to 90'   # the level is over (an exit zone, a failed objective)


def warp(lc, pad):
    """True: standing there; False: no room; 'exit': that pad ended the level; None: no answer."""
    lc.drain()
    put('gevr_warp.txt', str(pad))
    m = lc.wait(r'warphook: pad %d (->|has no)' % pad, 3.0)
    if m is None:
        sh('rm -f %s/gevr_warp.txt' % FILES)   # else it fires again as the level reloads
        return None
    if m.group(1) == '->':
        return 'exit' if lc.wait(ENDED, 0.6) else True
    return False


def spin(lc):
    """False when the level ended during it."""
    put('gevr_input.txt', SPIN)
    return lc.wait(ENDED, 3.8) is None


def tour_level(lc, gaps, name, stride, turn, from_pad=None):
    before = dumped()
    got = to_level(lc, name)
    if got is None:
        return False
    pads, bound = got
    todo = list(range(0, pads, stride)) + [10000 + b for b in range(0, bound, stride)]
    if from_pad is not None:
        todo = [p for p in todo if p >= from_pad]
    log('%s: %d pads, %d bound pads; %d warps' % (name, pads, bound, len(todo)))
    misses = 0
    stood = 0
    spins = 0
    exits = []
    exit_run = 0
    found0 = len(gaps.found)
    i = 0
    while i < len(todo):
        r = warp(lc, todo[i])
        if r is True and turn:
            # spin where the warp found a real gap (the room behind likely has
            # more), and every eighth stand regardless
            if gaps.new() or stood % 8 == 0:
                spins += 1
                if not spin(lc):
                    r = 'exit'
                gaps.new()
        if r is None:
            misses += 1
            if misses < 3:
                continue
            log('%s: no warp response at pad %d - skipping it, re-entering' % (name, todo[i]))
        if r is None or r == 'exit':
            step = 1
            if r == 'exit':
                exits.append(todo[i])
                # a run of exits is one big exit room (Surface 2's pads 2-6): skip
                # further each time, 1 2 4 8 16, until a warp stands again
                step = 2 ** min(exit_run, 4)
                exit_run += 1
                log('%s: pad %d ended the level - skipping %d, re-entering' % (name, todo[i], step))
            i += step
            misses = 0
            if i < len(todo) and to_level(lc, name) is None:
                return False
            continue
        misses = 0
        stood += bool(r)
        if r:
            exit_run = 0
        i += 1
        if i % 40 == 0:
            sh('am broadcast -a com.oculus.vrpowermanager.prox_close')   # it lapsed once and the headset slept
            gaps.new()
            log('%s: %d/%d warps, %d stood, %d spins, real gaps %d' % (name, i, len(todo), stood, spins, len(gaps.found) - found0))
    gaps.new()
    log('%s done: %d stood of %d, %d spins, exit pads %s, real gaps %d (tour total %d)'
        % (name, stood, len(todo), spins, exits, len(gaps.found) - found0, len(gaps.found)))
    if r != 'exit' and r is not None and not abort_level(lc):
        log('%s: could not abort the level' % name)
        return False
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fork', required=True, help='the texture pack fork (what counts as a real gap)')
    ap.add_argument('--levels', default=','.join(ORDER))
    ap.add_argument('--stride', type=int, default=1)
    ap.add_argument('--turn', type=int, default=1, help='1 = spin where warps find new textures (needs the turn field)')
    ap.add_argument('--first-in-level', action='store_true', help='a level is running now: abort it first')
    ap.add_argument('--from-pad', type=int, default=None, help='the first level starts at this pad')
    a = ap.parse_args()
    lc = Logcat()
    gaps = Gaps(a.fork)
    if a.first_in_level and not abort_level(lc):
        log('could not abort the running level')
        sys.exit(1)
    for k, name in enumerate(a.levels.split(',')):
        if not tour_level(lc, gaps, name, a.stride, a.turn, a.from_pad if k == 0 else None):
            log('stopping at', name)
            sys.exit(1)
    log('tour done, dump', dumped())


if __name__ == '__main__':
    main()
