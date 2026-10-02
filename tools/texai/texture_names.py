#!/usr/bin/env python3
"""Build a provenance-aware texture-name map for contextual ComfyUI jobs.

    python tools/texai/texture_names.py build
    python tools/texai/texture_names.py lookup GOLDENEYE#...png

Reads the ROM asset names in assets/images.def and all available local texture
dump indexes. A ROM name is used only when the index identifies that exact ROM
texture. Numeric names and names such as AZTEC_88 do not identify a visible
subject, so they remain unnamed. Optional visual-caption suggestions go in
build/texai-caption-suggestions.jsonl; they are marked unreviewed in the map.
"""

import argparse
import hashlib
import json
import re
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUT = ROOT / 'build' / 'texai-texture-names.json'
DEFAULT_SUGGESTIONS = ROOT / 'build' / 'texai-caption-suggestions.jsonl'
OVERRIDES = Path(__file__).with_name('texture_name_overrides.json')
PILOT = Path(__file__).with_name('pilot_samples.json')
REJECTED = Path(__file__).with_name('rejected.txt')


def rom_names():
    names = []
    for line in (ROOT / 'assets' / 'images.def').read_text(encoding='utf-8').splitlines():
        match = re.match(r'IMAGE\(([^,]+),', line)
        if match:
            names.append(match.group(1))
    return names


def index_paths():
    """Use every local ROM and headset dump, in a stable order."""
    build = ROOT / 'build'
    return sorted(p for p in build.rglob('index.tsv')
                  if any(x.startswith(('texai-rom', 'texture-dump')) for x in p.parts))


def rom_id(row):
    """Older romkeys encoded the ID as stage=-1000-ID; newer has column 11."""
    if len(row) >= 11 and row[9] == '-1':
        return int(row[10])
    if len(row) == 10 and int(row[9]) <= -1000:
        return -int(row[9]) - 1000
    return None


def meaningful_name(name):
    if name.isdigit() or re.fullmatch(r'AZTEC_\d+|\d+_LIGHT', name, re.I):
        return False
    return True


def category(name):
    if re.search(r'(^|_)(FIRE|SMOKE|FLARE|IMPACT|EXPLOSION)', name):
        return 'effect'
    if re.search(r'(TEXT|7SEG|FONT|GLYPH|DIGIT|NUMBER)', name):
        return 'lettering'
    if re.search(r'_(UL|UR|LL|LR|TL|TR|BL|BR|U|L)$', name):
        return 'fragment'
    if re.search(r'(MONITOR|SCREEN|PANEL|DIAL)', name):
        return 'screen'
    if re.search(r'(SIGN|ICON|LOGO|SYMBOL|WARNING)', name):
        return 'symbol'
    if re.search(r'(WALL|FLOOR|BRICK|CONCRETE|GRAVEL|WOOD|STONE|METAL|RUST)', name):
        return 'material'
    return 'object'


def describe_rom_name(name):
    """Offer a conservative subject hint; keep proper nouns and labels visible."""
    if not meaningful_name(name):
        return None
    if name.startswith('MONITOR_'):
        return 'a computer monitor image of ' + name[8:].replace('_', ' ').lower()
    if name.startswith('IMPACT'):
        return 'a game impact mark on ' + name[6:].replace('_', ' ').lower()
    if name.startswith('FIRE_'):
        return 'a frame of a fire effect'
    if name.startswith('SMOKE_'):
        return 'a frame of a smoke effect'
    return name.replace('_', ' ').lower()


def image_priority(path):
    s = str(path).replace('\\', '/').lower()
    if 'texture-dump-live' in s:
        return (0, s)
    m = re.search(r'texture-dump-(\d+)', s)
    if m:
        return (1, -int(m.group(1)), s)
    if 'texai-rom2' in s:
        return (2, s)
    return (3, s)


def load_suggestions(path):
    out = {}
    if path.exists():
        for line in path.read_text(encoding='utf-8').splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            if row.get('filename') and row.get('caption'):
                out[row['filename']] = row['caption'].strip()
    return out


