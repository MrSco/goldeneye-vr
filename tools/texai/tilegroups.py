#!/usr/bin/env python3
"""
Images the game draws as several textures side by side (the briefing folder's
photographs, two 128x64 halves each; murals and signs in strips or quarters),
upscaled whole and cut back into their tiles.

    python tools/texai/tilegroups.py find <index_dir> [--dumps DIR ...] [--images assets/images.def]
    python tools/texai/tilegroups.py seams <fork_dir> [--group N ...]
    python tools/texai/tilegroups.py make <index_dir> <fork_dir> [--group N ...] [--work build/texai-tiles]
                                          [--tool auto] [--include-rejected] [--preview]
    python tools/texai/tilegroups.py sheet <fork_dir> <out.png> [--before build/texai-tiles/before] [--group N ...]

Rare cut big images into tiles that share their edge texels: a photo's upper
half's first stored row is its lower half's last, so the filtered image is
continuous across the join. batch.py upscaled each tile alone, padded by its
guessed wrap flags (romkeys.py: power-of-two sides repeat; the photo halves
are clamped), so SeedVR2 saw each tile's far edge, the photo's white frame,
next to the join, and the seam fix pulled the join toward it: a bright crease
across every briefing photo (2026-10-19).

find: neighbours are textures at most NEAR image numbers apart, of one format,
where one's edge row or column is exactly the other's opposite edge (and not a
flat one); a texture only the dumps have is matched by its edges' texels, in
its own level. A group is laid out from those shared edges and kept only if every
tile gets one place and the tiles cover a rectangle, agreeing where they
overlap. Where a headset dump shows a tile repeating along the axis the group
is tiled on, the group is left to batch.py (Silo's 64x64 pair: its own wrap
matters as much as the join). Writes tilegroups.json, which batch.py skips.

make: each group's composite goes through batch.py's AI step (ai_one) as one
texture, wrapped only on an axis it is not tiled along and its tiles repeat
on, then is cut at texai.SCALE into its tiles, each keeping the shared row.
Every copy of a tile in the fork's batch folders (GOLDENEYE/AI/From ROM,
Mission NN - ..., Title and menus) is replaced, the first old one saved to
<work>/before. Groups with a tile anywhere else (the authors' textures,
AI/Portraits from slice_portraits.py) are skipped, as are groups with a tile
in rejected.txt unless --include-rejected. --preview stops at the composite
(<work>/final/group_N_soft.png) for review.

seams: per group, the mean difference (grey levels) between the HD copies of
each shared edge in the fork: about 0 when the tiles were made together.
"""

import argparse
import json
import os
import re
import shutil
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
GROUPS = os.path.join(HERE, 'tilegroups.json')
NEAR = 4   # image numbers apart: a 2x2's tiles are consecutive


def member_crcs():
    """the texture checksums of every grouped tile (batch.py leaves them alone)"""
    if not os.path.exists(GROUPS):
        return set()
    with open(GROUPS, encoding='utf-8') as f:
        return {t['name'].split('#')[1].upper() for g in json.load(f)['groups'] for t in g['tiles']}


def load_groups(only=None):
    with open(GROUPS, encoding='utf-8') as f:
        groups = json.load(f)['groups']
    return [g for g in groups if not only or g['id'] in only]


def read_rom_index(index_dir):
    """romkeys.py's index.tsv: the dump's columns plus the image number"""
    rows = []
    with open(os.path.join(index_dir, 'index.tsv'), encoding='utf-8') as f:
        for line in f:
            p = line.rstrip('\n').split('\t')
            if len(p) < 11:
                continue
            rows.append(dict(name=p[0], w=int(p[1]), h=int(p[2]), fmt=int(p[3]), siz=int(p[4]), cms=int(p[5]),
                             cmt=int(p[6]), masks=int(p[7]), maskt=int(p[8]), num=int(p[10])))
    return sorted(rows, key=lambda r: r['num'])


def repeats(r):
    """(s, t): an axis repeats when its mask is set and it isn't clamped or mirrored"""
    return r['masks'] != 0 and not (r['cms'] & 3), r['maskt'] != 0 and not (r['cmt'] & 3)


def dumped_wraps(dirs):
    """name -> (s, t) repeat flags as headsets drew it, where every dump agrees"""
    import batch
    seen = {}
    for d in dirs:
        if os.path.exists(os.path.join(d, 'index.tsv')):
            for r in batch.read_index(d):
                seen.setdefault(r['name'], set()).add(repeats(r))
    return {n: v.pop() for n, v in seen.items() if len(v) == 1}


