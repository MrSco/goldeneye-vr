"""Reversible, unattended Quest XR benchmark driver (tools/perf/analyze.py reads its output).

  backup  --out DIR                      snapshot saves, settings and test markers once
  run     --out DIR --apk APK --label L  install, launch Dam, record --runs traces
  restore --out DIR                      put the snapshot back and verify it byte for byte

Use the benchmark build type (assembleBenchmark): only it declares optional hand
tracking, which lets Quest launch the app while the controllers are asleep.
Each run: 15 s warm-up, then a 60 s trace whose phases are stationary, forward,
backward, strafe, angled wall contact and smooth turning. The trace records the
injected input on every frame, so analysis classifies phases from device data.
--cold PADS instead traces first-view cost: a fresh process per run, recorded from
its first level frames while it warps to each pad and turns around once, the route
twice (cold, then warm); analyze.py cold reads it.
Logs stream straight to disk; logcat is never cleared.
"""
import argparse
import os
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
MARKERS = ['gevr_fakegrip.txt','gevr_scopedump','gevr_scope.pam', 'gevr_body_batch.txt','gevr_rigid_min.txt','gevr_room_draws.txt', 'gevr_pipeline.txt', 'gevr_spacewarp.txt', 'gevr_cpulevel.txt', 'gevr_bean_nodemode.txt', 'gevr_input.txt', 'gevr_level.txt', 'gevr_warp.txt', 'gevr_cheat.txt',
           'gevr_profile.txt', 'gevr_profile.txt.status']
PROTECTED = ['data/goldeneye-vr.ini', 'eeprom.bin', *MARKERS]
WARMUP, MEASURE = 15, 60
# Stereo play walks with the C buttons the left stick produces (input.c):
# U 0x8 forward, D 0x4 back, L 0x2 / R 0x1 strafe. The right stick's turn is the
# hook's turn field. The N64 stick itself is ignored in stereo.
# (start s into the measured minute, button mask, turn, duration ms, warp first, phase)
PHASES = [(15.0, 0x8, 0, 5000, True, 'forward'),
          (20.0, 0x4, 0, 5000, False, 'backward'),
          (25.0, 0x1, 0, 5000, False, 'strafe'),
          (30.5, 0x9, 0, 14000, True, 'angled-contact'),
          (45.0, 0x0, 100, 14500, True, 'smooth-turn')]
# --cold: a fresh process per run, traced from its first level frames. Each pad
# of the route is warped to and turned around once, the route twice (cold, then
# warm). Each segment's turn value tags it in the trace: 55+i, then -(55+i).
COLD_SEGMENT = 7.0
TARGET_PID = ''


class Unavailable(RuntimeError):
    """The device could not produce a valid benchmark; never a performance result."""


def adb(*args, check=True, timeout=120):
    return subprocess.run(['adb', *args], check=check, capture_output=True, timeout=timeout,
                          encoding='utf-8', errors='replace').stdout.strip()


def push_back(local, remote, check=True):
    # adb pushes as shell, mode 644: the game could then read its settings and
    # save but not write them (Gun fit, progress). Give them back writable.
    adb('push', str(local), remote, check=check)
    adb('shell', 'chmod', '666', remote, check=check)


def put(name, value):
    # Values are fixed numeric benchmark commands or validated labels. The game's
    # hooks read and delete a marker as soon as it exists, so write it whole and
    # rename it into place: a half-written marker was read empty and lost.
    adb('shell', f"printf '%s' '{value}' > {FILES}/.{name}.tmp && mv {FILES}/.{name}.tmp {FILES}/{name}")


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
            push_back(folder / name, f'{FILES}/{name}')
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


def clear_stale_launch_checks():
    """A controllers-required dialog left by an earlier blocked launch keeps
    every later launch cached behind it; dismiss only that dialog's task."""
    text = adb('shell', 'dumpsys', 'activity', 'activities', check=False)
    tasks = sorted(set(re.findall(LAUNCH_CHECK + r' t(\d+)', text)))
    for task in tasks:
        adb('shell', 'am', 'stack', 'remove', task, check=False)
        print('dismissed stale launch-check dialog task', task, flush=True)
    if tasks:
        time.sleep(1.5)


def launch():
    global TARGET_PID
    clear_stale_launch_checks()
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
    # A headset waking from the desk can take a while to give the session focus;
    # START sent before it is dropped and the launcher never boots the game.
    focus_deadline = time.monotonic() + 30
    while time.monotonic() < focus_deadline and 'Session state changed: 5' not in recent():
        wake()
        time.sleep(1)
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


