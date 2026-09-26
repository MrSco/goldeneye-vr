#!/usr/bin/env python3
"""
AI texture pilot harness (see NOTES.md): prepare GoldenEye textures for an
image model (ChatGPT / Gemini), then turn what comes back into pack-ready
textures and a comparison sheet.

    python tools/texai/texai.py prep  <orig_dir> <work_dir> [samples.json]
    python tools/texai/texai.py post  <orig_dir> <work_dir> [tool ...]
    python tools/texai/texai.py sheet <orig_dir> <work_dir> [--final] [tool ...]

<orig_dir> holds the original textures as <tex>.png (RGBA, as the game uses
them). <work_dir> gets:
    in/<tex>.png, in/<tex>.txt   what to upload and the prompt to send
    in/manifest.json             how prep framed each texture
    out/<tool>/<tex>.png         the model's answer, saved by hand or script
    final/<tool>/<tex>_soft.png  the pack texture (colour locked to the original)
    final/<tool>/<tex>_strict.png  locked texel by texel (ghosts if the model moved things)
    compare.png                  original | bicubic | per tool: raw, strict, soft, soft tiled
    compare_final.png            (sheet --final) original | bicubic | each tool's soft texture

Everything in <orig_dir> and <work_dir> is derived from the ROM: keep both
under build/ (gitignored), never in the repo.

post, in order:
  1. crop the answer to the texture's box, resize to SCALE x the original;
  2. drift: average back down and compare with the original (PSNR over
     1x1 and 4x4-texel blocks). A model that moved or redrew things scores low
     (pilot: <= 17 dB at 4x4 was always visibly wrong, >= 20 dB usable);
  3. wrapped axes: spread the edge mismatch over a band so the wrap is continuous;
  4. cut-outs: alpha from the original, refined by the model's own silhouette
     within one texel of it; key colour texels refilled from their neighbours;
  5. back-projection against the original averaged over 1x1 (strict) or 4x4
     (soft) texels: colour and large-scale layout from the original, fine detail
     from the model. soft is the usable one.
"""

import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

SCALE = 8          # pack texture = 8x the original (32x32 -> 256x256)
CANVAS = 1024      # what the models are sent (both answer 1:1 exactly)
KEY = (255, 0, 255)

# Prompt templates that worked in the pilot. Describe the kind of thing, never
# the identity: a wrong description ("a man") is followed over the picture.
PROMPT_HEAD = (
    "Create an image, a new one for this attachment only (ignore any earlier images). "
    "The attached picture is a {w}x{h}-pixel texture{what} from the 1997 Nintendo 64 game GoldenEye 007, "
    "{placed}enlarged with nearest-neighbour scaling, so each visible block is one original pixel. ")
PROMPT_BODY = {
    "detail": (
        "Make a high-resolution remaster of this exact texture as a 1:1 square image. Keep the composition "
        "identical: every element at exactly the same position, size and proportion; the same colours and "
        "brightness; flat and straight-on, with no perspective, no added lighting, shadows, vignette, border or "
        "frame. Replace the blocky pixels with crisp, realistic fine detail. Do not add, remove or move anything, "
        "and add no text or labels."),
    "material": (
        "Make a high-resolution remaster of this exact texture as a 1:1 square image. Keep the composition "
        "identical: the same pattern at exactly the same positions and sizes (the same number of repeats), the "
        "same colours and brightness; flat and straight-on, with no perspective, no added lighting, shadows, "
        "vignette, border or frame. Replace the blocky pixels with crisp, realistic detail of the same material. "
        "Do not add, remove or move anything."),
    "face": (
        "The texture is the front of a character's face, wrapped onto a low-polygon head. Make a high-resolution "
        "remaster of this exact texture as a 1:1 square image: the same person, realistic and photographic with "
        "natural skin detail, keeping the eyes, eyebrows, nose, mouth, chin, hair and hairline at exactly the same "
        "position, size and proportion, with the same skin tone, hair colour and brightness, flat and straight-on, "
        "lit as flatly as the original. Do not add, remove or move anything."),
}
PROMPT_KEY = ("The flat magenta (#FF00FF) is not part of the texture (it is empty/transparent): keep it flat pure "
              "magenta exactly where it is. ")