def rejected_notes():
    notes = {}
    for line in REJECTED.read_text(encoding='utf-8').splitlines():
        if not line.strip() or line.startswith('#'):
            continue
        checksum, _, note = line.partition(' ')
        notes[checksum.upper()] = note.strip()
    return notes


def note_subject(note):
    """Extract only the subject before a human review note's failure clause."""
    subject = note.split(',')[0]
    subject = re.split(r'\b(?:turned|grew|garbled|distorted|reinterpreted|smoothed|invented)\b',
                       subject, maxsplit=1)[0]
    subject = re.sub(r'^\d+x\d+\s+', '', subject).strip(' .;')
    return subject if len(subject) >= 4 else None


def build(out=DEFAULT_OUT, suggestions_path=DEFAULT_SUGGESTIONS):
    names = rom_names()
    overrides = json.loads(OVERRIDES.read_text(encoding='utf-8'))
    pilot = {int(s['tex'], 16): s for s in json.loads(PILOT.read_text(encoding='utf-8'))}
    suggestions = load_suggestions(suggestions_path)
    notes = rejected_notes()
    rows = {}
    for index in index_paths():
        for line in index.read_text(encoding='utf-8').splitlines():
            p = line.split('\t')
            if len(p) < 10 or not p[0].lower().endswith('.png'):
                continue
            filename = p[0]
            item = rows.setdefault(filename, {'filename': filename, 'width': int(p[1]),
                                              'height': int(p[2]), 'rom_ids': set(),
                                              'stages': set(), 'images': {}})
            rid = rom_id(p)
            if rid is not None and 0 <= rid < len(names):
                item['rom_ids'].add(rid)
            if int(p[9]) >= 0:
                item['stages'].add(int(p[9]))
            image = index.parent / filename
            if image.exists():
                item['images'][image.relative_to(ROOT).as_posix()] = dict(
                    fmt=int(p[3]), siz=int(p[4]), cms=int(p[5]), cmt=int(p[6]),
                    masks=int(p[7]), maskt=int(p[8]))

    entries = {}
    for filename, item in sorted(rows.items()):
        ids = sorted(item.pop('rom_ids'))
        source_names = sorted({names[t] for t in ids if meaningful_name(names[t])})
        image_metadata = item.pop('images')
        images = sorted(image_metadata, key=image_priority)
        stages = sorted(item.pop('stages'))
        item.update(rom_ids=ids, rom_names=source_names, image=images[0] if images else None,
                    stages=stages, description=None, description_source=None,
                    category='unknown', review_required=True)
        if images:
            item.update(image_metadata[images[0]])
        if len(source_names) == 1:
            item.update(description=describe_rom_name(source_names[0]),
                        description_source='rom-name', category=category(source_names[0]))
        for rid in ids:
            if rid in pilot and pilot[rid].get('what'):
                item.update(description=pilot[rid]['what'], description_source='pilot-sample',
                            category=pilot[rid].get('kind', 'detail'))
                break
        checksum = filename.split('#')[1].upper()
        if checksum in notes:
            item['rejected_reason'] = notes[checksum]
            if not item['description']:
                subject = note_subject(notes[checksum])
                if subject:
                    item.update(description=subject, description_source='review-note')
        if filename in suggestions:
            item['caption_suggestion'] = suggestions[filename]
            if not item['description']:
                item.update(description=suggestions[filename], description_source='vision-suggestion')
        override = overrides.get(filename)
        if override:
            item.update(description=override['description'], description_source='curated',
                        category=override.get('category', item['category']),
                        note=override.get('note'),
                        review_required=override.get('review_required', False))
        if item['category'] in ('effect', 'lettering', 'fragment'):
            item['review_required'] = True
        entries[filename] = item

    # Palette and draw-state variants often have different filenames but the
    # exact same pixels. Share a description only where the pixels really match
    # and all described members agree.
    identical = {}
    for item in entries.values():
        if not item['image']:
            continue
        with Image.open(ROOT / item['image']) as im:
            rgba = im.convert('RGBA')
            digest = hashlib.sha256(rgba.tobytes()).hexdigest()
            identical.setdefault((rgba.size, digest), []).append(item)
    for group in identical.values():
        descriptions = {item['description'] for item in group if item['description']}
        if len(descriptions) != 1:
            continue
        source = next(item for item in group if item['description'])
        for item in group:
            if not item['description']:
                item.update(description=source['description'], description_source='pixel-identical',
                            category=source['category'])
                if source.get('rejected_reason'):
                    item['rejected_reason'] = source['rejected_reason']
    result = {'schema_version': 1, 'entries': entries}
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    sources = {}
    for item in entries.values():
        source = item['description_source'] or 'unmapped'
        sources[source] = sources.get(source, 0) + 1
    print(f'{len(entries)} textures -> {out}')
    print('Descriptions:', ', '.join(f'{name} {count}' for name, count in sorted(sources.items())))
    return result


