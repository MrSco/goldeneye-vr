"""Writes every app icon, the headers' logo and the banner.

    python tools/make_icons.py

The artwork (the user's, 2026-11-09):
- docs/art/app_icon.jpg ("Dam Under Fire": Bond in a headset at the Dam, the
  logo above): the app icon everywhere: both Android APKs' launcher icons,
  the Quest library, the Windows exes (port/pc/package/gevr.ico) and the PC
  window (gevr_icon.rgba).
- docs/art/wordmark.png (the "GoldenEye VR" reticle logo): the smaller logo
  places, the launcher's and the in-game panel's header (gevr_logo.rgba).
- docs/art/banner_source.jpg (the wide scope-eye): docs/banner.png.
- docs/art/tv_icon.jpg ("Frozen Inferno", 2026-11-20: app_icon.jpg's scene,
  wide): the Apple TV's home-screen icon.
Android's monochrome (themed) icon must be a single-colour silhouette: the
logo's reticle, drawn below. Needs Pillow.
"""
import json
import math
import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, 'android', 'app', 'src', 'main', 'res')

GOLD_HI = (255, 214, 120)
GOLD = (222, 168, 58)
GOLD_LO = (150, 98, 24)
NAVY = (14, 22, 40)
BLACK = (4, 6, 10)


def radial_bg(size, inner=NAVY, outer=BLACK):
    img = Image.new('RGB', (size, size), outer)
    px = img.load()
    c = size / 2
    for y in range(size):
        for x in range(size):
            d = min(1.0, math.hypot(x - c, y - c) / (size * 0.72))
            t = d * d
            px[x, y] = tuple(int(inner[i] * (1 - t) + outer[i] * t) for i in range(3))
    return img


def eye_points(cx, cy, w, h, n=160):
    """Almond outline: two arcs meeting in points at the corners."""
    top, bot = [], []
    for i in range(n + 1):
        u = -1 + 2 * i / n
        y = h / 2 * (1 - u * u) ** 0.9
        top.append((cx + u * w / 2, cy - y))
        bot.append((cx + u * w / 2, cy + y))
    return top + bot[::-1]


def gold_gradient(size, mask):
    """Vertical gold gradient through a mask."""
    grad = Image.new('RGB', (size, size))
    d = ImageDraw.Draw(grad)
    for y in range(size):
        t = y / size
        if t < 0.45:
            k = t / 0.45
            col = tuple(int(GOLD_HI[i] * (1 - k) + GOLD[i] * k) for i in range(3))
        else:
            k = (t - 0.45) / 0.55
            col = tuple(int(GOLD[i] * (1 - k) + GOLD_LO[i] * k) for i in range(3))
        d.line([(0, y), (size, y)], fill=col)
    out = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    out.paste(grad, (0, 0), mask)
    return out


def emblem(size, scale=1.0, mono=False):
    """The eye-and-sight emblem on transparency, centred, `scale` of the canvas."""
    S = size * 4
    mask = Image.new('L', (S, S), 0)
    m = ImageDraw.Draw(mask)
    cx = cy = S / 2
    w = S * 0.86 * scale
    h = S * 0.50 * scale
    stroke = S * 0.045 * scale

    # eye outline (outer almond minus inner almond)
    m.polygon(eye_points(cx, cy, w, h), fill=255)
    m.polygon(eye_points(cx, cy, w - stroke * 3.2, h - stroke * 2.0), fill=0)

    # iris: sight ring, ticks and the pupil dot
    r = h * 0.40
    m.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    ri = r - stroke * 0.95
    m.ellipse([cx - ri, cy - ri, cx + ri, cy + ri], fill=0)
    t = stroke * 0.55
    for ang in range(0, 360, 90):
        a = math.radians(ang)
        x0, y0 = cx + math.cos(a) * (r * 0.35), cy + math.sin(a) * (r * 0.35)
        x1, y1 = cx + math.cos(a) * (r * 1.28), cy + math.sin(a) * (r * 1.28)
        m.line([(x0, y0), (x1, y1)], fill=255, width=int(t))
    p = r * 0.16
    m.ellipse([cx - p, cy - p, cx + p, cy + p], fill=255)

    if mono:
        out = Image.new('RGBA', (S, S), (255, 255, 255, 0))
        out.putalpha(mask)
        white = Image.new('RGBA', (S, S), (255, 255, 255, 255))
        white.putalpha(mask)
        return white.resize((size, size), Image.LANCZOS)

    art = gold_gradient(S, mask)
    glow = Image.new('RGBA', (S, S), GOLD + (0,))
    glow.putalpha(mask.filter(ImageFilter.GaussianBlur(S * 0.02)).point(lambda v: int(v * 0.55)))
    out = Image.alpha_composite(glow, art)
    return out.resize((size, size), Image.LANCZOS)


