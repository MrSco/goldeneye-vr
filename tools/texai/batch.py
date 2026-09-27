#!/usr/bin/env python3
"""
Turn a texture dump (files/texture-dump from the headset, see gfx_pc.cpp
gevr_packdump) into AI-upscaled pack textures in the texture pack fork.

    python tools/texai/batch.py <dump_dir> <fork_dir> [--tool seedvr2|esrgan|esrgan-seedvr2]
                                [--work build/texai-batch] [--min 16] [--limit N] [--stage N] [--dry-run]

For each texture in <dump_dir>/index.tsv:
  - skipped when its name is already anywhere in <fork_dir>/GOLDENEYE (the
    authors' textures always win), or when either side is under --min texels
    (flat colours and ramps gain nothing);
  - upscaled by a local ComfyUI (comfy.py), padded by its own wrap flags;
  - post-processed by texai.py (4x4-texel colour lock, wrapped-axis seams,
    cut-out alpha) and written as <fork_dir>/GOLDENEYE/AI/<level>/<name>.

Re-running skips what is already written, so a stopped batch resumes.
<work>/log.tsv records each texture's drift score for review
(`texai.py sheet` style pages come from review.py).
"""

import argparse
import json
import os
import sys
import time

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import comfy  # noqa: E402
import texai  # noqa: E402

# LEVELID values (src/bondconstants.h enum LEVELID) -> the fork's folder names
LEVELS = {
    33: 'Mission 01 - Dam', 34: 'Mission 02 - Facility', 35: 'Mission 03 - Runway', 36: 'Mission 04 - Surface',
    9: 'Mission 05 - Bunker', 20: 'Mission 06 - Silo', 26: 'Mission 07 - Frigate', 43: 'Mission 08 - Surface 2',
    27: 'Mission 09 - Bunker 2', 22: 'Mission 10 - Statue', 24: 'Mission 11 - Archives', 29: 'Mission 12 - Streets',
    30: 'Mission 13 - Depot', 25: 'Mission 14 - Train', 37: 'Mission 15 - Jungle', 23: 'Mission 16 - Control',
    39: 'Mission 17 - Caverns', 41: 'Mission 18 - Cradle', 28: 'Mission 19 - Aztec', 32: 'Mission 20 - Egypt',
    90: 'Title and menus',
}


def level_folder(stage):
    if stage < 0:
        return 'From ROM'   # romkeys.py: named from the ROM, level unknown
    return LEVELS.get(stage, 'Other (level %d)' % stage)


def read_index(dump_dir):
    rows, seen = [], set()
    with open(os.path.join(dump_dir, 'index.tsv'), encoding='utf-8') as f:
        for line in f:
            p = line.rstrip('\n').split('\t')
            if len(p) < 10 or p[0] in seen:
                continue
            seen.add(p[0])
            rows.append(dict(name=p[0], w=int(p[1]), h=int(p[2]), fmt=int(p[3]), siz=int(p[4]), cms=int(p[5]),
                             cmt=int(p[6]), masks=int(p[7]), maskt=int(p[8]), stage=int(p[9])))
    return rows


def fork_names(fork_dir):
    names = set()
    for root, _, files in os.walk(os.path.join(fork_dir, 'GOLDENEYE')):
        for n in files:
            if n.lower().endswith('.png'):
                names.add(n)
    return names


def manifest_entry(r, img):
    """what texai/comfy need to know about a dumped texture"""
    # an axis repeats when its mask is set and it isn't clamped (bit 1); mirrored
    # axes (bit 0) are treated as clamped for the padding
    wrap = [r['masks'] != 0 and not (r['cms'] & 3), r['maskt'] != 0 and not (r['cmt'] & 3)]
    w, h = img.size
    k = texai.CANVAS // max(w, h)
    x0, y0 = (texai.CANVAS - w * k) // 2, (texai.CANVAS - h * k) // 2
    return dict(what='', kind='material' if any(wrap) else 'detail', wrap=wrap, flip=False, w=w, h=h,
                alpha=img.getextrema()[3][0] < 255, keyed=False,
                box=[x0 / texai.CANVAS, y0 / texai.CANVAS, (x0 + w * k) / texai.CANVAS, (y0 + h * k) / texai.CANVAS])