def image_names(path):
    """images.def: image number -> its name"""
    names = []
    with open(path, encoding='utf-8') as f:
        for line in f:
            m = re.match(r'\s*IMAGE\((\w+)', line)
            if m:
                names.append(m.group(1))
    return names


def shared(e1, e2):
    """two edges that are the same texels, and not a flat colour"""
    return np.array_equal(e1, e2) and len(np.unique(e1.reshape(-1, e1.shape[-1]), axis=0)) >= 3


def default_dumps():
    import glob
    return sorted(glob.glob(os.path.join('build', 'texture-dump-*', 'texture-dump'))
                  + glob.glob(os.path.join('build', 'texture-dump-*')))


def dump_rows(dirs, skip):
    """the dumps' textures not in skip (one row each, from the first dump with its file)"""
    import batch
    rows = {}
    for d in dirs:
        if os.path.exists(os.path.join(d, 'index.tsv')):
            for r in batch.read_index(d):
                if r['name'] not in skip and r['name'] not in rows and os.path.exists(os.path.join(d, r['name'])):
                    rows[r['name']] = dict(r, num=None, src=d)
    return sorted(rows.values(), key=lambda r: r['name'])


def source(name, index_dir, dumps):
    """a tile's native texels: the ROM index's file, else a dump's"""
    for d in [index_dir] + list(dumps):
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    raise FileNotFoundError(name)


