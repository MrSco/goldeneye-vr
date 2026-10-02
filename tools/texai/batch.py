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


def rejected():
    """texture checksums whose AI version was reviewed out (rejected.txt)"""
    path = os.path.join(HERE, 'rejected.txt')
    if not os.path.exists(path):
        return set()
    with open(path, encoding='utf-8') as f:
        return {l.split()[0].upper() for l in f if l.strip() and not l.startswith('#')}


def fork_names(fork_dir):
    """every texture name in the fork -> its path"""
    names = {}
    for root, _, files in os.walk(os.path.join(fork_dir, 'GOLDENEYE')):
        for n in files:
            if n.lower().endswith('.png'):
                names[n] = os.path.join(root, n)
    return names


def sibling_key(r):
    """Palette siblings: a CI texture's texels (and size) under other palette
    checksums. Mostly the very same image: the palette checksum takes in bytes
    the texture does not use, which change from load to load, so a Silo console
    dumps as 5-8 names with identical pixels (see wildcards)."""
    f = r['name'].split('#')
    return (f[1].upper(), r['fmt'], r['siz'], r['w'], r['h']) if len(f) > 4 else None


def wildcard_name(name):
    """GOLDENEYE#CRC#F#S#$_ciByRGBA.png: any palette (gevr_texpack.cpp looks it up
    last, by the texel checksum alone, after the exact names miss)"""
    return '#'.join(name.split('#')[:4]) + '#$_ciByRGBA.png'


def wildcards(dump_dir, groups):
    """exact name -> wildcard name, for the palette variants that are one image:
    the largest set of pixel-identical siblings, when there are two or more.
    One texture then covers them and the variants nobody has dumped yet."""
    import hashlib
    out = {}
    for g in groups.values():
        if len(g) < 2:
            continue
        by = {}
        for r in g:
            with Image.open(os.path.join(dump_dir, r['name'])) as im:
                by.setdefault(hashlib.md5(im.convert('RGBA').tobytes()).hexdigest(), []).append(r)
        big = max(by.values(), key=len)
        if len(big) >= 2:
            for r in big:
                out[r['name']] = wildcard_name(r['name'])
    return out


def make_sibling(dump_dir, src_row, src_hd, r, m, dst):
    """r from the HD texture of its palette sibling src_row, with no AI run: the
    sibling's HD (at 8x) plus the upsampled difference of the two originals, then
    the usual soft colour lock. Where the originals agree the detail is the same
    texel for texel, so the variants do not shimmer as the game swaps them."""
    import numpy as np
    w, h = r['w'], r['h']
    o_src = np.asarray(Image.open(os.path.join(dump_dir, src_row['name'])).convert('RGBA')).astype(np.float64)
    o_sib = np.asarray(Image.open(os.path.join(dump_dir, r['name'])).convert('RGBA')).astype(np.float64)
    hd = Image.open(src_hd).convert('RGBA')
    if hd.size != (w * texai.SCALE, h * texai.SCALE):
        hd = hd.resize((w * texai.SCALE, h * texai.SCALE), Image.LANCZOS)   # the authors' 4K master
    hd = np.asarray(hd).astype(np.float64) + texai.up(o_sib - o_src, texai.SCALE, m['wrap'])
    opaque = o_sib[:, :, 3] > 0
    rgb = texai.ibp(np.clip(hd[:, :, :3], 0, 255), o_sib[:, :, :3], opaque, 4, m['wrap'])
    out = np.dstack([np.clip(rgb, 0, 255), np.clip(hd[:, :, 3], 0, 255)])
    Image.fromarray(out.round().astype(np.uint8), 'RGBA').save(dst)


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
    # 1024 pixels in all, padding included: a 1280-pixel pass (a 128-texel strip's
    # 1024 plus its padding) overflowed the 12 GB card and ran 15+ minutes a texture
    final = min(1024, min(1024, max(256, side * 16)) * max(src.size) / side)
    if tool == 'esrgan':
        return comfy.esrgan(src, 'batch')[0]
    if tool == 'esrgan-seedvr2':
        mid = comfy.esrgan(src, 'batch')[0]
        return comfy.seedvr2(mid, final, 'batch_2')[0]
    mid = comfy.seedvr2(src, 4 * max(src.size), 'batch')[0]
    return comfy.seedvr2(mid, final, 'batch_2')[0]