def prompt(item):
    if not item.get('description'):
        return None
    desc = item['description'].rstrip('.')
    return (f'Remaster this exact low-resolution GoldenEye 007 game texture. Subject hint: {desc}. '
            'Use the reference image to determine the actual shapes; do not invent features from the hint. '
            'Keep every element in the same position, size, and proportion as the reference. '
            'Preserve the original colors, brightness, and any existing symbols or lettering exactly. '
            'Clean up pixelation into crisp detail without adding objects, text, lighting, shadows, '
            'perspective, borders, or decoration.')


def sheet(data, page, page_size, out):
    """Contact sheet for identifying textures which have no trustworthy name."""
    items = [item for item in data['entries'].values()
             if not item['description'] and item['image']]
    items.sort(key=lambda item: item['filename'])
    start = page * page_size
    batch = items[start:start + page_size]
    if not batch:
        raise ValueError(f'page {page} is empty; {len(items)} unnamed textures')
    columns, cell_w, cell_h = 8, 160, 150
    canvas = Image.new('RGB', (columns * cell_w, ((len(batch) + columns - 1) // columns) * cell_h), '#eee')
    draw = ImageDraw.Draw(canvas)
    for index, item in enumerate(batch):
        x, y = (index % columns) * cell_w, (index // columns) * cell_h
        with Image.open(ROOT / item['image']) as opened:
            img = opened.convert('RGBA')
        img.thumbnail((cell_w - 16, cell_h - 32), Image.Resampling.NEAREST)
        bg = Image.new('RGBA', img.size, '#ddd')
        bg.alpha_composite(img)
        canvas.paste(bg.convert('RGB'), (x + (cell_w - img.width) // 2, y + 6))
        draw.text((x + 6, y + cell_h - 22), str(start + index + 1) + '  ' + item['filename'].split('#')[1], fill='#111')
    out.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(out)
    out.with_suffix('.json').write_text(json.dumps(batch, indent=2) + '\n', encoding='utf-8')
    print(f'{len(batch)} of {len(items)} unnamed textures -> {out}')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest='command', required=True)
    b = sub.add_parser('build')
    b.add_argument('--out', type=Path, default=DEFAULT_OUT)
    b.add_argument('--suggestions', type=Path, default=DEFAULT_SUGGESTIONS)
    l = sub.add_parser('lookup')
    l.add_argument('filename')
    l.add_argument('--map', type=Path, default=DEFAULT_OUT)
    s = sub.add_parser('sheet')
    s.add_argument('--map', type=Path, default=DEFAULT_OUT)
    s.add_argument('--page', type=int, default=0)
    s.add_argument('--page-size', type=int, default=64)
    s.add_argument('--out', type=Path)
    a = ap.parse_args()
    if a.command == 'build':
        build(a.out, a.suggestions)
    elif a.command == 'sheet':
        if a.page < 0 or a.page_size < 1:
            ap.error('page must be >= 0 and page-size must be >= 1')
        data = json.loads(a.map.read_text(encoding='utf-8'))
        sheet(data, a.page, a.page_size,
              a.out or ROOT / 'build' / 'texai-contextual' / f'unnamed-{a.page:03d}.png')
    else:
        data = json.loads(a.map.read_text(encoding='utf-8'))
        item = data['entries'].get(Path(a.filename).name)
        if not item:
            ap.error('texture not in map: ' + a.filename)
        print(json.dumps({**item, 'prompt': prompt(item)}, indent=2, ensure_ascii=False))


if __name__ == '__main__':
    main()
