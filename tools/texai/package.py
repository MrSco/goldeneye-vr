#!/usr/bin/env python3
"""
Build the launcher's "GoldenEye 007 HD + AI" zip from the texture pack fork.

    python tools/texai/package.py <fork_dir> <authors_hd_release.zip> <out.zip>

The fork's GOLDENEYE/ holds the authors' 4K sources (their master, fixes since
their release included) and our GOLDENEYE/AI/ fill. The zip gets:
  - each of the authors' textures at the size of their own HD release when
    the release has it (their per-texture choice: a quarter of the 4K, half,
    or as is), otherwise a quarter of the 4K but at least 4x native (the size
    ge007.tdb lists);
  - GOLDENEYE/AI/ at the authors' HD scale beside them: their release sizes
    come to about 256 px on the long side (a 64-px wall 4x, 32 px 8x, 16 px
    16x), so each AI texture (8x in the fork) goes to 256 / its native long
    side as a power of two, 4x to 8x (never above the 8x it has);
  - not GOLDENEYE/Hacks/ (textures for ROM hacks, not the game);
  - a readme crediting the authors.
The Mods page (ModManager.java) unpacks the PNGs, paths kept.
"""

import io
import math
import os
import sys
import zipfile

from PIL import Image

README = """GoldenEye 007 HD + AI (GoldenEye VR)

The GoldenEye 007 HD texture pack by intermissionfb (textures) and
GhostlyDark (font textures), from https://github.com/GhostlyDark/GoldenEye-007-HD
and https://evilgames.eu/texture-packs/ge007-hd.htm, at the sizes of its HD
release, with its latest fixes.

GOLDENEYE/AI/ fills textures the pack does not have yet with AI upscales of
the game's own textures (SeedVR2, run locally), made for GoldenEye VR
(https://github.com/MrSco/goldeneye-vr). They never replace the authors'
textures. Fork: https://github.com/MrSco/GoldenEye-007-HD

GLideN64 "Rice" names: GOLDENEYE#<crc>#<fmt>#<siz>[#<palette crc>]_all.png /
_ciByRGBA.png.
"""


AI_SCALE = 8   # batch.py makes every AI texture 8x native


def hd_scale(native_long):
    """The authors' HD release scale for a texture this long: ~256 px, a power of two, 4x..16x."""
    return max(4, min(16, 2 ** round(math.log2(256.0 / native_long))))


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    fork, release, out = sys.argv[1:4]
    root = os.path.join(fork, 'GOLDENEYE')
    rz = zipfile.ZipFile(release)
    rel = {}
    for info in rz.infolist():
        if info.filename.lower().endswith('.png'):
            with Image.open(io.BytesIO(rz.read(info))) as im:
                rel[os.path.basename(info.filename).upper()] = im.size
    tdb = {}
    tdb_prefix = {}
    with open(os.path.join(root, 'ge007.tdb'), encoding='utf-8') as f:
        for line in f:
            if ';' in line:
                n, s = line.strip().split(';')
                dims = tuple(int(v) for v in s.split('x'))
                tdb[n.upper() + '.PNG'] = dims
                parts = n.split('#')
                if len(parts) >= 4:
                    tdb_prefix[(parts[0].upper(), parts[1].upper(), parts[2], parts[3].split('_')[0])] = dims

    rej = set()
    rpath = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rejected.txt')
    if os.path.exists(rpath):
        with open(rpath, encoding='utf-8') as f:
            rej = {l.split()[0].upper() for l in f if l.strip() and not l.startswith('#')}

    counts = dict(authors=0, authors_new=0, resized=0, ai=0, ai_resized=0, rejected=0)
    tmp = out + '.part'
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_STORED) as z:
        z.writestr('readme-ge007-hd-ai.txt', README)
        for dirpath, dirs, files in os.walk(root):
            rp = os.path.relpath(dirpath, fork).replace('\\', '/')
            if rp.startswith('GOLDENEYE/Hacks'):
                continue
            dirs.sort()
            for name in sorted(files):
                if not name.lower().endswith('.png'):
                    continue
                path = os.path.join(dirpath, name)
                arc = rp + '/' + name
                if rp.startswith('GOLDENEYE/AI'):
                    if name.split('#')[1].upper() in rej:   # reviewed out (rejected.txt)
                        counts['rejected'] += 1
                        continue
                    counts['ai'] += 1
                    with Image.open(path) as im:
                        parts = name.split('#')
                        nat = tdb.get(name.upper())
                        if nat is None and len(parts) >= 4:
                            nat = tdb_prefix.get((parts[0].upper(), parts[1].upper(), parts[2], parts[3].split('_')[0]))
                        if nat is None:
                            nat = (im.width // AI_SCALE, im.height // AI_SCALE)
                        s = min(AI_SCALE, hd_scale(max(nat)))
                        size = (nat[0] * s, nat[1] * s)
                        if size[0] >= im.width:
                            z.write(path, arc)   # already at (or under) the HD size
                            continue
                        im.load()
                        im = im.resize(size, Image.LANCZOS)
                        counts['ai_resized'] += 1
                        buf = io.BytesIO()
                        im.save(buf, 'PNG', optimize=False)
                        z.writestr(arc, buf.getvalue())
                    continue
                with Image.open(path) as im:
                    im.load()
                    size = rel.get(name.upper())
                    if size is None:
                        counts['authors_new'] += 1
                        nat = tdb.get(name.upper())
                        w, h = im.width // 4, im.height // 4
                        if nat and w < nat[0] * 4:
                            w, h = nat[0] * 4, nat[1] * 4
                        size = (min(w, im.width), min(h, im.height))
                    counts['authors'] += 1
                    if size != im.size:
                        im = im.resize(size, Image.LANCZOS)
                        counts['resized'] += 1
                    buf = io.BytesIO()
                    im.save(buf, 'PNG', optimize=False)
                    z.writestr(arc, buf.getvalue())
    os.replace(tmp, out)
    print(counts, '%.1f MB' % (os.path.getsize(out) / 2 ** 20))
    return 0


if __name__ == '__main__':
    sys.exit(main())