PROMPT_WRAP = {
    (True, True): " It is a repeating texture: it must tile seamlessly, the left edge continuing into the right "
                  "edge and the top edge into the bottom edge.",
    (True, False): " It repeats horizontally: the left edge must continue seamlessly into the right edge.",
    (False, True): " It repeats vertically: the top edge must continue seamlessly into the bottom edge.",
    (False, False): "",
}


# ---------------------------------------------------------------------------
# prep
# ---------------------------------------------------------------------------

def prep(orig_dir, work, samples_path):
    samples = json.load(open(samples_path))
    out = os.path.join(work, 'in')
    os.makedirs(out, exist_ok=True)
    man = {}
    for s in samples:
        tex = s['tex']
        im = Image.open(os.path.join(orig_dir, tex + '.png')).convert('RGBA')
        if s.get('flip'):
            im = im.transpose(Image.FLIP_TOP_BOTTOM)   # upright for the model
        w, h = im.size
        k = CANVAS // max(w, h)
        big = im.resize((w * k, h * k), Image.NEAREST)
        canvas = Image.new('RGBA', (CANVAS, CANVAS), KEY + (255,))
        x0, y0 = (CANVAS - big.width) // 2, (CANVAS - big.height) // 2
        canvas.alpha_composite(big, (x0, y0))
        canvas.convert('RGB').save(os.path.join(out, tex + '.png'))
        alpha = im.getextrema()[3][0] < 255
        wrap = [bool(v) for v in s.get('wrap', [False, False])]
        m = dict(what=s.get('what', ''), kind=s.get('kind', 'detail'), wrap=wrap, flip=bool(s.get('flip')),
                 w=w, h=h, alpha=alpha,
                 box=[x0 / CANVAS, y0 / CANVAS, (x0 + big.width) / CANVAS, (y0 + big.height) / CANVAS])
        man[tex] = m
        padded = big.size != (CANVAS, CANVAS)
        prompt = PROMPT_HEAD.format(
            w=w, h=h, what=' (%s)' % m['what'] if m['what'] and m['kind'] != 'face' else '',
            placed='placed in the middle of a flat magenta (#FF00FF) square and ' if padded else '')
        if alpha or padded:
            prompt += PROMPT_KEY
        prompt += PROMPT_BODY[m['kind'] if m['kind'] in PROMPT_BODY else 'detail'] + PROMPT_WRAP[tuple(wrap)]
        with open(os.path.join(out, tex + '.txt'), 'w', encoding='utf-8') as f:
            f.write(prompt + '\n')
        print(tex, m['what'], '%dx%d' % (w, h), m['kind'], 'wrap s/t=%d/%d' % tuple(wrap), 'alpha' if alpha else '')
    with open(os.path.join(out, 'manifest.json'), 'w') as f:
        json.dump(man, f, indent=1)


# ---------------------------------------------------------------------------
# post
# ---------------------------------------------------------------------------

def load_orig(orig_dir, tex, m):
    im = Image.open(os.path.join(orig_dir, tex + '.png')).convert('RGBA')
    if m['flip']:
        im = im.transpose(Image.FLIP_TOP_BOTTOM)
    return np.asarray(im).astype(np.float64)


def down(hd, s):
    h, w = hd.shape[0] // s, hd.shape[1] // s
    return hd.reshape(h, s, w, s, -1).mean(axis=(1, 3))


def up(lo, s, wrap):
    """bicubic; wrapped axes pad around, clamped ones repeat the edge"""
    p = 2
    pad = np.pad(lo, ((p, p), (0, 0), (0, 0)), mode='wrap' if wrap[1] else 'edge')
    pad = np.pad(pad, ((0, 0), (p, p), (0, 0)), mode='wrap' if wrap[0] else 'edge')
    chans = []
    for c in range(lo.shape[2]):
        ch = Image.fromarray(pad[:, :, c].astype(np.float32), mode='F')
        chans.append(np.asarray(ch.resize((pad.shape[1] * s, pad.shape[0] * s), Image.BICUBIC)))
    return np.stack(chans, axis=2)[p * s:-p * s, p * s:-p * s]