FMT_RGBA, FMT_IA, FMT_I = 0, 3, 4   # G_IM_FMT_*


def alpha_answer(dump_dir, tex, m, work, name):
    """A cut-out's alpha through Real-ESRGAN on its own (fast, keeps shapes):
    the post step's edge. Saved as <work>/alpha/<name>; its path goes in m."""
    path = os.path.join(work, 'alpha', name)
    if not os.path.exists(path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        comfy.framed(comfy.esrgan(comfy.alpha_input(dump_dir, tex, m), 'alpha')[0], m).save(path)
    m['alpha_answer'] = path
    return path


GRAINY = 0.15   # flat share under this: a grainy, photo-like texture
ESRGAN_MARGIN = 1.5   # dB at native resolution Real-ESRGAN must win by to replace SeedVR2 on one
# (under ~1 dB it was a coin toss by eye: faces, foliage, camouflage looked better from SeedVR2)


def flat_share(img):
    """The share of texels whose 3x3 neighbourhood is flat (luma range under 20).
    Signs, crates, panels ~0.3; the Statue Park statue, stone, camouflage <= 0.1."""
    import numpy as np
    a = np.asarray(img.convert('L')).astype(np.int16)
    h, w = a.shape
    if h < 3 or w < 3:
        return 1.0
    st = np.stack([a[dy:h - 2 + dy, dx:w - 2 + dx] for dy in range(3) for dx in range(3)])
    return float(((st.max(0) - st.min(0)) < 20).mean())


def pick_tool(tool, r, img=None):
    """--tool auto: IA textures (lettering, decals, signatures) and RGBA ones
    (glow sprites) go to Real-ESRGAN, which keeps shapes as drawn - SeedVR2
    redraws letters. So do grainy, photo-like textures: on the Statue Park
    statue's tiles SeedVR2 drew chrome ornaments and faces, a different one
    each tile. Clean graphic art (signs, crates, panels, maps) to SeedVR2."""
    if tool != 'auto':
        return tool
    if r['fmt'] in (FMT_IA, FMT_RGBA):
        return 'esrgan'
    if img is not None and flat_share(img) < GRAINY:
        return 'esrgan'
    return 'seedvr2'


def keep_grey(path, orig):
    """I/IA textures stay grey (the model tints them). An I texture whose alpha
    was its intensity gets the new intensity as alpha too."""
    import numpy as np
    im = np.asarray(Image.open(path).convert('RGBA')).astype(np.float64)
    o = np.asarray(orig.convert('RGBA')).astype(np.int32)
    y = im[:, :, 0] * 0.299 + im[:, :, 1] * 0.587 + im[:, :, 2] * 0.114
    im[:, :, 0] = im[:, :, 1] = im[:, :, 2] = y
    if (o[:, :, 3] == o[:, :, 0]).all():
        im[:, :, 3] = y
    Image.fromarray(np.clip(im, 0, 255).round().astype(np.uint8), 'RGBA').save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dump_dir')
    ap.add_argument('fork_dir')
    ap.add_argument('--tool', default='auto', choices=('auto', 'seedvr2', 'esrgan', 'esrgan-seedvr2'))
    ap.add_argument('--work', default=os.path.join('build', 'texai-batch'))
    ap.add_argument('--min', type=int, default=16)
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--stage', type=int, default=None)
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()

    rows = read_index(a.dump_dir)
    have = fork_names(a.fork_dir)
    rej = rejected()
    todo = [r for r in rows if r['name'] not in have and min(r['w'], r['h']) >= a.min
            and r['name'].split('#')[1].upper() not in rej
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
    siblings = {}   # sibling_key -> dumped rows
    for r in rows:
        if sibling_key(r) and os.path.exists(os.path.join(a.dump_dir, r['name'])):
            siblings.setdefault(sibling_key(r), []).append(r)
    wild = wildcards(a.dump_dir, siblings)
    print('%d palette variants go to %d any-palette ($) textures'
          % (sum(r['name'] in wild for r in todo), len({wild[r['name']] for r in todo if r['name'] in wild})))

    def hd_of(s):
        """the fork's HD texture for a dumped row: its own name's, or its wildcard's"""
        return have.get(s['name']) or have.get(wild.get(s['name'], ''))

    todo.sort(key=lambda r: sibling_key(r) or ('', r['name']))   # a group's first gets the AI, the rest follow
    done = 0
    ai_runs = 0
    t0 = time.time()
    for r in todo:
        if a.limit and done >= a.limit:
            break
        tex = r['name'][:-4]
        out_name = wild.get(r['name'], r['name'])
        if out_name in have:
            continue   # another variant made the wildcard already
        dst_dir = os.path.join(a.fork_dir, 'GOLDENEYE', 'AI', level_folder(r['stage']))
        dst = os.path.join(dst_dir, out_name)
        if os.path.exists(dst):
            continue
        img = Image.open(os.path.join(a.dump_dir, r['name'])).convert('RGBA')
        m = manifest_entry(r, img)
        ans_path = os.path.join(a.work, 'answer', r['name'])
        tool = pick_tool(a.tool, r, img)
        src = next((s for s in siblings.get(sibling_key(r), ()) if s is not r and hd_of(s)), None)
        if src is not None:
            os.makedirs(dst_dir, exist_ok=True)
            make_sibling(a.dump_dir, src, hd_of(src), r, m, dst)
            have[out_name] = dst
            log.write('%s\tsibling:%s\t%d\t%d\t\t\t%s\n' % (out_name, src['name'], r['w'], r['h'],
                                                          level_folder(r['stage'])))
            log.flush()
            done += 1
            print('%d/%d %s %dx%d from palette sibling %s' % (done, len(todo), out_name, r['w'], r['h'], src['name']))
            continue
        # grainy art gets both: Real-ESRGAN only where it is clearly more faithful
        both = tool == 'esrgan' and a.tool == 'auto' and r['fmt'] not in (FMT_IA, FMT_RGBA)
        try:
            made = {}
            for t in (('esrgan', 'seedvr2') if both else (tool,)):
                ai_runs += 1
                if ai_runs % 15 == 0:
                    comfy.free()   # VRAM creeps up over a batch until jobs spill and crawl
                answer = upscale(t, comfy.native_input(a.dump_dir, tex, m))
                path = ans_path if t == tool else ans_path[:-4] + '_' + t + '.png'
                comfy.framed(answer, m).save(path)
                if m['alpha'] and 'alpha_answer' not in m:
                    alpha_answer(a.dump_dir, tex, m, a.work, r['name'])
                base = os.path.join(a.work, 'final', tex + ('' if t == tool else '_' + t))
                s = texai.post_one(a.dump_dir, path, tex, m, base)
                if r['fmt'] in (FMT_IA, FMT_I):
                    keep_grey(base + '_soft.png', img)
                made[t] = (base + '_soft.png', s, texai.native_psnr(base + '_soft.png', img))
            if both:
                gain = made['esrgan'][2] - made['seedvr2'][2]
                tool = 'esrgan' if gain >= ESRGAN_MARGIN else 'seedvr2'
            final, s, _ = made[tool]
        except Exception as e:   # one bad texture must not stop an overnight run
            print('%s: %s: %s' % (r['name'], type(e).__name__, e))
            continue
        os.makedirs(dst_dir, exist_ok=True)
        os.replace(final, dst)
        have[out_name] = dst
        log.write('%s\t%s\t%d\t%d\t%.1f\t%.1f\t%s\n' % (out_name, tool, r['w'], r['h'], s['drift'], s['drift4'],
                                                       level_folder(r['stage'])))
        log.flush()
        done += 1
        el = time.time() - t0
        print('%d/%d %s %dx%d drift4 %.1f (%.1f s each, %.0f min left)'
              % (done, len(todo), r['name'], r['w'], r['h'], s['drift4'], el / done, el / done * (len(todo) - done) / 60))
    return 0


if __name__ == '__main__':
    sys.exit(main())