def cold_segments(pads):
    """[(pad, turn, name)] for the cold pass then the warm pass over the route."""
    return [(pad, sign * (55 + i), f'{kind}-{pad}') for sign, kind in ((1, 'cold'), (-1, 'warm'))
            for i, pad in enumerate(pads)]


def validate(path, hz, status, spacewarp=0, phases=PHASES, seconds=MEASURE):
    with path.open() as f:
        metadata = f.readline()
        rows = list(csv.DictReader(f))
    if 'gevr-profile-v2' not in metadata or 'overflow=0' not in metadata:
        raise Unavailable(f'Unexpected trace header or overflow: {metadata.strip()}')
    if not status.endswith(' 0'):
        raise Unavailable(f'Trace overflow reported: {status}')
    valid = [r for r in rows if int(r['kind']) and int(r['camera_valid'])]
    # Count active time, not an assumed successful frame rate: a slow renderer
    # must remain measurable at a requested 120 Hz. The game ticks at 60 Hz.
    active_seconds = (int(valid[-1]['start_time_ns']) - int(valid[0]['start_time_ns'])) / 1e9 if valid else 0
    if len(valid) < seconds * 30 or active_seconds < seconds - 10:
        raise Unavailable(f'Insufficient active XR gameplay: {len(valid)} rows over {active_seconds:.1f} s; {metadata.strip()}')
    if any(int(r['errors']) for r in valid):
        raise Unavailable('Nested timing attribution errors; trace cannot support optimization')
    periods = [int(r['period_ns']) for r in valid]
    # SpaceWarp: the runtime runs the app at half the display rate and makes the frames between.
    app_hz = hz / 2 if spacewarp else hz
    if abs(sum(periods) / len(periods) - 1e9 / app_hz) > 10000:
        raise Unavailable('Runtime did not honor the requested display rate')
    inputs = {(int(r['input_buttons'], 16), int(r['input_turn'])) for r in valid}
    missing = [p[5] for p in phases if (p[1], p[2]) not in inputs]
    if missing:
        raise Unavailable(f'Phases never reached the game: {missing}')
    return len(valid)


def start(args, config, folder):
    """Diagnostic controls, launch into Dam, and check the renderer took them."""
    wake()
    put('gevr_body_batch.txt',str(args.body_batch))
    put('gevr_rigid_min.txt',str(args.rigid_min))
    put('gevr_room_draws.txt',str(args.room_draws))
    put('gevr_pipeline.txt',str(args.pipeline))
    put('gevr_spacewarp.txt', str(args.spacewarp))
    if args.cpu_level is not None:
        put('gevr_cpulevel.txt', str(args.cpu_level))
    put('gevr_bean_nodemode.txt',str(args.node_mode))
    launch()
    applied=recent()
    expected=f'gevr bean controls: rigid_min={args.rigid_min} body_batch={args.body_batch}'
    # The room switch is read only when XBLA rooms build (XblaVisualsMode=1);
    # the Bean pack alone replaces characters and guns.
    rooms = re.search(rb'XblaVisualsMode=1', config) is not None
    if expected not in applied or (rooms and f'gevr bean room-draws: {args.room_draws}' not in applied) or \
            f'gevr pipeline: {args.pipeline}' not in applied:
        raise Unavailable('Renderer did not acknowledge requested diagnostic controls')
    # the run's own streamed log: the device's buffer rotates in under a minute
    # while Quest launches, and lost SpaceWarp's lines before this check
    swlog = (folder / 'device.log').read_text(errors='replace')
    # a build without SpaceWarp says nothing about it; one with it must say what it did
    if args.spacewarp and (f'gevr spacewarp: wanted=1' not in swlog or 'SpaceWarp: on' not in swlog):
        raise Unavailable('SpaceWarp was requested but not turned on')
    if args.body_batch and 'gevr bean body-batch:' not in applied:
        raise Unavailable('Whole-character batching was requested but never applied')


def await_trace(label, measure, folder, info, hz, spacewarp, phases, seconds):
    """Wait for the device's export of a recorded trace, pull and validate it."""
    deadline = measure + seconds + 60
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
    adb('shell', 'rm', '-f', remote, remote + '.gpu.csv')   # pulled; never leave traces behind
    rows = validate(folder / f'{label}.csv', hz, status, spacewarp, phases, seconds)
    info['traces'].append({'label': label, 'status': status, 'valid_rows': rows})
    print(label, 'validated', rows, 'active frames', flush=True)


