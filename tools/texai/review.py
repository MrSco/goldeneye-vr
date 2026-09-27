#!/usr/bin/env python3
"""
Review pages for a batch.py run: original (nearest) beside the AI texture,
worst drift score first, 40 pairs a page.

    python tools/texai/review.py <dump_dir> <fork_dir> [--work build/texai-batch] [--best]

Writes <work>/review_NN.png. Each label is the drift4 score and the texture's
checksum; the checksum finds the file under <fork>/GOLDENEYE/AI/.
"""

import argparse
import os
import sys

from PIL import Image, ImageDraw

CELL = 160
COLS = 8          # 4 pairs a row
ROWS = 10


def checker(size):
    bg = Image.new('RGBA', size, (60, 60, 70, 255))
    for y in range(0, size[1], 16):
        for x in range(0, size[0], 16):
            if (x // 16 + y // 16) % 2:
                bg.paste((80, 80, 92, 255), (x, y, x + 16, y + 16))
    return bg


def fit(im, nearest):
    im = im.convert('RGBA')
    k = (CELL - 8) / max(im.size)
    im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.NEAREST if nearest else Image.LANCZOS)
    bg = checker((CELL - 8, CELL - 8))
    bg.alpha_composite(im, ((bg.width - im.width) // 2, (bg.height - im.height) // 2))
    return bg.convert('RGB')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dump_dir')
    ap.add_argument('fork_dir')
    ap.add_argument('--work', default=os.path.join('build', 'texai-batch'))
    ap.add_argument('--best', action='store_true', help='best scores first')
    a = ap.parse_args()
    rows = []
    with open(os.path.join(a.work, 'log.tsv'), encoding='utf-8') as f:
        for line in f:
            p = line.rstrip('\n').split('\t')
            if len(p) >= 7:
                rows.append((float(p[5]) if p[5] else 99.0, p[0], p[6]))   # palette siblings have no score
    rows.sort(reverse=a.best)
    per = COLS * ROWS // 2
    for page in range((len(rows) + per - 1) // per):
        sheet = Image.new('RGB', (COLS * CELL, ROWS * (CELL + 14)), (30, 30, 36))
        dr = ImageDraw.Draw(sheet)
        for i, (d4, name, folder) in enumerate(rows[page * per:(page + 1) * per]):
            x, y = (i % (COLS // 2)) * 2 * CELL, (i // (COLS // 2)) * (CELL + 14)
            src = name
            if '#$_' in name:   # any-palette: the dumped variants are one image, show the first
                pre = name.split('#$_')[0] + '#'
                src = next(n for n in sorted(os.listdir(a.dump_dir)) if n.startswith(pre))
            orig = Image.open(os.path.join(a.dump_dir, src))
            ai = Image.open(os.path.join(a.fork_dir, 'GOLDENEYE', 'AI', folder, name))
            sheet.paste(fit(orig, True), (x + 4, y + 4))
            sheet.paste(fit(ai, False), (x + CELL + 4, y + 4))
            dr.text((x + 4, y + CELL - 2), '%.1f  %s %dx%d' % (d4, name.split('#')[1], orig.width, orig.height),
                    fill=(255, 220, 0) if d4 >= 20 else (255, 110, 90))
        path = os.path.join(a.work, 'review_%02d.png' % page)
        sheet.save(path)
        print(path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
