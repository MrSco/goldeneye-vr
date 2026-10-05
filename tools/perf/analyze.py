"""Summarize and compare quest_profile.py traces.

  analyze.py summary RUN_DIR [RUN_DIR ...]      per-phase percentiles, waits, GPU, sections
  analyze.py compare BASE_DIR CAND_DIR          paired deltas with bootstrap 95% intervals

App processing is the frame's CPU time minus the time it spent blocked:
xrWaitFrame (runtime pacing), the swapchain image wait, the vertex-ring fence
wait and the frame-rate throttle. Each wait is reported on its own. Work done
between frames (input, music fades, metrics polls) is reported as pre-work.
Headline numbers use only frames that rendered the game with a valid camera,
outside the 0.5 s after any input change or tracking reset, and never the
1-in-16 detailed frames, whose per-triangle timers inflate the tail.
"""
import argparse
import csv
import json
from pathlib import Path
import re
import sys

import numpy as np

TRANSITION_NS = 500_000_000
WAITS = ['wait', 'image_wait', 'vertex_wait', 'throttle']
PHASE_ORDER = ['stationary', 'forward', 'backward', 'strafe', 'angled-contact', 'smooth-turn']


def phase_of(x, y, turn):
    if turn:
        return 'smooth-turn'
    if x and y:
        return 'angled-contact'
    if y < 0:
        return 'forward'
    if y > 0:
        return 'backward'
    if x:
        return 'strafe'
    return 'stationary'


def load_trace(path):
    with open(path) as f:
        meta = f.readline()
        rows = list(csv.DictReader(f))
    if 'gevr-profile-v2' not in meta:
        raise SystemExit(f'{path}: not a v2 trace')
    if not rows:
        return {}
    cols = rows[0].keys()
    data = {c: np.array([float(r[c]) for r in rows]) for c in cols if c != 'input_buttons'}
    data['input_buttons'] = np.array([int(r['input_buttons'], 16) for r in rows])
    return data


def frame_table(path):
    """Per-frame derived columns plus the headline mask and phase labels."""
    d = load_trace(path)
    n = len(d['start_ns'])
    frame = d['submit_ns'] - d['start_ns']
    waits = {w: d[f'{w}_ns'] for w in WAITS}
    app = frame - sum(waits.values())
    phases = np.array([phase_of(int(x), int(y), int(t)) for x, y, t in
                       zip(d['input_x'], d['input_y'], d['input_turn'])])
    # Stationary means "before any movement", not the idle gaps between phases.
    moved = np.flatnonzero(phases != 'stationary')
    if len(moved):
        phases[moved[0]:] = np.where(phases[moved[0]:] == 'stationary', 'gap', phases[moved[0]:])
    start = d['start_ns']
    settle = np.zeros(n, bool)
    edges = list(np.flatnonzero(phases[1:] != phases[:-1]) + 1) + list(np.flatnonzero(d['reset'] != 0))
    for e in edges:
        settle |= (start >= start[e]) & (start < start[e] + TRANSITION_NS)
    active = (d['kind'] > 0) & (d['camera_valid'] > 0)
    headline = active & ~settle & (d['reset'] == 0) & (d['errors'] == 0) & (d['detailed'] == 0)
    # App-side missed frames: the predicted display time skipped a refresh.
    step = np.diff(d['display_ns'], prepend=d['display_ns'][0])
    missed = np.clip(np.round(step / np.maximum(d['period_ns'], 1)) - 1, 0, None)
    missed[0] = 0
    return {'d': d, 'frame': frame, 'app': app, 'waits': waits, 'phases': phases,
            'headline': headline, 'active': active, 'missed': missed}


def pct(values, q):
    return float(np.percentile(values, q)) / 1e6 if len(values) else float('nan')


def stats(values):
    return {'n': int(len(values)), 'median': pct(values, 50), 'p95': pct(values, 95),
            'p99': pct(values, 99), 'max': pct(values, 100)}


def gpu_table(path, start, end):
    rows = []
    with open(path) as f:
        f.readline()
        for r in csv.DictReader(f):
            if start <= int(r['ready_ns']) <= end:
                rows.append((int(r['kind']), int(r['gpu_ns'])))
    return rows


def run_dirs_traces(folder):
    folder = Path(folder)
    info = json.loads((folder / 'build.json').read_text())
    traces = sorted(p for p in folder.glob('*.csv') if not p.name.endswith('.gpu.csv'))
    return info, traces