def square_icon(size, scale=0.78):
    bg = radial_bg(size).convert('RGBA')
    return Image.alpha_composite(bg, emblem(size, scale))


def round_icon(size):
    img = square_icon(size)
    mask = Image.new('L', (size * 4, size * 4), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, size * 4 - 1, size * 4 - 1], fill=255)
    img.putalpha(mask.resize((size, size), Image.LANCZOS))
    return img


def banner(w, h):
    img = Image.new('RGB', (w, h), BLACK)
    bg = radial_bg(max(w, h)).resize((w, h))
    img.paste(bg)
    img = img.convert('RGBA')
    e = emblem(int(h * 0.95), 0.92)
    img.alpha_composite(e, (int(w * 0.04), int((h - e.height) / 2)))
    d = ImageDraw.Draw(img)
    title = ImageFont.truetype('C:/Windows/Fonts/bahnschrift.ttf', int(h * 0.20))
    sub = ImageFont.truetype('C:/Windows/Fonts/bahnschrift.ttf', int(h * 0.075))
    x = int(w * 0.04 + e.width * 1.02)
    d.text((x, int(h * 0.26)), 'GoldenEye VR', font=title, fill=GOLD_HI)
    d.text((x + 4, int(h * 0.52)), 'A native Meta Quest port of the N64 decomp', font=sub, fill=(220, 226, 236))
    d.text((x + 4, int(h * 0.64)), 'Stereo VR or a big virtual screen  -  bring your own ROM', font=sub, fill=(150, 160, 178))
    return img


def save(img, *parts):
    path = os.path.join(RES, *parts) if parts[0] != '@' else os.path.join(ROOT, *parts[1:])
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path, optimize=True)
    print('wrote', os.path.relpath(path, ROOT), img.size)


ART = os.path.join(ROOT, 'docs', 'art')


def art_icon(size):
    """The whole square artwork at size x size."""
    src = Image.open(os.path.join(ART, 'app_icon.jpg')).convert('RGB')
    return src.resize((size, size), Image.LANCZOS).convert('RGBA')


def art_round(size):
    img = art_icon(size)
    mask = Image.new('L', (size * 4, size * 4), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, size * 4 - 1, size * 4 - 1], fill=255)
    img.putalpha(mask.resize((size, size), Image.LANCZOS))
    return img