def cold_runs(args, config, folder, info):
    """First-view cost: each run is a fresh process traced from its first level
    frames over the route's pads, cold then warm (COLD_SEGMENT)."""
    segments = cold_segments(args.cold)
    seconds = int(len(segments) * COLD_SEGMENT) + 2
    assert seconds <= 60, 'route too long for one trace'
    phases = [(0, 0, turn, 0, True, name) for pad, turn, name in segments]
    info['cold_segments'] = segments
    for repeat in range(args.runs):
        label = f'{args.label}-{repeat + 1}'
        if repeat:
            # nothing carried over: the next run starts a new process
            adb('shell', 'am', 'force-stop', PACKAGE)
            start(args, config, folder)
        put('gevr_profile.txt', f'record {label} 1 {seconds} {args.detail}')
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            ack = read_status()
            if ack.startswith(label + ' '):
                break
            time.sleep(.2)
        else:
            raise Unavailable('No profiling acknowledgment from an actively rendering runtime')
        measure = time.monotonic() + 1
        events = []
        for i, (pad, turn, name) in enumerate(segments):
            while time.monotonic() < measure + i * COLD_SEGMENT:
                time.sleep(min(.25, measure + i * COLD_SEGMENT - time.monotonic()))
            wake()
            put('gevr_warp.txt', str(pad))
            press('0', frames=1, turn=turn, wait=0, ms=int((COLD_SEGMENT - .5) * 1000))
            events.append({'host_offset': time.monotonic() - measure, 'phase': name, 'pad': pad, 'turn': turn})
            print(label, name, flush=True)
        (folder / f'{label}.events.json').write_text(json.dumps(events, indent=2))
        await_trace(label, measure, folder, info, args.hz, args.spacewarp, phases, seconds)
        if repeat == 0:
            # where each pad actually is, captured after the trace so it costs it nothing
            for pad in args.cold:
                put('gevr_warp.txt', str(pad))
                time.sleep(2)
                shot = subprocess.check_output(['adb', 'exec-out', 'screencap', '-p'], timeout=30)
                (folder / f'pad{pad}.view.png').write_bytes(shot)