def metrics_from_log(folder):
    """xr-metric lines (XR_META_performance_metrics), if the runtime exposed any."""
    log = Path(folder) / 'device.log'
    values = {}
    unavailable = set()
    if not log.exists():
        return values, unavailable
    for line in log.read_text(errors='replace').splitlines():
        if 'xr-metric: time=' not in line:
            continue
        for path, value in re.findall(r' (/\S+?)=(\S+)', line):
            m = re.match(r'([-0-9.]+)/u(\d+)', value)
            if m:
                values.setdefault(path, []).append(float(m[1]))
            else:
                unavailable.add(path)
    return values, unavailable


def summarize(folder):
    info, traces = run_dirs_traces(folder)
    out = {'folder': str(folder), 'hz': info['hz'], 'detail': info['detail'],
           'showstats': info.get('showstats'), 'sha256': info['sha256'], 'phases': {}, 'sections': {}}
    per_phase = {}
    gpu = {1: [], 2: []}
    excluded = []
    section_self = {}
    detailed_frames = 0
    for path in traces:
        t = frame_table(path)
        d = t['d']
        for phase in PHASE_ORDER:
            m = t['headline'] & (t['phases'] == phase)
            p = per_phase.setdefault(phase, {'app': [], 'frame': [], 'pre': [], 'missed': 0, 'frames': 0,
                                             'collision': 0, 'clipped': 0, 'moving': 0,
                                             **{w: [] for w in WAITS}})
            p['app'].append(t['app'][m]); p['frame'].append(t['frame'][m]); p['pre'].append(d['pre_ns'][m])
            for w in WAITS:
                p[w].append(t['waits'][w][m])
            p['missed'] += int(t['missed'][m].sum()); p['frames'] += int(m.sum())
            req = np.hypot(d['requested_x'], d['requested_z'])
            moving = m & (d['collision'] > 0) & (req >= 1.0)
            off = np.hypot(d['requested_x'] - d['actual_x'], d['requested_z'] - d['actual_z']) > 0.01
            p['moving'] += int(moving.sum()); p['clipped'] += int((moving & off).sum())
        g = gpu_table(str(path) + '.gpu.csv', d['start_ns'][0], d['submit_ns'][-1])
        for kind, ns in g:
            gpu.setdefault(kind, []).append(ns)
        # Transitions and resets are excluded from phase statistics, not hidden.
        excluded.append(t['app'][t['active'] & ~t['headline'] & (d['detailed'] == 0)])
        sampled = t['active'] & (d['detailed'] > 0) & (d['errors'] == 0)
        detailed_frames += int(sampled.sum())
        for c in d:
            if c.endswith('_self_ns') and sampled.any():
                section_self.setdefault(c[:-8], []).append(d[c][sampled])
    for phase, p in per_phase.items():
        cat = lambda k: np.concatenate(p[k]) if p[k] else np.array([])
        out['phases'][phase] = {
            'app': stats(cat('app')), 'frame': stats(cat('frame')), 'pre': stats(cat('pre')),
            'waits': {w: stats(cat(w)) for w in WAITS},
            'runs_p99_app': [pct(a, 99) for a in p['app']], 'runs_median_app': [pct(a, 50) for a in p['app']],
            'missed_frames': p['missed'], 'frames': p['frames'],
            'moving_collision_ticks': p['moving'], 'clipped_ticks': p['clipped']}
    out['excluded_transitions'] = stats(np.concatenate(excluded)) if excluded else stats(np.array([]))
    out['gpu'] = {('fresh' if k == 1 else 'redraw'): stats(np.array(v)) for k, v in gpu.items() if v}
    if detailed_frames:
        ranked = sorted(((np.concatenate(v).mean() / 1e6, k, pct(np.concatenate(v), 99))
                         for k, v in section_self.items()), reverse=True)
        out['sections'] = {'detailed_frames': detailed_frames,
                           'self_ms_mean_p99': [(k, round(mean, 4), round(p99, 4)) for mean, k, p99 in ranked
                                                if mean > 0.001]}
    values, unavailable = metrics_from_log(folder)
    out['xr_metrics'] = {k: {'median': float(np.median(v)), 'max': float(np.max(v)), 'n': len(v)}
                         for k, v in values.items()}
    out['xr_metrics_unavailable'] = sorted(unavailable)
    return out


