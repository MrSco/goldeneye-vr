"""Synthetic-trace tests for analyze.py, using the production CSV column layout."""
from pathlib import Path
import io
import json
import re
import sys
import tempfile
from contextlib import redirect_stdout

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import analyze  # noqa: E402

source = (HERE.parents[1] / 'port/src/gevr_frame_timing.c').read_text()
fixed = re.search(r'fprintf\(out, "(start_time_ns,[^"]+)"\);', source)[1].split(',')
sections = re.findall(r'"(\w+)"', re.search(r'static const char \*names\[\] = \{(.*?)\};', source, re.S)[1])
COLUMNS = fixed + [f'{s}{suffix}' for s in sections for suffix in ('_ns', '_self_ns', '_pre_ns')]
HZ, PERIOD = 120, 8_333_333
# measured seconds: stationary 0-15, forward 15-20, backward 20-25, strafe 25-30, angled 30-45, turn 45-60
PLAN = [(0, 15, (0x0, 0)), (15, 20, (0x8, 0)), (20, 25, (0x4, 0)), (25, 30, (0x1, 0)),
        (30, 45, (0x9, 0)), (45, 60, (0x0, 100))]
APP_MS = {'stationary': 3.0, 'forward': 4.0, 'backward': 4.0, 'strafe': 4.0, 'angled-contact': 4.5, 'smooth-turn': 5.0}


def write_trace(path, shift_ms=0.0, seed=0, skip_at=None):
    rng = np.random.default_rng(seed)
    lines = ['# gevr-profile-v2 label=t rows=7200 overflow=0 detail=1', ','.join(COLUMNS)]
    gpu = ['# gpu async arrival-time samples; overflow=0', 'ready_ns,kind,gpu_ns']
    display = 10**9
    for i in range(HZ * 60):
        t = i / HZ
        start = 10**12 + int(t * 1e9)
        buttons, turn = next(cmd for a, b, cmd in PLAN if a <= t < b)
        phase = analyze.phase_of(buttons, turn)
        since = t - next(a for a, b, cmd in PLAN if a <= t < b)
        app = APP_MS[phase] + shift_ms + rng.normal(0, .05)
        if 0 < since < .3 and phase != 'stationary':
            app = 50.0          # transition hitch, excluded
        detailed = i % 16 == 0
        if detailed:
            app += 20.0         # per-triangle timers, excluded from headlines
        wait, image = 4.0, .2
        frame = app + wait + image
        display += PERIOD * (2 if i == skip_at else 1)
        row = dict.fromkeys(COLUMNS, 0)
        row.update(start_time_ns=start, submit_time_ns=start + int(frame * 1e6), display_ns=display, period_ns=PERIOD,
                   kind=1, body_valid=1, camera_valid=1, detailed=int(detailed), work_ns=int((app + image) * 1e6),
                   pre_ns=100000, input_buttons=f'{buttons:x}', input_turn=turn, collision=1,
                   move_attempted=0x5 if phase == 'angled-contact' else 0x1, move_accepted=0x1,
                   wait_ns=int(wait * 1e6), image_wait_ns=int(image * 1e6), frame_ns=int(frame * 1e6),
                   dl_self_ns=int(1.5e6) if detailed else 0, clip_self_ns=int(0.7e6) if detailed else 0)
        lines.append(','.join(str(row[c]) for c in COLUMNS))
        gpu.append(f'{start + 5_000_000},1,{int(6e6)}')
    path.write_text('\n'.join(lines) + '\n')
    path.with_suffix('.gpu.csv').write_text('\n'.join(gpu) + '\n')


def make_run(folder, shift_ms=0.0, seed=0):
    folder.mkdir()
    for n in range(3):
        write_trace(folder / f'{folder.name}-{n + 1}.csv', shift_ms, seed + n, skip_at=1000 if n == 0 else None)
    (folder / 'build.json').write_text(json.dumps({'hz': HZ, 'detail': 1, 'showstats': 0, 'sha256': 'x'}))
    (folder / 'device.log').write_text(
        '10-05 GoldenEye: xr-metric: time=1 /perfmetrics_meta/app/cpu_frametime=4.2000/u2'
        ' /perfmetrics_meta/app/gpu_frametime=unavailable(0)\n')


with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    make_run(temp / 'base')
    make_run(temp / 'same', seed=10)
    make_run(temp / 'fast', shift_ms=-1.0, seed=20)
    s = analyze.summarize(temp / 'base')
    for phase, ms in APP_MS.items():
        p = s['phases'][phase]
        assert abs(p['app']['median'] - ms) < .02, (phase, p['app'])
        assert p['app']['p99'] < ms + .3, (phase, p['app'])     # hitches and detailed frames excluded
        assert abs(p['waits']['wait']['median'] - 4.0) < 1e-6 and abs(p['frame']['median'] - ms - 4.2) < .02
    assert s['excluded_transitions']['max'] > 49 and s['excluded_transitions']['n'] > 0
    assert s['phases']['stationary']['missed_frames'] == 1
    assert sum(p['missed_frames'] for p in s['phases'].values()) == 1
    angled = s['phases']['angled-contact']
    assert angled['contact_ticks'] == angled['collision_ticks'] > 0
    assert s['phases']['forward']['contact_ticks'] == 0 and s['phases']['forward']['collision_ticks'] > 0
    assert abs(s['gpu']['fresh']['median'] - 6.0) < 1e-6
    assert s['sections']['self_ms_mean_p99'][0][0] == 'dl' and s['sections']['self_ms_mean_p99'][1][0] == 'clip'
    assert s['xr_metrics']['/perfmetrics_meta/app/cpu_frametime']['median'] == 4.2
    assert s['xr_metrics_unavailable'] == ['/perfmetrics_meta/app/gpu_frametime']
    with redirect_stdout(io.StringIO()):
        analyze.print_summary(s)
    faster = analyze.compare(temp / 'base', temp / 'fast')
    for phase in APP_MS:
        point, lo, hi = faster[phase]['median']
        assert abs(point + 1.0) < .03 and hi < 0, (phase, faster[phase])
    same = analyze.compare(temp / 'base', temp / 'same')
    for phase in APP_MS:
        point, lo, hi = same[phase]['median']
        assert lo <= 0 <= hi and abs(point) < .02, (phase, same[phase])
print('PASS: analyzer phases, transition/detail exclusion, waits, missed frames, contact, GPU, sections, metrics, bootstrap')
