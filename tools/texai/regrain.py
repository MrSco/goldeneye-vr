#!/usr/bin/env python3
"""
Redo grainy SeedVR2 textures with Real-ESRGAN where that is more faithful.

    python tools/texai/regrain.py <fork_dir> <dump_dir>... [--work build/texai-regrain]
                                  [--margin 0.5] [--apply]

SeedVR2 draws structure into grainy, photo-like textures (flat share under
batch.GRAINY: the Statue Park statue's tiles came out as chrome ornaments,
a different one each tile). For each AI texture in the fork whose original is
in one of the dump dirs (their index.tsv), is grainy, and was made by SeedVR2
(the batch logs under build/ say which tool):
  - make the Real-ESRGAN version in <work>/esrgan/ (the fork untouched);
  - score both at native resolution: PSNR of the texture averaged back down
    to the original's texels against the original (invented detail shows as
    texel-level drift the 4x4 colour lock lets through);
  - with --apply, replace the fork's texture where ESRGAN scores --margin dB
    or more higher.
<work>/scores.tsv has every score; <work>/review_NN.png shows original | SeedVR2
| ESRGAN for the swaps, largest gain first.
"""

import argparse
import glob
import os
import shutil
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import batch  # noqa: E402
import comfy  # noqa: E402
import texai  # noqa: E402


def tools_used(root):
    """name -> the tool that made it, from every batch log under root"""
    used = {}
    for log in sorted(glob.glob(os.path.join(root, '*', 'log.tsv')), key=os.path.getmtime):
        with open(log, encoding='utf-8') as f:
            for line in f:
                p = line.rstrip('\n').split('\t')
                if len(p) >= 2:
                    used[p[0]] = p[1]
    return used


native_psnr = texai.native_psnr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('fork_dir')
    ap.add_argument('dump_dirs', nargs='+')
    ap.add_argument('--work', default=os.path.join('build', 'texai-regrain'))
    ap.add_argument('--margin', type=float, default=batch.ESRGAN_MARGIN)
    ap.add_argument('--apply', action='store_true')
    a = ap.parse_args()

    rows = {}
    for d in a.dump_dirs:
        for r in batch.read_index(d):
            if os.path.exists(os.path.join(d, r['name'])):
                rows.setdefault(r['name'], (d, r))
    used = tools_used(os.path.dirname(os.path.abspath(a.work)))
    ai = {n: p for n, p in batch.fork_names(a.fork_dir).items()
          if os.sep + 'AI' + os.sep in p or '/AI/' in p}
    cand = []
    for n, p in sorted(ai.items()):
        if n not in rows or used.get(n, 'seedvr2') != 'seedvr2':
            continue
        d, r = rows[n]
        img = Image.open(os.path.join(d, n)).convert('RGBA')
        if r['fmt'] in (batch.FMT_IA, batch.FMT_RGBA) or batch.flat_share(img) >= batch.GRAINY:
            continue
        cand.append((n, p, d, r, img))
    print('%d AI textures, %d grainy SeedVR2 ones with originals' % (len(ai), len(cand)))

    os.makedirs(os.path.join(a.work, 'esrgan'), exist_ok=True)
    os.makedirs(os.path.join(a.work, 'answer'), exist_ok=True)
    scores = []
    for i, (n, p, d, r, img) in enumerate(cand):
        tex = n[:-4]
        out = os.path.join(a.work, 'esrgan', n)
        try:
            if not os.path.exists(out):
                m = batch.manifest_entry(r, img)
                ans = os.path.join(a.work, 'answer', n)
                comfy.framed(batch.upscale('esrgan', comfy.native_input(d, tex, m)), m).save(ans)
                if m['alpha']:
                    batch.alpha_answer(d, tex, m, a.work, n)
                texai.post_one(d, ans, tex, m, os.path.join(a.work, 'final', tex))
                if r['fmt'] == batch.FMT_I:
                    batch.keep_grey(os.path.join(a.work, 'final', tex + '_soft.png'), img)
                os.replace(os.path.join(a.work, 'final', tex + '_soft.png'), out)
            old, new = native_psnr(p, img), native_psnr(out, img)
        except Exception as e:
            print('%s: %s: %s' % (n, type(e).__name__, e))
            continue
        scores.append((new - old, old, new, n, p, d))
        if (i + 1) % 25 == 0:
            print('%d/%d' % (i + 1, len(cand)))
    scores.sort(reverse=True)
    with open(os.path.join(a.work, 'scores.tsv'), 'w', encoding='utf-8') as f:
        for g, old, new, n, p, d in scores:
            f.write('%s\t%.2f\t%.2f\t%.2f\t%s\n' % (n, old, new, g, 'swap' if g >= a.margin else 'keep'))
    swaps = [s for s in scores if s[0] >= a.margin]
    print('%d scored; ESRGAN %.1f+ dB more faithful on %d' % (len(scores), a.margin, len(swaps)))

    cell, per = 150, 24
    for page in range((len(swaps) + per - 1) // per):
        chunk = swaps[page * per:(page + 1) * per]
        sheet = Image.new('RGB', (2 * (3 * cell + 12), ((len(chunk) + 1) // 2) * (cell + 14)), (30, 30, 36))
        dr = ImageDraw.Draw(sheet)
        for k, (g, old, new, n, p, d) in enumerate(chunk):
            x, y = (k % 2) * (3 * cell + 12), (k // 2) * (cell + 14)
            for j, im in enumerate((Image.open(os.path.join(d, n)).resize((cell, cell), Image.NEAREST),
                                    Image.open(p).resize((cell, cell), Image.LANCZOS),
                                    Image.open(os.path.join(a.work, 'esrgan', n)).resize((cell, cell), Image.LANCZOS))):
                sheet.paste(im.convert('RGB'), (x + j * cell, y))
            dr.text((x + 2, y + cell), '%s  %.1f -> %.1f dB' % (n.split('#')[1], old, new), fill=(255, 220, 0))
        sheet.save(os.path.join(a.work, 'review_%02d.png' % page))

    if a.apply:
        for g, old, new, n, p, d in swaps:
            shutil.copyfile(os.path.join(a.work, 'esrgan', n), p)
        print('replaced %d in the fork' % len(swaps))
    return 0


if __name__ == '__main__':
    sys.exit(main())