def bootstrap_delta(base_runs, cand_runs, q, iterations=2000, seed=7):
    """Hierarchical bootstrap: resample runs, then frames within runs."""
    rng = np.random.default_rng(seed)
    deltas = np.empty(iterations)
    def sample(runs):
        picked = [runs[i] for i in rng.integers(0, len(runs), len(runs))]
        return np.concatenate([r[rng.integers(0, len(r), len(r))] for r in picked])
    for i in range(iterations):
        deltas[i] = np.percentile(sample(cand_runs), q) - np.percentile(sample(base_runs), q)
    point = np.percentile(np.concatenate(cand_runs), q) - np.percentile(np.concatenate(base_runs), q)
    lo, hi = np.percentile(deltas, [2.5, 97.5])
    return point / 1e6, lo / 1e6, hi / 1e6


def phase_runs(folder, phase):
    _, traces = run_dirs_traces(folder)
    runs = []
    for path in traces:
        t = frame_table(path)
        m = t['headline'] & (t['phases'] == phase)
        if m.any():
            runs.append(t['app'][m])
    return runs


def compare(base, cand):
    result = {}
    for phase in PHASE_ORDER:
        b, c = phase_runs(base, phase), phase_runs(cand, phase)
        if not b or not c:
            continue
        result[phase] = {name: bootstrap_delta(b, c, q) for name, q in (('median', 50), ('p95', 95), ('p99', 99))}
    return result


def print_summary(s):
    print(f"\n## {s['folder']}  ({s['hz']} Hz, detail={s['detail']}, showstats={s['showstats']})")
    print('phase            frames  app med/p95/p99 ms      frame p99  xrWait med  img p99  vtx p99  pre p99  missed  clipped/moving')
    for phase in PHASE_ORDER:
        p = s['phases'].get(phase)
        if not p or not p['frames']:
            continue
        a, w = p['app'], p['waits']
        print(f"{phase:15s} {p['frames']:7d}  {a['median']:5.2f} / {a['p95']:5.2f} / {a['p99']:5.2f}"
              f"   {p['frame']['p99']:8.2f}  {w['wait']['median']:9.2f}  {w['image_wait']['p99']:7.2f}"
              f"  {w['vertex_wait']['p99']:7.2f}  {p['pre']['p99']:7.2f}  {p['missed_frames']:6d}"
              f"  {p['clipped_ticks']}/{p['moving_collision_ticks']}")
        print(f"{'':15s} per-run app p99: {', '.join(f'{v:.2f}' for v in p['runs_p99_app'])}")
    e = s['excluded_transitions']
    print(f"excluded transition/reset frames: n={e['n']} app p99 {e['p99']:.2f} max {e['max']:.2f} ms")
    for kind, g in s['gpu'].items():
        print(f"GPU {kind}: n={g['n']} median {g['median']:.2f} p95 {g['p95']:.2f} p99 {g['p99']:.2f} max {g['max']:.2f} ms")
    if s['sections']:
        print(f"Self time on {s['sections']['detailed_frames']} detailed frames (mean / p99 ms):")
        for name, mean, p99 in s['sections']['self_ms_mean_p99'][:15]:
            print(f'  {name:16s} {mean:7.3f} {p99:7.3f}')
    if s['xr_metrics']:
        print('XR runtime metrics (median / max):')
        for k, v in sorted(s['xr_metrics'].items()):
            print(f"  {k}: {v['median']:.3f} / {v['max']:.3f} (n={v['n']})")
    else:
        print('XR runtime metrics: none reported' +
              (f" (unavailable: {', '.join(s['xr_metrics_unavailable'])})" if s['xr_metrics_unavailable'] else ''))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='action', required=True)
    s = sub.add_parser('summary'); s.add_argument('folders', nargs='+'); s.add_argument('--json')
    c = sub.add_parser('compare'); c.add_argument('base'); c.add_argument('cand'); c.add_argument('--json')
    args = parser.parse_args(argv)
    if args.action == 'summary':
        result = [summarize(f) for f in args.folders]
        for r in result:
            print_summary(r)
    else:
        result = compare(args.base, args.cand)
        print(f'\n## {args.cand} vs {args.base}: app-processing delta ms (95% CI); negative is faster')
        for phase, r in result.items():
            cells = '  '.join(f"{k} {v[0]:+.3f} [{v[1]:+.3f}, {v[2]:+.3f}]" for k, v in r.items())
            print(f'{phase:15s} {cells}')
    if args.json:
        Path(args.json).write_text(json.dumps(result, indent=2))
    return result


if __name__ == '__main__':
    main(sys.argv[1:])
