"""Reversible, unattended Quest XR benchmark driver (tools/perf/analyze.py reads its output).

  backup  --out DIR                      snapshot saves, settings and test markers once
  run     --out DIR --apk APK --label L  install, launch Dam, record --runs traces
  restore --out DIR                      put the snapshot back and verify it byte for byte

Use the benchmark build type (assembleBenchmark): only it declares optional hand
tracking, which lets Quest launch the app while the controllers are asleep.
Each run: 15 s warm-up, then a 60 s trace whose phases are stationary, forward,
backward, strafe, angled wall contact and smooth turning. The trace records the
injected input on every frame, so analysis classifies phases from device data.
Logs stream straight to disk; logcat is never cleared.
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
LAUNCH_CHECK = 'LaunchCheckControllerRequiredDialogActivity'
MARKERS = ['gevr_input.txt', 'gevr_level.txt', 'gevr_warp.txt', 'gevr_cheat.txt',
           'gevr_profile.txt', 'gevr_profile.txt.status']
PROTECTED = ['data/goldeneye-vr.ini', 'eeprom.bin', *MARKERS]
WARMUP, MEASURE = 15, 60
# (start s into the measured minute, stick x, stick y, turn, duration ms, warp first, phase)
PHASES = [(15.0, 0, -80, 0, 5000, True, 'forward'),
          (20.0, 0, 80, 0, 5000, False, 'backward'),
          (25.0, 80, 0, 0, 5000, False, 'strafe'),
          (30.5, 55, -80, 0, 14000, True, 'angled-contact'),
          (45.0, 0, 0, 100, 14500, True, 'smooth-turn')]
TARGET_PID = ''


class Unavailable(RuntimeError):
    """The device could not produce a valid benchmark; never a performance result."""


def adb(*args, check=True, timeout=120):
    return subprocess.run(['adb', *args], check=check, capture_output=True, timeout=timeout,
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


def check_backup(out):
    """Refuse to overwrite saves or settings the user changed after the backup."""
    folder = out / 'backup'
    manifest = json.loads((folder / 'manifest.json').read_text())
    local = out / 'backup-check.tmp'
    for name in ['data/goldeneye-vr.ini', 'eeprom.bin']:
        if not manifest[name]:
            continue
        adb('pull', f'{FILES}/{name}', str(local))
        same = local.read_bytes() == (folder / name).read_bytes()
        local.unlink()
        if not same:
            raise Unavailable(f'{name} on the headset differs from the backup; take a new backup first')


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
        else:
            assert adb('shell', f'test -f {FILES}/{name} && echo yes', check=False) != 'yes', name
    print('Restored and verified protected files', flush=True)


def wake():
    adb('shell', 'am', 'broadcast', '-a', 'com.oculus.vrpowermanager.prox_close')
    adb('shell', 'input', 'keyevent', '224')


def recent():
    return adb('logcat', '-d', '-t', '1600', '--pid=' + TARGET_PID, '-v', 'brief', '-s', 'GoldenEye:*', 'GoldenEye-VR:*')


def press(mask='0', x=0, y=0, frames=4, turn=0, wait=1.0, ms=0):
    put('gevr_input.txt', f'{mask} {x} {y} {frames} {turn} {ms}' if ms else f'{mask} {x} {y} {frames} {turn}')
    time.sleep(wait)


def resumed_activity():
    text = adb('shell', 'dumpsys', 'activity', 'activities', check=False)
    match = re.search(r'(?:topResumedActivity|mResumedActivity)[=:]\s*(.+)', text)
    return match[1] if match else ''


def launch():
    global TARGET_PID
    adb('shell', 'am', 'start', '-n', PACKAGE + '/.MainActivity')
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if LAUNCH_CHECK in resumed_activity():
            raise Unavailable('Quest showed the controllers-required launch dialog; '
                              'install a benchmark-variant APK (assembleBenchmark)')
        TARGET_PID = adb('shell', 'pidof', PACKAGE, check=False)
        if TARGET_PID:
            break
        time.sleep(.5)
    else:
        raise Unavailable(f'App did not start; resumed activity: {resumed_activity()}')
    time.sleep(4)
    if not adb('shell', 'pidof', PACKAGE, check=False):
        raise Unavailable('App exited during startup')
    # Launcher START, then title/menu A presses. Level hook selects Dam at mission select.
    put('gevr_level.txt', 'dam 0')
    press('1000', wait=4)
    for _ in range(16):
        if 'levelhook:' in recent():
            break
        press('8000', wait=2)
        wake()
    else:
        raise Unavailable('Dam level hook did not fire; unattended gameplay unavailable')
    press('1000', wait=8)
    for _ in range(6):
        press('8000', wait=2)
        if re.search(r'intro-mode: \d+ -> 4', recent()):
            break
    put('gevr_cheat.txt', 'invincible')
    time.sleep(3)


def read_status():
    return adb('shell', 'cat', f'{FILES}/gevr_profile.txt.status', check=False)


def validate(path, hz, status):
    with path.open() as f:
        metadata = f.readline()
        rows = list(csv.DictReader(f))
    if 'gevr-profile-v2' not in metadata or 'overflow=0' not in metadata:
        raise Unavailable(f'Unexpected trace header or overflow: {metadata.strip()}')
    if not status.endswith(' 0'):
        raise Unavailable(f'Trace overflow reported: {status}')
    valid = [r for r in rows if int(r['kind']) and int(r['camera_valid'])]
    if len(valid) < hz * 50:
        raise Unavailable(f'Insufficient active XR gameplay: {len(valid)} rows; {metadata.strip()}')
    if any(int(r['errors']) for r in valid):
        raise Unavailable('Nested timing attribution errors; trace cannot support optimization')
    periods = [int(r['period_ns']) for r in valid]
    if abs(sum(periods) / len(periods) - 1e9 / hz) > 10000:
        raise Unavailable('Runtime did not honor the requested display rate')
    inputs = {(int(r['input_x']), int(r['input_y']), int(r['input_turn'])) for r in valid}
    missing = [p[6] for p in PHASES if (p[1], p[2], p[3]) not in inputs]
    if missing:
        raise Unavailable(f'Phases never reached the game: {missing}')
    return len(valid)


def run(args, out):
    assert re.fullmatch(r'[A-Za-z0-9_-]+', args.label)
    folder = out / args.label
    folder.mkdir(parents=True, exist_ok=False)
    check_backup(out)
    # Every process starts from the user's own save and quality settings; only the
    # refresh rate, smooth turning and the stats readout (a once-a-second log
    # burst that would distort the tail) are set for the benchmark.
    original = (out / 'backup/data/goldeneye-vr.ini').read_bytes()
    config, hz_edits = re.subn(rb'DisplayHz=\d+', f'DisplayHz={args.hz}'.encode(), original)
    config, turn_edits = re.subn(rb'SnapTurn=[^\r\n]+', b'SnapTurn=0.0', config)
    config, stat_edits = re.subn(rb'ShowStats=\d', f'ShowStats={args.showstats}'.encode(), config)
    assert hz_edits == turn_edits == stat_edits == 1, 'settings file layout changed'
    local = folder / 'settings.ini'
    local.write_bytes(config)
    adb('shell', 'am', 'force-stop', PACKAGE)
    for marker in MARKERS:
        adb('shell', 'rm', '-f', f'{FILES}/{marker}')
    adb('install', '-r', str(args.apk), timeout=300)
    if 'oculus.software.handtracking' not in adb('shell', 'dumpsys', 'package', PACKAGE):
        print('warning: installed APK is not the benchmark variant; launch may be blocked', flush=True)
    adb('push', str(local), f'{FILES}/data/goldeneye-vr.ini')
    adb('push', str(out / 'backup/eeprom.bin'), f'{FILES}/eeprom.bin')
    info = {'apk': str(args.apk.resolve()), 'sha256': hashlib.sha256(args.apk.read_bytes()).hexdigest(),
            'hz': args.hz, 'detail': args.detail, 'showstats': args.showstats, 'runs': args.runs,
            'pad': args.pad, 'phases': PHASES, 'warmup': WARMUP, 'measure': MEASURE, 'traces': []}
    log = (folder / 'device.log').open('wb')
    logger = subprocess.Popen(['adb', 'logcat', '-T', '1', '-v', 'threadtime'], stdout=log, stderr=log)
    try:
        wake()
        launch()
        for repeat in range(args.runs):
            label = f'{args.label}-{repeat + 1}'
            put('gevr_warp.txt', str(args.pad))
            time.sleep(2)
            put('gevr_profile.txt', f'record {label} {WARMUP} {MEASURE} {args.detail}')
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                ack = read_status()
                if ack.startswith(label + ' '):
                    break
                time.sleep(.2)
            else:
                raise Unavailable('No profiling acknowledgment from an actively rendering runtime')
            measure = time.monotonic() + WARMUP
            events = [{'host_offset': -WARMUP, 'phase': 'warm-up+stationary', 'ack': ack}]
            for offset, x, y, turn, ms, warp, phase in PHASES:
                while time.monotonic() < measure + offset:
                    time.sleep(min(.25, measure + offset - time.monotonic()))
                wake()
                if warp:
                    put('gevr_warp.txt', str(args.pad))
                    time.sleep(.5)
                press(x=x, y=y, frames=1, turn=turn, wait=0, ms=ms)
                events.append({'host_offset': time.monotonic() - measure, 'phase': phase,
                               'command': [x, y, turn, ms], 'warp': warp})
                print(label, phase, flush=True)
            # Export happens on the render thread after the minute; wait for its verdict.
            deadline = measure + MEASURE + 60
            while time.monotonic() < deadline:
                status = read_status()
                if status.startswith(('done ' + label + ' ', 'failed ' + label + ' ')):
                    break
                time.sleep(1)
            else:
                raise Unavailable('Trace export never reported a result')
            if status.startswith('failed'):
                raise Unavailable(f'Trace export failed on the device: {status}')
            remote = f'{FILES}/gevr_profile.txt.{label}.csv'
            adb('pull', remote, str(folder / f'{label}.csv'))
            adb('pull', remote + '.gpu.csv', str(folder / f'{label}.gpu.csv'))
            (folder / f'{label}.events.json').write_text(json.dumps(events, indent=2))
            rows = validate(folder / f'{label}.csv', args.hz, status)
            info['traces'].append({'label': label, 'status': status, 'valid_rows': rows})
            print(label, 'validated', rows, 'active frames', flush=True)
            adb('shell', 'rm', '-f', remote, remote + '.gpu.csv')
    finally:
        logger.terminate()
        logger.wait(timeout=10)
        log.close()
        adb('shell', 'am', 'force-stop', PACKAGE)
        for marker in MARKERS:
            adb('shell', 'rm', '-f', f'{FILES}/{marker}', check=False)
        (folder / 'build.json').write_text(json.dumps(info, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('action', choices=['backup', 'run', 'restore'])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--apk', type=Path)
    parser.add_argument('--label', default='baseline120')
    parser.add_argument('--hz', type=int, choices=[90, 120], default=120)
    parser.add_argument('--pad', type=int, default=0)
    parser.add_argument('--runs', type=int, default=3)
    parser.add_argument('--detail', type=int, choices=[0, 1], default=0)
    parser.add_argument('--showstats', type=int, choices=[0, 1], default=0)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    if args.action == 'backup':
        backup(args.out)
    elif args.action == 'restore':
        restore(args.out)
    else:
        try:
            run(args, args.out)
        except Unavailable as error:
            raise SystemExit(f'UNAVAILABLE: {error}')