def art_adaptive(size, keep=0.60):
    """An adaptive icon's foreground: the artwork over the part a launcher's
    mask can show (about 72 of the 108 dp), so the whole picture survives."""
    img = Image.new('RGBA', (size, size), (0, 0, 0, 255))
    inner = int(size * keep + 0.5)
    img.paste(art_icon(inner), ((size - inner) // 2, (size - inner) // 2))
    return img


def reticle(size):
    """The logo's reticle as one colour (the monochrome themed icon): a ring,
    the four cross-hair arms through it, on transparent."""
    k = 4
    big = Image.new('L', (size * k, size * k), 0)
    d = ImageDraw.Draw(big)
    c = size * k / 2
    r = size * k * 0.22
    w = max(2, int(size * k * 0.035))
    d.ellipse([c - r, c - r, c + r, c + r], outline=255, width=w)
    arm = size * k * 0.30
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        d.line([(c + dx * r * 0.45, c + dy * r * 0.45), (c + dx * arm, c + dy * arm)], fill=255, width=w)
    alpha = big.resize((size, size), Image.LANCZOS)
    img = Image.new('RGBA', (size, size), (255, 255, 255, 0))
    img.putalpha(alpha)
    return img


def wordmark(w, h):
    """The reticle logo fitted into w x h, transparent around it."""
    src = Image.open(os.path.join(ART, 'wordmark.png')).convert('RGBA')
    k = min(w / src.width, h / src.height)
    logo = src.resize((int(src.width * k + 0.5), int(src.height * k + 0.5)), Image.LANCZOS)
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    img.paste(logo, ((w - logo.width) // 2, (h - logo.height) // 2), logo)
    return img


def write_raw(img, *paths):
    """Raw RGBA (the headers' and the PC window's: no image decoder needed)."""
    for p in paths:
        full = os.path.join(ROOT, *p)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, 'wb') as fh:
            fh.write(img.tobytes())
        print('wrote', os.path.relpath(full, ROOT), img.size)


def art_banner(w, h, title=True):
    """banner_source.jpg scaled to cover w x h, darkened at the bottom for the title."""
    src = Image.open(os.path.join(ART, 'banner_source.jpg')).convert('RGB')
    k = max(w / src.width, h / src.height)
    src = src.resize((int(src.width * k + 0.5), int(src.height * k + 0.5)), Image.LANCZOS)
    x0 = (src.width - w) // 2
    y0 = (src.height - h) // 2
    img = src.crop((x0, y0, x0 + w, y0 + h)).convert('RGBA')
    if not title:
        return img
    shade = Image.new('L', (1, h))
    for y in range(h):
        t = max(0.0, (y / h - 0.55) / 0.45)
        shade.putpixel((0, y), int(215 * t))
    black = Image.new('RGBA', (w, h), (0, 0, 0, 255))
    black.putalpha(shade.resize((w, h)))
    img = Image.alpha_composite(img, black)
    d = ImageDraw.Draw(img)
    big = ImageFont.truetype('C:/Windows/Fonts/bahnschrift.ttf', int(h * 0.13))
    small = ImageFont.truetype('C:/Windows/Fonts/bahnschrift.ttf', int(h * 0.050))
    d.text((int(w * 0.04), int(h * 0.74)), 'GoldenEye VR', font=big, fill=GOLD_HI)
    d.text((int(w * 0.045), int(h * 0.885)),
           'A native Meta Quest port of the N64 decomp  -  bring your own ROM',
           font=small, fill=(225, 230, 238))
    return img


def main():
    dens = {'mdpi': 1, 'hdpi': 1.5, 'xhdpi': 2, 'xxhdpi': 3, 'xxxhdpi': 4}
    for name, k in dens.items():
        legacy = int(48 * k)
        adaptive = int(108 * k)
        save(art_icon(legacy).convert('RGB'), 'mipmap-' + name, 'ic_launcher.png')
        save(art_round(legacy), 'mipmap-' + name, 'ic_launcher_round.png')
        save(art_adaptive(adaptive).convert('RGB'), 'mipmap-' + name, 'ic_launcher_foreground.png')
        save(Image.new('RGB', (adaptive, adaptive), BLACK), 'mipmap-' + name, 'ic_launcher_background.png')
        save(reticle(adaptive), 'mipmap-' + name, 'ic_launcher_monochrome.png')
    save(art_icon(512).convert('RGB'), 'drawable-nodpi', 'ic_launcher_quest.png')
    save(art_icon(512).convert('RGB'), '@', 'android', 'app', 'src', 'main', 'play_store_512.png')
    save(art_banner(1280, 560).convert('RGB'), '@', 'docs', 'banner.png')
    # the headers' logo, 3:1 (vr_launcher.cpp, vr_pause_menu.cpp), and the PC window's icon
    logo = wordmark(384, 128)
    write_raw(logo, ('android', 'app', 'src', 'main', 'assets', 'gevr_logo.rgba'),
              ('port', 'pc', 'package', 'gevr_logo.rgba'))
    logo.save(os.path.join(ROOT, 'tools', 'menu-studio', 'assets', 'launcher-icon.png'))
    write_raw(art_icon(128), ('port', 'pc', 'package', 'gevr_icon.rgba'))
    # the Windows exes' icon (port/pc/package/gevr.rc)
    ico = os.path.join(ROOT, 'port', 'pc', 'package', 'gevr.ico')
    art_icon(256).save(ico, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print('wrote', os.path.relpath(ico, ROOT))
    ios_app_icon()
    tvos_brand_assets()


# iOS: an asset-catalog app icon with every size listed (Xcode 27 refuses a
# single-size one, and App Store validation wants the catalog: CFBundleIconName,
# 120 px iPhone and 152 px iPad icons), opaque.
IOS_ICONS = (('iphone', 20, (2, 3)), ('iphone', 29, (2, 3)), ('iphone', 40, (2, 3)), ('iphone', 60, (2, 3)),
             ('ipad', 20, (1, 2)), ('ipad', 29, (1, 2)), ('ipad', 40, (1, 2)), ('ipad', 76, (1, 2)),
             ('ipad', 83.5, (2,)), ('ios-marketing', 1024, (1,)))


def ios_app_icon():
    import json
    images = []
    for idiom, pt, scales in IOS_ICONS:
        for k in scales:
            px = int(round(pt * k))
            name = 'icon-%d.png' % px
            if not os.path.exists(os.path.join(ROOT, 'port', 'ios', 'Assets.xcassets', 'AppIcon.appiconset', name)):
                save(art_icon(px).convert('RGB'), '@', 'port', 'ios', 'Assets.xcassets', 'AppIcon.appiconset', name)
            images.append({'filename': name, 'idiom': idiom, 'scale': '%dx' % k,
                           'size': '%gx%g' % (pt, pt)})
    info = {'author': 'xcode', 'version': 1}
    base = os.path.join(ROOT, 'port', 'ios', 'Assets.xcassets')
    for path, doc in ((os.path.join(base, 'Contents.json'), {'info': info}),
                      (os.path.join(base, 'AppIcon.appiconset', 'Contents.json'), {'images': images, 'info': info})):
        with open(path, 'w', newline='\n') as f:
            json.dump(doc, f, indent=2)
            f.write('\n')
    print('wrote port/ios/Assets.xcassets/AppIcon.appiconset')


# Apple TV (port/tvos/project.yml): the home screen's icon is a wide (5:3)
# layered stack that tilts under the focus: "Frozen Inferno" (docs/art/
# tv_icon.jpg, 16:9, the user's choice 2026-11-20) filling it behind a clear
# front layer (tvOS asks for two layers or more); before that the square app
# icon on a blur of itself, then the Dam banner. The Top Shelf, shown while
# the icon is focused, is the README's Dam banner (docs/banner.png, the
# user's art since 2026-10-16, not banner_source.jpg). Every size App Store
# validation asks for, at 1x and 2x. The opaque pictures are JPEG, which the
# asset catalog takes, so the repository stays small.
TV_INFO = {'author': 'xcode', 'version': 1}


def tv_json(path, doc):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', newline='\n') as f:
        json.dump(doc, f, indent=2)
        f.write('\n')


def tv_save(img, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if path.endswith('.jpg'):
        img.convert('RGB').save(path, quality=86, optimize=True, progressive=True)
    else:
        img.save(path, optimize=True)
    print('wrote', os.path.relpath(path, ROOT), img.size)


def tv_cover(src, w, h, cx=0.5):
    """src scaled to cover w x h, the overflow cut; cx: the fraction of src's
    width to keep in the middle, where the crop allows."""
    k = max(w / src.width, h / src.height)
    src = src.resize((int(src.width * k + 0.5), int(src.height * k + 0.5)), Image.LANCZOS)
    x0 = min(max(int(src.width * cx - w / 2), 0), src.width - w)
    y0 = (src.height - h) // 2
    return src.crop((x0, y0, x0 + w, y0 + h))


def tv_back(w, h):
    """The stack's back: Frozen Inferno across the whole shape (5:3 keeps 94% of its 16:9 width)."""
    return tv_cover(Image.open(os.path.join(ART, 'tv_icon.jpg')).convert('RGB'), w, h)


def tv_front(w, h):
    """The stack's front: clear (the banner is the picture)."""
    return Image.new('RGBA', (w, h), (0, 0, 0, 0))


def tv_shelf(w, h):
    """The Top Shelf: the README's Dam banner, its logo kept in the crop."""
    return tv_cover(Image.open(os.path.join(ROOT, 'docs', 'banner.png')).convert('RGB'), w, h)


def tvos_brand_assets():
    import shutil
    base = os.path.join(ROOT, 'port', 'tvos', 'Assets.xcassets')
    brand = os.path.join(base, 'App Icon & Top Shelf Image.brandassets')
    shutil.rmtree(brand, ignore_errors=True)
    tv_json(os.path.join(base, 'Contents.json'), {'info': TV_INFO})
    # the icon stacks: (folder, 1x size, scales)
    for stack, (w, h), scales in (('App Icon.imagestack', (400, 240), (1, 2)),
                                  ('App Icon - App Store.imagestack', (1280, 768), (1,))):
        root = os.path.join(brand, stack)
        layers = (('Front', 'front', 'png', tv_front), ('Back', 'back', 'jpg', tv_back))
        for layer, stem, ext, draw in layers:
            images = []
            for k in scales:
                name = '%s%s.%s' % (stem, '' if k == 1 else '@%dx' % k, ext)
                tv_save(draw(w * k, h * k), os.path.join(root, layer + '.imagestacklayer', 'Content.imageset', name))
                images.append({'filename': name, 'idiom': 'tv', 'scale': '%dx' % k})
            tv_json(os.path.join(root, layer + '.imagestacklayer', 'Content.imageset', 'Contents.json'),
                    {'images': images, 'info': TV_INFO})
            tv_json(os.path.join(root, layer + '.imagestacklayer', 'Contents.json'), {'info': TV_INFO})
        tv_json(os.path.join(root, 'Contents.json'),
                {'info': TV_INFO, 'layers': [{'filename': l[0] + '.imagestacklayer'} for l in layers]})
    # the Top Shelf images
    for folder, stem, (w, h) in (('Top Shelf Image.imageset', 'shelf', (1920, 720)),
                                 ('Top Shelf Image Wide.imageset', 'shelf-wide', (2320, 720))):
        images = []
        for k in (1, 2):
            name = '%s%s.jpg' % (stem, '' if k == 1 else '@2x')
            tv_save(tv_shelf(w * k, h * k), os.path.join(brand, folder, name))
            images.append({'filename': name, 'idiom': 'tv', 'scale': '%dx' % k})
        tv_json(os.path.join(brand, folder, 'Contents.json'), {'images': images, 'info': TV_INFO})
    tv_json(os.path.join(brand, 'Contents.json'), {'assets': [
        {'filename': 'App Icon - App Store.imagestack', 'idiom': 'tv', 'role': 'primary-app-icon', 'size': '1280x768'},
        {'filename': 'App Icon.imagestack', 'idiom': 'tv', 'role': 'primary-app-icon', 'size': '400x240'},
        {'filename': 'Top Shelf Image Wide.imageset', 'idiom': 'tv', 'role': 'top-shelf-image-wide', 'size': '2320x720'},
        {'filename': 'Top Shelf Image.imageset', 'idiom': 'tv', 'role': 'top-shelf-image', 'size': '1920x720'},
    ], 'info': TV_INFO})
    print('wrote port/tvos/Assets.xcassets')


if __name__ == '__main__':
    import sys
    if sys.argv[1:] == ['tvos']:   # only the Apple TV assets: python tools/make_icons.py tvos
        tvos_brand_assets()
    else:
        main()
