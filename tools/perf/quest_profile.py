"""Reversible ADB benchmark driver. Writes logs directly to disk, never clears logcat.

backup / run --apk ... --label ... --hz 120 / restore
The backup persists between paired runs; restore is also safe after a failed run.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

FILES = '/sdcard/Android/data/com.gevr.port/files'
PACKAGE = 'com.gevr.port'
TARGET_PID = ''
MARKERS = ['gevr_input.txt', 'gevr_level.txt', 'gevr_warp.txt', 'gevr_cheat.txt',
           'gevr_profile.txt', 'gevr_profile.txt.status']
PROTECTED = ['data/goldeneye-vr.ini', 'eeprom.bin', *MARKERS]


def adb(*args, check=True):
    return subprocess.run(['adb', *args], check=check, capture_output=True,
                          encoding='utf-8', errors='replace').stdout.strip()


def put(name, value):
    # Values are fixed numeric benchmark commands or validated labels.
    adb('shell', f"printf '%s' '{value}' > {FILES}/{name}")


def backup(out):
    folder = out / 'backup'
    folder.mkdir(parents=True, exist_ok=False)
    adb('shell', 'am', 'force-stop', PACKAGE)
    manifest = {}
    for name in PROTECTED:
        exists = adb('shell', f'test -f {FILES}/{name} && echo yes', check=False) == 'yes'
        manifest[name] = exists
        if exists:
            local = folder / name
            local.parent.mkdir(parents=True, exist_ok=True)
            adb('pull', f'{FILES}/{name}', str(local))
    (folder / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    (folder / 'power.txt').write_text(adb('shell', 'dumpsys', 'power'))
    (folder / 'package.txt').write_text(adb('shell', 'dumpsys', 'package', PACKAGE))
    print('Backed up saves, configuration and existing test markers', flush=True)


def restore(out):
    folder = out / 'backup'
    manifest = json.loads((folder / 'manifest.json').read_text())
    adb('shell', 'am', 'force-stop', PACKAGE)
    for name, existed in manifest.items():
        if existed:
            adb('push', str(folder / name), f'{FILES}/{name}')
        else:
            adb('shell', 'rm', '-f', f'{FILES}/{name}')
    adb('shell', 'am', 'broadcast', '-a', 'com.oculus.vrpowermanager.automation_disable', check=False)
    adb('shell', 'am', 'broadcast', '-a', 'com.oculus.vrpowermanager.prox_open', check=False)
    if 'mWakefulness=Asleep' in (folder / 'power.txt').read_text():
        adb('shell', 'input', 'keyevent', '223')
    for name, existed in manifest.items():
        if existed:
            local = out / 'restore-check.tmp'
            adb('pull', f'{FILES}/{name}', str(local))
            assert local.read_bytes() == (folder / name).read_bytes(), name
            local.unlink()
    print('Restored and verified protected files', flush=True)


def wake():
    adb('shell', 'am', 'broadcast', '-a', 'com.oculus.vrpowermanager.prox_close')
    adb('shell', 'input', 'keyevent', '224')


def recent():
    return adb('logcat', '-d', '-t', '1600', '--pid=' + TARGET_PID, '-v', 'brief', '-s', 'GoldenEye:*', 'GoldenEye-VR:*')


def press(mask='0', x=0, y=0, frames=4, turn=0, wait=1):
    put('gevr_input.txt', f'{mask} {x} {y} {frames} {turn}')
    time.sleep(wait)


def launch():
    global TARGET_PID
    adb('shell', 'am', 'start', '-n', PACKAGE + '/.MainActivity')
    time.sleep(4)
    TARGET_PID = adb('shell', 'pidof', PACKAGE)
    if not TARGET_PID:
        raise RuntimeError('App exited during startup')
    # Launcher START, then title/menu A presses. Level hook selects Dam at mission select.
    put('gevr_level.txt', 'dam 0')
    press('1000', wait=4)
    for _ in range(16):
        if 'levelhook:' in recent():
            break
        press('8000', wait=2)
        wake()
    else:
        raise RuntimeError('Dam level hook did not fire; unattended gameplay unavailable')
    press('1000', wait=8)
    for _ in range(6):
        press('8000', wait=2)
        if re.search(r'intro-mode: \d+ -> 4', recent()):
            break
    put('gevr_cheat.txt', 'invincible')
    time.sleep(3)


def run(args, out):
    assert re.fullmatch(r'[A-Za-z0-9_-]+', args.label)
    folder = out / args.label
    folder.mkdir(parents=True, exist_ok=True)
    # Restore initial game state for every process, retaining the user's rendering quality.
    original = (out / 'backup/data/goldeneye-vr.ini').read_bytes()
    config = re.sub(rb'DisplayHz=\d+', f'DisplayHz={args.hz}'.encode(), original)
    config = re.sub(rb'SnapTurn=[^\r\n]+', b'SnapTurn=0.0', config)
    local = folder / 'settings.ini'
    local.write_bytes(config)
    adb('shell', 'am', 'force-stop', PACKAGE)
    for marker in MARKERS:
        adb('shell', 'rm', '-f', f'{FILES}/{marker}')
    adb('push', str(local), f'{FILES}/data/goldeneye-vr.ini')
    adb('push', str(out / 'backup/eeprom.bin'), f'{FILES}/eeprom.bin')
    adb('install', '-r', str(args.apk))
    log = (folder / 'device.log').open('wb')
    logger = subprocess.Popen(['adb', 'logcat', '-T', '1', '-v', 'threadtime'], stdout=log, stderr=log)
    try:
        wake()
        launch()
        for repeat in range(args.runs):
            label = f'{args.label}-{repeat + 1}'
            put('gevr_warp.txt', str(args.pad))
            time.sleep(2)
            put('gevr_profile.txt', f'record {label} 15 60 {args.detail}')
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                ack = adb('shell', 'cat', f'{FILES}/gevr_profile.txt.status', check=False)
                if ack.startswith(label + ' '):
                    break
                time.sleep(.2)
            else:
                raise RuntimeError('No profiling acknowledgment from an actively rendering runtime')
            # Fifteen-second warm-up, followed by four repeatable workloads in one 60 s trace.
            start = time.monotonic()
            events = []
            sequence = [(0, 0, 0, 600, 0, 'warm-up'),
                        (15, 0, 0, 600, 0, 'stationary'),
                        (30, 0, -80, 260, 0, 'forward'),
                        (35, 0, 80, 260, 0, 'backward'),
                        (40, 80, 0, 260, 0, 'strafe'),
                        (45, 55, -80, 600, 0, 'angled-contact'),
                        (60, 0, 0, 600, 100, 'smooth-turn')]
            for offset, x, y, frames, turn, phase in sequence:
                while time.monotonic() - start < offset:
                    time.sleep(min(.25, offset - (time.monotonic() - start)))
                wake()
                if offset in (30, 45, 60):
                    put('gevr_warp.txt', str(args.pad))
                press(x=x, y=y, frames=frames, turn=turn, wait=.05)
                events.append({'elapsed': time.monotonic() - start, 'phase': phase,
                               'command': [x, y, frames, turn]})
                print(label, phase, flush=True)
            while time.monotonic() - start < 77:
                time.sleep(.25)
            remote = f'{FILES}/gevr_profile.txt.{label}.csv'
            adb('pull', remote, str(folder / f'{label}.csv'))
            adb('pull', remote + '.gpu.csv', str(folder / f'{label}.gpu.csv'))
            (folder / f'{label}.events.json').write_text(json.dumps(events, indent=2))
            with (folder / f'{label}.csv').open() as f:
                metadata = f.readline()
                rows = list(csv.DictReader(f))
            valid = [r for r in rows if int(r['kind']) and int(r['camera_valid'])]
            if len(valid) < args.hz * 50:
                raise RuntimeError(f'Insufficient active XR gameplay: {len(valid)} rows; {metadata}')
            if any(int(r['errors']) for r in valid):
                raise RuntimeError('Nested timing attribution errors; trace cannot support optimization')
            periods = [int(r['period_ns']) for r in valid]
            if abs(sum(periods)/len(periods) - 1e9/args.hz) > 10000:
                raise RuntimeError('Runtime did not honor the requested display rate')
            print(label, 'validated', len(valid), 'active frames', flush=True)
            adb('shell', 'rm', '-f', remote, remote + '.gpu.csv')
    finally:
        logger.terminate()
        logger.wait(timeout=10)
        log.close()
        adb('shell', 'am', 'force-stop', PACKAGE)
    (folder / 'build.json').write_text(json.dumps({'apk': str(args.apk.resolve()),
        'sha256': hashlib.sha256(args.apk.read_bytes()).hexdigest(), 'hz': args.hz,
        'detail': args.detail, 'runs': args.runs, 'pad': args.pad}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('action', choices=['backup', 'run', 'restore'])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--apk', type=Path)
    parser.add_argument('--label', default='baseline120')
    parser.add_argument('--hz', type=int, choices=[90, 120], default=120)
    parser.add_argument('--pad', type=int, default=0)
    parser.add_argument('--runs', type=int, default=3)
    parser.add_argument('--detail', type=int, choices=[0, 1], default=1)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    if args.action == 'backup':
        backup(args.out)
    elif args.action == 'restore':
        restore(args.out)
    else:
        run(args, args.out)