def find(a):
    rows = read_rom_index(a.index_dir)
    for r in rows:
        r['src'] = a.index_dir
    px = {}

    def tex(r):
        if r['name'] not in px:
            px[r['name']] = np.asarray(Image.open(os.path.join(r['src'], r['name'])).convert('RGBA'))
        return px[r['name']]

    # (a, b, dx, dy): b sits at a's place + (dx, dy), one texel row or column shared
    links = []
    for i, ra in enumerate(rows):
        for rb in rows[i + 1:]:
            if rb['num'] - ra['num'] > NEAR:
                break
            if (rb['fmt'], rb['siz']) != (ra['fmt'], ra['siz']) or not os.path.exists(os.path.join(a.index_dir, rb['name'])):
                continue
            A, B = tex(ra), tex(rb)
            if ra['w'] == rb['w']:
                if shared(A[-1], B[0]):
                    links.append((ra['name'], rb['name'], 0, ra['h'] - 1))
                if shared(A[0], B[-1]):
                    links.append((ra['name'], rb['name'], 0, 1 - rb['h']))
            if ra['h'] == rb['h']:
                if shared(A[:, -1], B[:, 0]):
                    links.append((ra['name'], rb['name'], ra['w'] - 1, 0))
                if shared(A[:, 0], B[:, -1]):
                    links.append((ra['name'], rb['name'], 1 - rb['w'], 0))

    # Textures only a headset dump has (the Dam photo, ones drawn from outside
    # the texture table) have no image number: matched by their edges' texels
    # instead, within one level, leaving out any with a pixel-identical copy
    # (palette variants, which share every edge)
    import hashlib
    dumped = dump_rows(a.dumps, {r['name'] for r in rows})
    whole, starts = {}, {}
    for r in dumped:
        whole.setdefault(hashlib.md5(tex(r).tobytes()).hexdigest(), []).append(r['name'])
    alone = {v[0] for v in whole.values() if len(v) == 1}
    dumped = [r for r in dumped if r['name'] in alone]
    for r in dumped:
        T, k = tex(r), (r['fmt'], r['siz'], r['stage'])
        starts.setdefault(k + ('top', r['w'], T[0].tobytes()), []).append(r)
        starts.setdefault(k + ('left', r['h'], T[:, 0].tobytes()), []).append(r)
    for ra in dumped:
        A, k = tex(ra), (ra['fmt'], ra['siz'], ra['stage'])
        for rb in starts.get(k + ('top', ra['w'], A[-1].tobytes()), ()):
            if rb is not ra and shared(A[-1], tex(rb)[0]):
                links.append((ra['name'], rb['name'], 0, ra['h'] - 1))
        for rb in starts.get(k + ('left', ra['h'], A[:, -1].tobytes()), ()):
            if rb is not ra and shared(A[:, -1], tex(rb)[:, 0]):
                links.append((ra['name'], rb['name'], ra['w'] - 1, 0))
    rows += dumped

    by = {r['name']: r for r in rows}

    def order(n):
        return (by[n]['num'] if by[n]['num'] is not None else 1 << 30, n)
    adj = {}
    for na, nb, dx, dy in links:
        adj.setdefault(na, []).append((nb, dx, dy))
        adj.setdefault(nb, []).append((na, -dx, -dy))
    wraps = dumped_wraps(a.dumps)
    labels = image_names(a.images) if a.images and os.path.exists(a.images) else []
    groups, done = [], set()
    extra = 10000   # ids for groups without image numbers
    for start in sorted(adj, key=order):
        if start in done:
            continue
        pos, todo, bad = {start: (0, 0)}, [start], None
        while todo:
            n = todo.pop()
            for m, dx, dy in adj[n]:
                p = (pos[n][0] + dx, pos[n][1] + dy)
                if m not in pos:
                    pos[m] = p
                    todo.append(m)
                elif pos[m] != p:
                    bad = 'two places for %s' % m
        done |= set(pos)
        nums = sorted(by[n]['num'] for n in pos if by[n]['num'] is not None)
        if nums:
            gid, what = nums[0], '%d-%d' % (nums[0], nums[-1])
        else:
            import batch
            gid, extra = extra, extra + 1
            what = '%d (%s, %s)' % (gid, batch.level_folder(by[start]['stage']), start[10:18])
        x0, y0 = min(p[0] for p in pos.values()), min(p[1] for p in pos.values())
        tiles = [dict(name=n, num=by[n]['num'], x=pos[n][0] - x0, y=pos[n][1] - y0, w=by[n]['w'], h=by[n]['h'])
                 for n in sorted(pos, key=order)]
        W, H = max(t['x'] + t['w'] for t in tiles), max(t['y'] + t['h'] for t in tiles)
        if not bad and len({(t['x'], t['y']) for t in tiles}) < len(tiles):
            bad = 'two tiles in one place'
        if not bad:
            comp, cover = np.zeros((H, W, 4), np.uint8), np.zeros((H, W), bool)
            for t in tiles:
                T, c = tex(by[t['name']]), cover[t['y']:t['y'] + t['h'], t['x']:t['x'] + t['w']]
                if not np.array_equal(comp[t['y']:t['y'] + t['h'], t['x']:t['x'] + t['w']][c], T[c]):
                    bad = 'overlaps disagree'
                comp[t['y']:t['y'] + t['h'], t['x']:t['x'] + t['w']] = T
                c[:] = True
            if not bad and not cover.all():
                bad = 'not a rectangle'
        tiled = (len({t['x'] for t in tiles}) > 1, len({t['y'] for t in tiles}) > 1)
        known = [wraps[t['name']] for t in tiles if t['name'] in wraps]
        if not bad and any(k[ax] for k in known for ax in (0, 1) if tiled[ax]):
            bad = 'its tiles repeat along the tiled axis'
        if bad:
            print('%s: %d tiles, left to batch.py: %s' % (what, len(tiles), bad))
            continue
        # an axis the group isn't tiled along repeats when every tile does: as
        # the headset drew it, else as romkeys guessed
        wrap = [not tiled[ax] and all(wraps.get(t['name'], repeats(by[t['name']]))[ax] for t in tiles)
                for ax in (0, 1)]
        label = ''
        if labels and nums:
            names = [labels[n] for n in nums if n < len(labels)]
            label = os.path.commonprefix(names).rstrip('_') or '/'.join(names)
        f0 = by[start]
        groups.append(dict(id=gid, label=label, fmt=f0['fmt'], siz=f0['siz'], size=[W, H], wrap=wrap,
                           dumped=len(known), tiles=tiles))
        print('%s %-12s %d tiles -> %dx%d, wrap s/t=%d/%d, %d/%d dumped'
              % (what, label, len(tiles), W, H, wrap[0], wrap[1], len(known), len(tiles)))
    with open(GROUPS, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(dict(groups=groups), f, indent=1)
        f.write('\n')
    print('%d groups, %d tiles -> %s' % (len(groups), sum(len(g['tiles']) for g in groups), GROUPS))
    return 0


def batch_folders():
    import batch
    return set(batch.LEVELS.values()) | {'From ROM'}


_files = {}


def copies(fork_dir, name):
    """every file in the fork for a tile: its own name's, and an any-palette one's"""
    import batch
    if fork_dir not in _files:
        idx = _files[fork_dir] = {}
        for root, _, files in os.walk(os.path.join(fork_dir, 'GOLDENEYE')):
            for n in files:
                idx.setdefault(n, []).append(os.path.join(root, n))
    want = [name] + ([batch.wildcard_name(name)] if name.endswith('_ciByRGBA.png') else [])
    return [p for n in want for p in _files[fork_dir].get(n, ())]


def is_batch_copy(fork_dir, path):
    rel = os.path.relpath(path, os.path.join(fork_dir, 'GOLDENEYE', 'AI')).split(os.sep)
    return len(rel) == 2 and (rel[0] in batch_folders() or rel[0].startswith('Other (level'))


def hd_tile(path, t, scale):
    im = Image.open(path).convert('RGBA')
    if im.size != (t['w'] * scale, t['h'] * scale):
        im = im.resize((t['w'] * scale, t['h'] * scale), Image.LANCZOS)
    return np.asarray(im).astype(np.float64)


def joins(g):
    """(a, b, axis): b continues a along the axis (0 = down, 1 = right), one texel shared"""
    out = []
    for a in g['tiles']:
        for b in g['tiles']:
            if a['x'] == b['x'] and a['w'] == b['w'] and b['y'] == a['y'] + a['h'] - 1:
                out.append((a, b, 0))
            if a['y'] == b['y'] and a['h'] == b['h'] and b['x'] == a['x'] + a['w'] - 1:
                out.append((a, b, 1))
    return out


def join_mismatch(tiles, g, scale):
    """the mean difference between the HD copies of each shared edge: [(a, b, grey levels)]"""
    out = []
    for a, b, axis in joins(g):
        if a['name'] not in tiles or b['name'] not in tiles:
            continue
        A, B = tiles[a['name']][:, :, :3], tiles[b['name']][:, :, :3]
        if axis == 0:
            d = np.abs(A[-scale:] - B[:scale]).mean()
        else:
            d = np.abs(A[:, -scale:] - B[:, :scale]).mean()
        out.append((a, b, d))
    return out


def seams(a):
    import texai
    for g in load_groups(a.group):
        tiles = {}
        for t in g['tiles']:
            c = copies(a.fork_dir, t['name'])
            if c:
                tiles[t['name']] = hd_tile(c[0], t, texai.SCALE)
        ds = [d for x, y, d in join_mismatch(tiles, g, texai.SCALE) if x['name'] in tiles and y['name'] in tiles] \
            if len(tiles) > 1 else []
        print('%5d %-12s %d/%d tiles in the fork, joins: %s'
              % (g['id'], g['label'], len(tiles), len(g['tiles']), ' '.join('%.1f' % d for d in ds) or '-'))
    return 0


def composite(index_dir, dumps, g):
    W, H = g['size']
    comp = Image.new('RGBA', (W, H))
    for t in g['tiles']:
        comp.paste(Image.open(source(t['name'], index_dir, dumps)).convert('RGBA'), (t['x'], t['y']))
    return comp


def make(a):
    import batch
    import texai
    S = texai.SCALE
    rej = batch.rejected()
    comp_dir = os.path.join(a.work, 'composite')
    os.makedirs(comp_dir, exist_ok=True)
    os.makedirs(os.path.join(a.work, 'before'), exist_ok=True)
    log = open(os.path.join(a.work, 'log.tsv'), 'a', encoding='utf-8')
    for g in load_groups(a.group):
        what = '%d %s' % (g['id'], g['label'])
        have = {t['name']: copies(a.fork_dir, t['name']) for t in g['tiles']}
        if not any(have.values()):
            print('%s: none of its tiles is in the pack, skipped' % what)
            continue
        other = [p for c in have.values() for p in c if not is_batch_copy(a.fork_dir, p)]
        if other:
            print('%s: skipped, not batch.py\'s: %s' % (what, os.path.relpath(other[0], a.fork_dir)))
            continue
        nrej = [t['name'] for t in g['tiles'] if t['name'].split('#')[1].upper() in rej]
        if nrej and not a.include_rejected:
            print('%s: skipped, %s is in rejected.txt (--include-rejected makes it)' % (what, nrej[0]))
            continue
        name = 'group_%d.png' % g['id']
        img = composite(a.index_dir, a.dumps, g)
        img.save(os.path.join(comp_dir, name))
        W, H = g['size']
        r = dict(name=name, w=W, h=H, fmt=g['fmt'], siz=g['siz'], cms=0, cmt=0,
                 masks=int(g['wrap'][0]), maskt=int(g['wrap'][1]))
        m = batch.manifest_entry(r, img)
        try:
            final, s, tool = batch.ai_one(comp_dir, r, img, m, a.work, a.tool)
        except Exception as e:   # one bad group must not stop the rest
            print('%s: %s: %s' % (what, type(e).__name__, e))
            continue
        hd = Image.open(final).convert('RGBA')
        assert hd.size == (W * S, H * S), (hd.size, W, H)
        if a.preview:
            print('%s: %s, %s, drift4 %.1f (preview, the fork untouched)' % (what, final, tool, s['drift4']))
            continue
        home = os.path.dirname(next(p for c in have.values() for p in c))
        for t in g['tiles']:
            piece = hd.crop((t['x'] * S, t['y'] * S, (t['x'] + t['w']) * S, (t['y'] + t['h']) * S))
            for p in have[t['name']] or [os.path.join(home, t['name'])]:
                keep = os.path.join(a.work, 'before', os.path.basename(p))
                if os.path.exists(p) and not os.path.exists(keep):
                    shutil.copy2(p, keep)
                piece.save(p)
        log.write('%d\t%s\t%s\t%dx%d\t%.1f\t%.1f\t%d\n' % (g['id'], g['label'], tool, W, H, s['drift'], s['drift4'],
                                                         len(g['tiles'])))
        log.flush()
        print('%s: %d tiles, %dx%d, %s, drift4 %.1f' % (what, len(g['tiles']), W, H, tool, s['drift4']))
    return 0


def assemble(paths, g, scale):
    """the group's HD tiles put back together (a missing tile stays black)"""
    W, H = g['size']
    out = Image.new('RGBA', (W * scale, H * scale), (0, 0, 0, 255))
    for t in g['tiles']:
        p = paths.get(t['name'])
        if p and os.path.exists(p):
            out.paste(Image.fromarray(hd_tile(p, t, scale).round().astype(np.uint8), 'RGBA'),
                      (t['x'] * scale, t['y'] * scale))
    return out


def sheet(a):
    """each group's tiles assembled (before | after), at 4x native, alpha dropped"""
    S, show = 8, 4
    cells = []
    for g in load_groups(a.group):
        after = {t['name']: (copies(a.fork_dir, t['name']) or [None])[0] for t in g['tiles']}
        if not any(after.values()):
            continue
        pics = [assemble(after, g, S)]
        if a.before:
            pics.insert(0, assemble({t['name']: os.path.join(a.before, t['name']) for t in g['tiles']}, g, S))
        W, H = g['size']
        pics = [p.convert('RGB').resize((W * show, H * show), Image.LANCZOS) for p in pics]
        cell = Image.new('RGB', (sum(p.width for p in pics) + 8 * (len(pics) - 1), pics[0].height + 16), (40, 40, 40))
        x = 0
        for p in pics:
            cell.paste(p, (x, 16))
            x += p.width + 8
        ImageDraw.Draw(cell).text((2, 2), '%d %s' % (g['id'], g['label']), fill=(255, 255, 0))
        cells.append(cell)
    if not cells:
        print('nothing to show')
        return 1
    cols = 2 if a.before else 4
    cw = max(c.width for c in cells) + 12
    rows = [cells[i:i + cols] for i in range(0, len(cells), cols)]
    out = Image.new('RGB', (cw * cols, sum(max(c.height for c in r) + 12 for r in rows)), (20, 20, 20))
    y = 0
    for r in rows:
        for i, c in enumerate(r):
            out.paste(c, (i * cw + 6, y + 6))
        y += max(c.height for c in r) + 12
    out.save(a.out)
    print('%d groups -> %s (%dx%d)' % (len(cells), a.out, out.width, out.height))
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('find')
    p.add_argument('index_dir')
    p.add_argument('--dumps', nargs='*', default=None)
    p.add_argument('--images', default=os.path.join(HERE, '..', '..', 'assets', 'images.def'))
    p = sub.add_parser('seams')
    p.add_argument('fork_dir')
    p.add_argument('--group', type=int, nargs='*')
    p = sub.add_parser('make')
    p.add_argument('index_dir')
    p.add_argument('fork_dir')
    p.add_argument('--group', type=int, nargs='*')
    p.add_argument('--work', default=os.path.join('build', 'texai-tiles'))
    p.add_argument('--tool', default='auto', choices=('auto', 'seedvr2', 'esrgan', 'esrgan-seedvr2'))
    p.add_argument('--include-rejected', action='store_true')
    p.add_argument('--dumps', nargs='*', default=None, help="where dump-only tiles' texels are (as find's)")
    p.add_argument('--preview', action='store_true', help='make the composites, write nothing to the fork')
    p = sub.add_parser('sheet')
    p.add_argument('fork_dir')
    p.add_argument('out')
    p.add_argument('--before')
    p.add_argument('--group', type=int, nargs='*')
    a = ap.parse_args()
    if getattr(a, 'dumps', 0) is None:
        a.dumps = default_dumps()
    if a.cmd == 'find':
        return find(a)
    return dict(seams=seams, make=make, sheet=sheet)[a.cmd](a)


if __name__ == '__main__':
    sys.exit(main())