def psnr(a, b, mask):
    d = ((a - b) ** 2)[mask].mean()
    return 99.0 if d == 0 else 10 * np.log10(255.0 ** 2 / d)


def seam(hd, wrap):
    """the wrap step against the steps at the other texel boundaries (1.0 = no seam)"""
    r = [0.0]
    for axis, on in ((1, wrap[0]), (0, wrap[1])):
        if not on:
            continue
        a = np.moveaxis(hd, axis, 0)
        inside = np.mean([np.abs(a[k] - a[k - 1]).mean() for k in range(SCALE, a.shape[0], SCALE)])
        r.append(np.abs(a[0] - a[-1]).mean() / max(inside, 1e-6))
    return max(r)


def fix_seams(hd, wrap, band):
    """spread each wrapped axis's edge mismatch (beyond an ordinary step) over a band each side"""
    hd = hd.copy()
    for axis, on in ((1, wrap[0]), (0, wrap[1])):
        if not on:
            continue
        a = np.moveaxis(hd, axis, 0)          # a view: the seam is between a[-1] and a[0]
        d = (a[0] - a[-1]) - 0.5 * ((a[1] - a[0]) + (a[-1] - a[-2]))
        for i in range(band):
            k = 0.5 * (1 - i / band)
            a[i] -= d * k
            a[-1 - i] += d * k
    return hd


def bleed(rgb, alpha):
    """copy opaque colours outward into transparent texels, so filtering has no dark fringe"""
    rgb = rgb.copy()
    known = alpha > 0
    for _ in range(64):
        if known.all():
            break
        acc = np.zeros_like(rgb)
        cnt = np.zeros(known.shape)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            k = np.roll(known, (dy, dx), (0, 1))
            acc += np.roll(rgb, (dy, dx), (0, 1)) * k[..., None]
            cnt += k
        grow = (~known) & (cnt > 0)
        rgb[grow] = acc[grow] / cnt[grow][:, None]
        known = known | grow
    return rgb


def morph(mask, r, grow):
    """square dilate (grow) or erode by r pixels"""
    out = mask.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            s = np.roll(mask, (dy, dx), (0, 1))
            out = (out | s) if grow else (out & s)
    return out


def painted(raw):
    """pixels the model painted (not the magenta key colour)"""
    r, g, b = raw[:, :, 0], raw[:, :, 1], raw[:, :, 2]
    return ~((r > 150) & (b > 150) & (g < 110) & (np.abs(r - b) < 90))


def ibp(hd, rgb0, opaque, block, wrap, iters=12):
    """back-projection against the original averaged over block x block texels"""
    tgt = down(rgb0, block) if block > 1 else rgb0
    msk = down(opaque[:, :, None].astype(np.float64), block)[:, :, 0] > 0.5 if block > 1 else opaque
    for _ in range(iters):
        err = tgt - down(hd, SCALE * block)
        err[~msk] = 0
        hd = hd + up(err, SCALE * block, wrap)
    return hd