def run(args, out):
    assert re.fullmatch(r'[A-Za-z0-9_-]+', args.label)
    folder = out / args.label
    folder.mkdir(parents=True, exist_ok=False)
    check_backup(out)
    manifest=json.loads((out/'backup/manifest.json').read_text())
    if any(marker not in manifest for marker in MARKERS):
        raise Unavailable('Backup is missing new diagnostic markers; preserve them before running')
    # Every process starts from the user's own save and quality settings; only the
    # refresh rate, smooth turning and the stats readout (a once-a-second log
    # burst that would distort the tail) are set for the benchmark.
    original = (out / 'backup/data/goldeneye-vr.ini').read_bytes()
    config, hz_edits = re.subn(rb'DisplayHz=\d+', f'DisplayHz={args.hz}'.encode(), original)
    config, turn_edits = re.subn(rb'SnapTurn=[^\r\n]+', b'SnapTurn=0.0', config)
    config, stat_edits = re.subn(rb'ShowStats=\d', f'ShowStats={args.showstats}'.encode(), config)
    assert hz_edits == turn_edits == stat_edits == 1, 'settings file layout changed'
    if args.texture_pack is not None:
        # '' (none) or a pack folder name, otherwise the user's own choice.
        pack = '' if args.texture_pack == 'none' else args.texture_pack
        assert re.fullmatch(r'[A-Za-z0-9_.-]*', pack)
        config, pack_edits = re.subn(rb'ActiveTexturePack=[^\r\n]*', f'ActiveTexturePack={pack}'.encode(), config)
        assert pack_edits == 1, 'settings file layout changed'
    if args.xbla is not None:
        config, xbla_edits = re.subn(rb'XblaVisualsMode=\d', f'XblaVisualsMode={args.xbla}'.encode(), config)
        assert xbla_edits == 1, 'settings file layout changed'
    if args.msaa is not None:
        # the launcher's Anti-aliasing (EyeMsaa: 1 off, 2; a 4 reads as 2)
        config, msaa_edits = re.subn(rb'EyeMsaa=\d', f'EyeMsaa={args.msaa}'.encode(), config)
        assert msaa_edits == 1, 'settings file layout changed'
    local = folder / 'settings.ini'
    local.write_bytes(config)
    adb('shell', 'am', 'force-stop', PACKAGE)
    for marker in MARKERS:
        adb('shell', 'rm', '-f', f'{FILES}/{marker}')
    aapt = Path('C:/Users/Occor/AppData/Local/Android/Sdk/build-tools/36.0.0/aapt2.exe')
    badging = subprocess.run([str(aapt), 'dump', 'badging', str(args.apk)], capture_output=True,
                             encoding='utf-8', errors='replace').stdout
    if "name='oculus.software.handtracking'" not in badging:
        print('warning: APK is not the benchmark variant; Quest may block the launch', flush=True)
    adb('install', '-r', str(args.apk), timeout=300)
    push_back(local, f'{FILES}/data/goldeneye-vr.ini')
    push_back(out / 'backup/eeprom.bin', f'{FILES}/eeprom.bin')
    info = {'apk': str(args.apk.resolve()), 'sha256': hashlib.sha256(args.apk.read_bytes()).hexdigest(),
            'hz': args.hz, 'detail': args.detail, 'showstats': args.showstats, 'runs': args.runs,
            'pad': args.pad, 'scope_probe': args.scope_probe, 'body_batch': args.body_batch, 'rigid_min': args.rigid_min, 'room_draws': args.room_draws, 'pipeline': args.pipeline, 'spacewarp': args.spacewarp, 'node_mode': args.node_mode, 'simpleperf': args.simpleperf, 'texture_pack': args.texture_pack, 'xbla': args.xbla, 'msaa': args.msaa, 'cpu_level': args.cpu_level, 'cold': args.cold, 'phases': PHASES, 'warmup': WARMUP, 'measure': MEASURE, 'traces': []}
    log = (folder / 'device.log').open('wb')
    logger = subprocess.Popen(['adb', 'logcat', '-T', '1', '-v', 'threadtime'], stdout=log, stderr=log)
    try:
        start(args, config, folder)
        if args.cold:
            cold_runs(args, config, folder, info)
            return
        if args.scope_probe:
            put('gevr_fakegrip.txt','1 0 -0.10 -0.30 0.70710678 0 0 0.70710678')
            put('gevr_cheat.txt','hold17 invincible maxammo')
            time.sleep(3)
            scope_log=recent()
            if 'stereo: scope on (hand 0 item 17)' not in scope_log:
                raise Unavailable('Synthetic scope probe did not activate the real scope renderer')
        for repeat in range(args.runs):
            label = f'{args.label}-{repeat + 1}'
            put('gevr_warp.txt', str(args.pad))
            time.sleep(2)
            # read at the first Bean model draw, so only once the level shows
            if repeat == 0 and 'gevr bean controls: nodemode=' in recent() and \
                    f'gevr bean controls: nodemode={args.node_mode}' not in recent():
                raise Unavailable('Renderer did not acknowledge the requested Bean node mode')
            # Capture the actual location and geometry before recording/warm-up.
            # A pad number alone does not establish which reported scene is tested.
            screenshot=subprocess.check_output(['adb','exec-out','screencap','-p'],timeout=30)
            (folder / f'{label}.view.png').write_bytes(screenshot)
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
            sampler = None
            if args.simpleperf:
                # Attribution only: sampling perturbs timing, so these traces are
                # never compared with unsampled runs.
                while time.monotonic() < measure:
                    time.sleep(.1)
                # A user build only lets the shell sample a profileable app by package.
                sampler = subprocess.Popen(['adb', 'shell', 'simpleperf', 'record', '--app', PACKAGE,
                                            '-e', 'cpu-clock', '-f', '2000', '--call-graph', 'fp', '--clockid', 'monotonic',
                                            '--duration', str(MEASURE), '-o', '/data/local/tmp/gevr-perf.data'],
                                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            for offset, mask, turn, ms, warp, phase in PHASES:
                while time.monotonic() < measure + offset:
                    time.sleep(min(.25, measure + offset - time.monotonic()))
                wake()
                if warp:
                    put('gevr_warp.txt', str(args.pad))
                    time.sleep(.5)
                press(f'{mask:x}', frames=1, turn=turn, wait=0, ms=ms)
                events.append({'host_offset': time.monotonic() - measure, 'phase': phase,
                               'command': [mask, turn, ms], 'warp': warp})
                print(label, phase, flush=True)
            if sampler:
                output = sampler.communicate(timeout=MEASURE + 60)[0].decode(errors='replace')
                (folder / f'{label}.simpleperf.txt').write_text(output)
                if sampler.returncode:
                    raise Unavailable(f'simpleperf failed: {output.strip()[-300:]}')
                adb('pull', '/data/local/tmp/gevr-perf.data', str(folder / f'{label}.perf.data'))
                adb('shell', 'rm', '-f', '/data/local/tmp/gevr-perf.data')
            # Export happens on the render thread after the minute; wait for its verdict.
            (folder / f'{label}.events.json').write_text(json.dumps(events, indent=2))
            await_trace(label, measure, folder, info, args.hz, args.spacewarp, PHASES, MEASURE)
    finally:
        logger.terminate()
        logger.wait(timeout=10)
        log.close()
        adb('shell', 'am', 'force-stop', PACKAGE)
        for marker in MARKERS:
            adb('shell', 'rm', '-f', f'{FILES}/{marker}', check=False)
        # Success or failure, the headset gets the user's own settings and save back.
        push_back(out / 'backup/data/goldeneye-vr.ini', f'{FILES}/data/goldeneye-vr.ini', check=False)
        push_back(out / 'backup/eeprom.bin', f'{FILES}/eeprom.bin', check=False)
        (folder / 'build.json').write_text(json.dumps(info, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('action', choices=['backup', 'run', 'restore'])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--apk', type=Path)
    parser.add_argument('--label', default='baseline120')
    parser.add_argument('--hz', type=int, choices=[90, 120], default=120)
    parser.add_argument('--pad', type=int, default=0)
    parser.add_argument('--cold', type=lambda s: [int(p) for p in s.split(',')],
                        help='first-view cost: comma-separated route pads, each a fresh process '
                             'traced from its first level frames, cold pass then warm (analyze.py cold)')
    parser.add_argument('--room-draws',type=int,choices=[0,1],default=1,help='0 disables opaque world batching (the game default is on)')
    parser.add_argument('--body-batch',type=int,choices=[0,1],default=1,help='0 disables whole-character batching (the game default is on)')
    parser.add_argument('--rigid-min',type=int,default=0,help='minimum triangles for a retained prop; 0 retains all (the game default)')
    parser.add_argument('--runs', type=int, default=3)
    parser.add_argument('--detail', type=int, choices=[0, 1], default=0)
    parser.add_argument('--showstats', type=int, choices=[0, 1], default=0)
    parser.add_argument('--xbla', type=int, choices=[0, 1], help="XBLA rooms/skies (XblaVisualsMode); default keeps the user's setting")
    parser.add_argument('--msaa', type=int, choices=[1, 2], help="eye anti-aliasing samples (EyeMsaa); default keeps the user's setting")
    parser.add_argument('--cpu-level', type=int, choices=[-1, 0, 1, 2],
                        help='CPU performance level asked at session start (files/gevr_cpulevel.txt); '
                             "-1 none; default keeps the game's CpuLevel")
    parser.add_argument('--texture-pack', help="'none' or a pack folder; default keeps the user's setting")
    parser.add_argument('--scope-probe',action='store_true',
                        help='equip sniper and use an explicit synthetic grip for unattended real scope rendering')
    parser.add_argument('--spacewarp', type=int, choices=[0, 1], default=0,
                        help='1 turns on Application SpaceWarp (files/gevr_spacewarp.txt)')
    parser.add_argument('--pipeline', type=int, choices=[0, 1, 2], default=2,
                        help='0 ticks the game inside its XR frame (the old order), 1 ticks it early but draws its '
                             'eye pass inside the frame, 2 (the game default) also replays the eye pass')
    parser.add_argument('--node-mode', type=int, choices=[0, 1], default=1,
                        help="0 draws Bean's model lists in their own state, 1 (the game default) in the game's node state")
    parser.add_argument('--simpleperf', action='store_true',
                        help='sample the measured minute with simpleperf (attribution only; needs the benchmark build)')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    # One driver per headset: a second run's restore would overwrite the first's
    # settings mid-trace and both would inject input into one game.
    lock = args.out / '.driver.lock'
    try:
        handle = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        raise SystemExit(f'BUSY: another quest_profile.py holds {lock} (delete it if no driver is running)')
    try:
        os.write(handle, str(os.getpid()).encode())
        os.close(handle)
        if args.action == 'backup':
            backup(args.out)
        elif args.action == 'restore':
            restore(args.out)
        else:
            try:
                run(args, args.out)
            except Unavailable as error:
                raise SystemExit(f'UNAVAILABLE: {error}')
    finally:
        lock.unlink(missing_ok=True)