def upscale(tool, src):
    # the texture's longer side at 16x (at least 256, at most 1024 pixels), padding
    # included: twice the pack texture's 8x, which the post step averages down
    side = max(src.size[0] - 2 * comfy.PAD, src.size[1] - 2 * comfy.PAD)
    final = min(1024, max(256, side * 16)) * max(src.size) / side
    if tool == 'esrgan':
        return comfy.esrgan(src, 'batch')[0]
    if tool == 'esrgan-seedvr2':
        mid = comfy.esrgan(src, 'batch')[0]
        return comfy.seedvr2(mid, final, 'batch_2')[0]
    mid = comfy.seedvr2(src, 4 * max(src.size), 'batch')[0]
    return comfy.seedvr2(mid, final, 'batch_2')[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dump_dir')
    ap.add_argument('fork_dir')
    ap.add_argument('--tool', default='seedvr2', choices=('seedvr2', 'esrgan', 'esrgan-seedvr2'))
    ap.add_argument('--work', default=os.path.join('build', 'texai-batch'))
    ap.add_argument('--min', type=int, default=16)
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--stage', type=int, default=None)
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()

    rows = read_index(a.dump_dir)
    have = fork_names(a.fork_dir)
    todo = [r for r in rows if r['name'] not in have and min(r['w'], r['h']) >= a.min
            and (a.stage is None or r['stage'] == a.stage)]
    print('%d dumped, %d already in the fork, %d under %d texels, %d to do'
          % (len(rows), sum(r['name'] in have for r in rows),
             sum(r['name'] not in have and min(r['w'], r['h']) < a.min for r in rows), a.min, len(todo)))
    if a.dry_run:
        by = {}
        for r in todo:
            by[level_folder(r['stage'])] = by.get(level_folder(r['stage']), 0) + 1
        for k in sorted(by):
            print('  %5d %s' % (by[k], k))
        return 0

    os.makedirs(os.path.join(a.work, 'answer'), exist_ok=True)
    log = open(os.path.join(a.work, 'log.tsv'), 'a', encoding='utf-8')
    done = 0
    t0 = time.time()
    for r in todo:
        if a.limit and done >= a.limit:
            break
        tex = r['name'][:-4]
        dst_dir = os.path.join(a.fork_dir, 'GOLDENEYE', 'AI', level_folder(r['stage']))
        dst = os.path.join(dst_dir, r['name'])
        if os.path.exists(dst):
            continue
        img = Image.open(os.path.join(a.dump_dir, r['name'])).convert('RGBA')
        m = manifest_entry(r, img)
        ans_path = os.path.join(a.work, 'answer', r['name'])
        try:
            answer = upscale(a.tool, comfy.native_input(a.dump_dir, tex, m))
            comfy.framed(answer, m).save(ans_path)
            s = texai.post_one(a.dump_dir, ans_path, tex, m, os.path.join(a.work, 'final', tex))
        except Exception as e:   # one bad texture must not stop an overnight run
            print('%s: %s: %s' % (r['name'], type(e).__name__, e))
            continue
        os.makedirs(dst_dir, exist_ok=True)
        os.replace(os.path.join(a.work, 'final', tex + '_soft.png'), dst)
        log.write('%s\t%s\t%d\t%d\t%.1f\t%.1f\t%s\n' % (r['name'], a.tool, r['w'], r['h'], s['drift'], s['drift4'],
                                                       level_folder(r['stage'])))
        log.flush()
        done += 1
        el = time.time() - t0
        print('%d/%d %s %dx%d drift4 %.1f (%.1f s each, %.0f min left)'
              % (done, len(todo), r['name'], r['w'], r['h'], s['drift4'], el / done, el / done * (len(todo) - done) / 60))
    return 0


if __name__ == '__main__':
    sys.exit(main())
