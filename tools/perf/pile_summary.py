"""Per-second frame stats from a pile_drive.py logcat (Show stats on): ragdolls
so far, XR fps, GPU eye ms, VrApi GPU busy, CPU game ms, draws, tris; and the
Bean skin share lines.
usage: python tools/perf/pile_summary.py LOGCAT [from-ragdolls]"""
import re
import sys

text = open(sys.argv[1], errors='replace').read().splitlines()
start = int(sys.argv[2]) if len(sys.argv) > 2 else 0
rows, cur, bodies, skins = [], {}, 0, set()
for line in text:
    m = re.search(r'ragdoll: body (\d+), ', line)
    if m:
        bodies = max(bodies, int(m.group(1)))
    m = re.search(r'bean skin: (\d+) vertices, (\d+) distinct', line)
    if m:
        skins.add((int(m.group(1)), int(m.group(2))))
    t = line[6:14]
    if 'GoldenEye-VR: DISPLAY' in line:
        cur = {'t': t, 'bodies': bodies, 'xr': int(re.search(r'XR (\d+) FPS', line).group(1))}
        rows.append(cur)
    elif cur and 'GoldenEye-VR: GPU EYE MAX F' in line:
        m = re.search(r'MAX F ([\d.]+)', line)
        if m:
            cur['gpu'] = float(m.group(1))
    elif cur and 'GoldenEye-VR: CPU GAME' in line:
        cur['game'] = float(re.search(r'GAME ([\d.]+)', line).group(1))
    elif cur and 'GoldenEye-VR: DRAW/TICK' in line:
        cur['draw'] = float(re.search(r'TICK ([\d.]+)', line).group(1))
    elif cur and 'GoldenEye-VR: DRAWS' in line:
        m = re.search(r'DRAWS (\d+) TRIS (\d+)K', line); cur['draws'] = int(m.group(1)); cur['tris'] = int(m.group(2))
    elif cur and 'VrApi' in line and 'GPU%=' in line:
        cur['busy'] = float(re.search(r'GPU%=([\d.]+)', line).group(1))
print('skins (vertices -> distinct):', sorted(skins)[:12], '...' if len(skins) > 12 else '')
if skins:
    v = sum(a for a, b in skins); d = sum(b for a, b in skins)
    print(f'  all skins: {v} -> {d} ({d / v:.0%})')
sel = [r for r in rows if r['bodies'] >= start and 'tris' in r]
print('t        bodies xr  gpuF  busy game draw draws trisK')
for r in sel:
    print(f"{r['t']} {r['bodies']:6} {r['xr']:3} {r.get('gpu', 0):5.1f} {r.get('busy', 0):4.2f} {r.get('game', 0):4.1f} "
          f"{r.get('draw', 0):4.1f} {r.get('draws', 0):5} {r.get('tris', 0):5}")
if sel:
    worst = sorted(sel, key=lambda r: r['xr'])[:10]
    print('worst 10 seconds: xr', [r['xr'] for r in worst], 'gpuF', [r.get('gpu') for r in worst],
          'tris', [r.get('tris') for r in worst])