def post_one(orig_dir, src, tex, m, dst_base):
    orig = load_orig(orig_dir, tex, m)
    W, H = m['w'] * SCALE, m['h'] * SCALE
    wrap = m['wrap']
    ai = Image.open(src).convert('RGB')
    bx = m['box']
    ai = ai.crop((round(bx[0] * ai.width), round(bx[1] * ai.height), round(bx[2] * ai.width), round(bx[3] * ai.height)))
    raw = np.asarray(ai.resize((W, H), Image.LANCZOS)).astype(np.float64)
    rgb0, a0 = orig[:, :, :3], orig[:, :, 3]
    opaque = a0 > 0
    op4 = down(opaque[:, :, None].astype(np.float64), 4)[:, :, 0] > 0.5
    stats = dict(drift=psnr(down(raw, SCALE), rgb0, opaque),
                 drift4=psnr(down(raw, SCALE * 4), down(rgb0, 4), op4) if op4.any() else float('nan'),
                 seam=seam(raw, wrap))
    base = fix_seams(raw, wrap, max(4, W // 16))
    if m['alpha']:
        ahd = up(a0[:, :, None], SCALE, wrap)[:, :, 0]
        binary = set(np.unique(a0)) <= {0.0, 255.0}
        ahd = np.where(ahd >= 128, 255.0, 0.0) if binary else np.clip(ahd, 0, 255)
        # the model's silhouette, but only within a texel of the original's: its
        # finer edge, not a new shape; key-coloured pixels never show
        p = painted(raw)
        o = ahd > 0
        inside = (p & morph(o, SCALE, True)) | morph(o, SCALE, False)
        ahd = np.where(inside, 255.0, 0.0) if binary else ahd * inside
        # key spill: purple-tinted pixels near the silhouette's edge (resampling
        # blends the key colour into the edge); their colour comes from inside
        near_edge = inside & ~morph(inside, 6, False)
        spill = near_edge & (raw[:, :, 0] > raw[:, :, 1] + 30) & (raw[:, :, 2] > raw[:, :, 1] + 30)
        base = bleed(base, np.where(p & inside & ~spill, 255.0, 0.0))
    else:
        ahd = np.full((H, W), 255.0)
    for mode, block in (('strict', 1), ('soft', 4)):
        hd = np.clip(ibp(base, rgb0, opaque, block, wrap), 0, 255)
        stats['seam_' + mode] = seam(hd, wrap)
        if m['alpha']:
            hd = bleed(hd, ahd)
        img = Image.fromarray(np.dstack([hd, ahd]).round().astype(np.uint8), 'RGBA')
        if m['flip']:
            img = img.transpose(Image.FLIP_TOP_BOTTOM)   # back the way the game stores it
        os.makedirs(os.path.dirname(dst_base), exist_ok=True)
        img.save('%s_%s.png' % (dst_base, mode))
    return stats


def tools_in(work, names):
    return names or sorted(os.listdir(os.path.join(work, 'out')))


def post(orig_dir, work, names):
    man = json.load(open(os.path.join(work, 'in', 'manifest.json')))
    print('%-8s %-5s %8s %8s %6s %6s' % ('tool', 'tex', 'drift', 'drift4', 'seam', 'soft'))
    for tool in tools_in(work, names):
        for tex, m in man.items():
            src = os.path.join(work, 'out', tool, tex + '.png')
            if os.path.exists(src):
                s = post_one(orig_dir, src, tex, m, os.path.join(work, 'final', tool, tex))
                wrapped = any(m['wrap'])
                print('%-8s %-5s %8.1f %8.1f %6s %6s' % (tool, tex, s['drift'], s['drift4'],
                      '%.2f' % s['seam'] if wrapped else '-', '%.2f' % s['seam_soft'] if wrapped else '-'))


# ---------------------------------------------------------------------------
# sheet
# ---------------------------------------------------------------------------

CELL = 256
BG = (40, 40, 48)


def fit(im):
    im = im.convert('RGBA')
    k = CELL / max(im.size)
    im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
    bg = Image.new('RGBA', (CELL, CELL), BG + (255,))
    for y in range(0, CELL, 16):
        for x in range(0, CELL, 16):
            if (x // 16 + y // 16) % 2:
                bg.paste((60, 60, 70, 255), (x, y, x + 16, y + 16))
    bg.alpha_composite(im, ((CELL - im.width) // 2, (CELL - im.height) // 2))
    return bg.convert('RGB')


def sheet_compact(orig_dir, work, names):
    """original, bicubic, then each tool's pack texture (soft) - what would ship"""
    man = json.load(open(os.path.join(work, 'in', 'manifest.json')))
    cols = ['original', 'bicubic'] + names
    out = Image.new('RGB', (len(cols) * (CELL + 8), len(man) * (CELL + 24) + 24), BG)
    dr = ImageDraw.Draw(out)
    for c, name in enumerate(cols):
        dr.text((c * (CELL + 8) + 4, 6), name, fill=(255, 255, 255))
    for r, (tex, m) in enumerate(man.items()):
        y = 24 + r * (CELL + 24)
        orig = Image.open(os.path.join(orig_dir, tex + '.png')).convert('RGBA')
        if m['flip']:
            orig = orig.transpose(Image.FLIP_TOP_BOTTOM)
        cells = [orig.resize((orig.width * SCALE, orig.height * SCALE), Image.NEAREST),
                 orig.resize((orig.width * SCALE, orig.height * SCALE), Image.BICUBIC)]
        for t in names:
            f = os.path.join(work, 'final', t, tex + '_soft.png')
            im = Image.open(f).convert('RGBA') if os.path.exists(f) else None
            cells.append(im.transpose(Image.FLIP_TOP_BOTTOM) if im is not None and m['flip'] else im)
        for c, im in enumerate(cells):
            if im is not None:
                out.paste(fit(im), (c * (CELL + 8), y))
        dr.text((4, y + CELL + 4), '%s  %s  %dx%d' % (tex, m['what'], m['w'], m['h']), fill=(255, 220, 0))
    path = os.path.join(work, 'compare_final.png')
    out.save(path)
    print(path)


def sheet(orig_dir, work, names):
    names = tools_in(work, names)
    if names and names[0] == '--final':
        return sheet_compact(orig_dir, work, names[1:] or tools_in(work, []))
    man = json.load(open(os.path.join(work, 'in', 'manifest.json')))
    cols = ['original', 'bicubic'] + sum([[t + ' raw', t + ' strict', t + ' soft', t + ' soft 2x2'] for t in names], [])
    out = Image.new('RGB', (len(cols) * (CELL + 8), len(man) * (CELL + 24) + 24), BG)
    dr = ImageDraw.Draw(out)
    for c, name in enumerate(cols):
        dr.text((c * (CELL + 8) + 4, 6), name, fill=(255, 255, 255))
    for r, (tex, m) in enumerate(man.items()):
        y = 24 + r * (CELL + 24)
        orig = Image.open(os.path.join(orig_dir, tex + '.png')).convert('RGBA')
        if m['flip']:
            orig = orig.transpose(Image.FLIP_TOP_BOTTOM)
        cells = [orig.resize((orig.width * SCALE, orig.height * SCALE), Image.NEAREST),
                 orig.resize((orig.width * SCALE, orig.height * SCALE), Image.BICUBIC)]
        for t in names:
            raw = os.path.join(work, 'out', t, tex + '.png')
            if not os.path.exists(raw):
                cells += [None] * 4
                continue
            got = []
            for mode in ('strict', 'soft'):
                f = Image.open(os.path.join(work, 'final', t, '%s_%s.png' % (tex, mode))).convert('RGBA')
                got.append(f.transpose(Image.FLIP_TOP_BOTTOM) if m['flip'] else f)
            tiled = None
            if any(m['wrap']):
                f = got[1]
                nx, ny = (2 if m['wrap'][0] else 1), (2 if m['wrap'][1] else 1)
                tiled = Image.new('RGBA', (f.width * nx, f.height * ny))
                for k in range(nx * ny):
                    tiled.paste(f, ((k % nx) * f.width, (k // nx) * f.height))
            im = Image.open(raw).convert('RGB')
            b = m['box']
            cells += [im.crop((round(b[0] * im.width), round(b[1] * im.height),
                               round(b[2] * im.width), round(b[3] * im.height)))] + got + [tiled]
        for c, im in enumerate(cells):
            if im is not None:
                out.paste(fit(im), (c * (CELL + 8), y))
        dr.text((4, y + CELL + 4), '%s  %s  %dx%d' % (tex, m['what'], m['w'], m['h']), fill=(255, 220, 0))
    path = os.path.join(work, 'compare.png')
    out.save(path)
    print(path)


def main():
    if len(sys.argv) < 4 or sys.argv[1] not in ('prep', 'post', 'sheet'):
        print(__doc__)
        return 2
    cmd, orig_dir, work = sys.argv[1:4]
    rest = sys.argv[4:]
    if cmd == 'prep':
        prep(orig_dir, work, rest[0] if rest else os.path.join(os.path.dirname(__file__), 'pilot_samples.json'))
    elif cmd == 'post':
        post(orig_dir, work, rest)
    else:
        sheet(orig_dir, work, rest)
    return 0


if __name__ == '__main__':
    sys.exit(main())
