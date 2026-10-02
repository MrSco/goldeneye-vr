#!/usr/bin/env python3
"""Generate review candidates with FLUX.2 using texture_names.py's prompts.

    python tools/texai/texture_names.py build
    python tools/texai/contextual.py list --limit 20
    python tools/texai/contextual.py run --name GOLDENEYE#...png
    python tools/texai/contextual.py run --limit 10

This writes candidates under build/texai-contextual/. It never changes the
released texture pack. Inspect the original and candidate before selecting one.
"""

import argparse
import json
import shutil
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import batch  # noqa: E402
import comfy  # noqa: E402
import texai  # noqa: E402
import texture_names  # noqa: E402

MAP = ROOT / 'build' / 'texai-texture-names.json'
WORK = ROOT / 'build' / 'texai-contextual'
SKIP_CATEGORIES = {'effect', 'lettering', 'fragment'}


def select(entries, names, include_review_notes, include_suggested):
    wanted = set(names)
    source_order = {'curated': 0, 'pilot-sample': 1, 'pixel-identical': 2,
                    'rom-name': 3, 'review-note': 4, 'vision-suggestion': 5}
    for item in sorted(entries.values(), key=lambda row: (
            source_order.get(row['description_source'], 9), row['filename'])):
        if wanted and item['filename'] not in wanted:
            continue
        source = item['description_source']
        if not item.get('description') or not item.get('image'):
            continue
        if source == 'vision-suggestion' and not include_suggested:
            continue
        if (item.get('rejected_reason') or item['category'] in SKIP_CATEGORIES) and not include_review_notes:
            continue
        yield item


def make_candidate(item, work, force=False):
    filename = item['filename']
    source = ROOT / item['image']
    candidate = work / 'candidates' / filename
    if candidate.exists() and not force:
        print('exists', candidate)
        return
    prompt = texture_names.prompt(item)
    with Image.open(source) as opened:
        orig = opened.convert('RGBA')
    row = {**item, 'name': filename, 'w': orig.width, 'h': orig.height,
           'fmt': item['fmt'], 'siz': item['siz'], 'cms': item['cms'], 'cmt': item['cmt'],
           'masks': item['masks'], 'maskt': item['maskt']}
    m = batch.manifest_entry(row, orig)
    tex = filename[:-4]
    answer, seconds = comfy.flux2(comfy.native_input(str(source.parent), tex, m), prompt, tex)
    framed = work / 'framed' / filename
    framed.parent.mkdir(parents=True, exist_ok=True)
    comfy.framed(answer, m).save(framed)
    base = work / 'post' / tex
    stats = texai.post_one(str(source.parent), str(framed), tex, m, str(base))
    candidate.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(str(base) + '_soft.png', candidate)
    log = work / 'log.jsonl'
    with log.open('a', encoding='utf-8') as f:
        f.write(json.dumps({'filename': filename, 'description': item['description'],
                            'description_source': item['description_source'], 'seconds': seconds,
                            'drift4': stats['drift4'], 'candidate': str(candidate.relative_to(ROOT))}) + '\n')
    print(f'{filename}: {seconds:.1f}s, drift4 {stats["drift4"]:.1f} -> {candidate}', flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('command', choices=('list', 'run'))
    ap.add_argument('--map', type=Path, default=MAP)
    ap.add_argument('--work', type=Path, default=WORK)
    ap.add_argument('--name', action='append', default=[], help='Exact dump filename; repeat for multiple')
    ap.add_argument('--limit', type=int, default=0, help='Maximum entries; 0 means all')
    ap.add_argument('--force', action='store_true', help='Regenerate existing candidates after prompt changes')
    ap.add_argument('--include-review-notes', action='store_true',
                    help='Include earlier rejected textures, effects, lettering and image fragments')
    ap.add_argument('--include-suggested', action='store_true',
                    help='Include unreviewed visual-caption suggestions')
    a = ap.parse_args()
    if not a.map.exists():
        ap.error(f'missing map {a.map}; run texture_names.py build')
    entries = json.loads(a.map.read_text(encoding='utf-8'))['entries']
    if a.name:
        missing = sorted(set(a.name) - set(entries))
        if missing:
            ap.error('not in map: ' + ', '.join(missing))
    selected = list(select(entries, a.name, a.include_review_notes, a.include_suggested))
    if a.name and len(selected) < len(set(a.name)):
        ap.error('some selected textures lack a description or need --include-review-notes/--include-suggested')
    if a.limit:
        selected = selected[:a.limit]
    print(f'{len(selected)} promptable textures selected')
    if a.command == 'list':
        for item in selected:
            print(item['filename'], item['description_source'], item['description'])
        return
    for i, item in enumerate(selected, 1):
        if i % 15 == 0:
            comfy.free()
        try:
            make_candidate(item, a.work, a.force)
        except Exception as e:
            print(f'{item["filename"]}: {type(e).__name__}: {e}', file=sys.stderr, flush=True)


if __name__ == '__main__':
    main()
