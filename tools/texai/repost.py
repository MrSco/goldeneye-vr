#!/usr/bin/env python3
"""
Re-run texai's post step on a batch's saved answers (no GPU), after a fix to
post_one, and overwrite the textures in the fork.

    python tools/texai/repost.py <dump_dir> <fork_dir> [--work build/texai-batch] [--all]

Only textures with transparent texels are redone unless --all. Textures on
rejected.txt, or no longer in the fork, are left alone.
"""

import argparse
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import batch  # noqa: E402
import texai  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dump_dir')
    ap.add_argument('fork_dir')
    ap.add_argument('--work', default=os.path.join('build', 'texai-batch'))
    ap.add_argument('--all', action='store_true')
    a = ap.parse_args()

    index = {r['name']: r for r in batch.read_index(a.dump_dir)}
    rej = batch.rejected()
    done = skipped = 0
    with open(os.path.join(a.work, 'log.tsv'), encoding='utf-8') as f:
        rows = [l.rstrip('\n').split('\t') for l in f if l.strip()]
    for p in rows:
        name, folder = p[0], p[6]
        r = index.get(name)
        dst = os.path.join(a.fork_dir, 'GOLDENEYE', 'AI', folder, name)
        ans = os.path.join(a.work, 'answer', name)
        if r is None or name.split('#')[1].upper() in rej or not os.path.exists(dst) or not os.path.exists(ans):
            skipped += 1
            continue
        img = Image.open(os.path.join(a.dump_dir, name)).convert('RGBA')
        if not a.all and img.getextrema()[3][0] == 255:
            continue
        tex = name[:-4]
        m = batch.manifest_entry(r, img)
        if m['alpha']:
            batch.alpha_answer(a.dump_dir, tex, m, a.work, name)   # ComfyUI, if not saved yet
        texai.post_one(a.dump_dir, ans, tex, m, os.path.join(a.work, 'final', tex))
        if r['fmt'] in (batch.FMT_IA, batch.FMT_I):
            batch.keep_grey(os.path.join(a.work, 'final', tex + '_soft.png'), img)
        os.replace(os.path.join(a.work, 'final', tex + '_soft.png'), dst)
        done += 1
    print('redone %d, skipped %d' % (done, skipped))
    return 0


if __name__ == '__main__':
    sys.exit(main())
