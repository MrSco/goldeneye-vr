#!/usr/bin/env python3
"""
Slice high-resolution -b character portraits from 00-All into 64 GoldenEye GLideN64
quadrant tiles, formatted for GoldenEye-007-HD.

Usage:
    python tools/texai/slice_portraits.py <source_dir> <fork_dir> [--clean-legacy]

Example:
    python tools/texai/slice_portraits.py "C:\\Users\\Occor\\Downloads\\00-All" "C:\\Users\\Occor\\.gemini\\antigravity\\worktrees\\GoldenEye-007-HD\\replace_portrait_b_versions" --clean-legacy
"""

import argparse
import os
import sys
from PIL import Image

# 16 characters in s_mpcharselimages order, mapped to their 00-All source filenames
# and GLideN64 Rice CRC quadrant filenames (UL, UR, LL, LR).
PORTRAITS = [
    {
        'num': 1,
        'name': 'Brosnan',
        'src': '01-b.png',
        'rect': False,
        'tiles': {
            'UL': 'GOLDENEYE#20AEEE9E#4#0_all.png',
            'UR': 'GOLDENEYE#C97BB1FB#4#0_all.png',
            'LL': 'GOLDENEYE#CEEB36E2#4#0_all.png',
            'LR': 'GOLDENEYE#C654D2CA#4#0_all.png',
        }
    },
    {
        'num': 2,
        'name': 'Natalya',
        'src': '02-b.png',
        'rect': False,
        'tiles': {
            'UL': 'GOLDENEYE#61FE0D0B#4#0_all.png',
            'UR': 'GOLDENEYE#98A89AD9#4#0_all.png',
            'LL': 'GOLDENEYE#DEA31D37#4#0_all.png',
            'LR': 'GOLDENEYE#973206B2#4#0_all.png',
        }
    },
    {
        'num': 3,
        'name': 'Trevelyan',
        'src': '03-b.png',
        'rect': False,
        'tiles': {
            'UL': 'GOLDENEYE#676FB7CD#4#0_all.png',
            'UR': 'GOLDENEYE#F1A72182#4#0_all.png',
            'LL': 'GOLDENEYE#9731FCA8#4#0_all.png',
            'LR': 'GOLDENEYE#692BE3C9#4#0_all.png',
        }
    },
    {
        'num': 4,
        'name': 'Xenia',
        'src': '04-b.png',
        'rect': False,
        'tiles': {
            'UL': 'GOLDENEYE#74D5D795#4#0_all.png',
            'UR': 'GOLDENEYE#10A5821D#4#0_all.png',
            'LL': 'GOLDENEYE#6C284244#4#0_all.png',
            'LR': 'GOLDENEYE#15611EAD#4#0_all.png',
        }
    },
    {
        'num': 5,
        'name': 'Ourumov',
        'src': '05-b.png',
        'rect': False,
        'tiles': {
            'UL': 'GOLDENEYE#58BC3061#4#0_all.png',
            'UR': 'GOLDENEYE#452A5E30#4#0_all.png',
            'LL': 'GOLDENEYE#3660997C#4#0_all.png',
            'LR': 'GOLDENEYE#C5046D25#4#0_all.png',
        }
    },
    {
        'num': 6,
        'name': 'Boris',
        'src': '06-b.png',
        'rect': False,
        'tiles': {
            'UL': 'GOLDENEYE#3883AD52#4#0_all.png',
            'UR': 'GOLDENEYE#2A2D9932#4#0_all.png',
            'LL': 'GOLDENEYE#D54224A6#4#0_all.png',
            'LR': 'GOLDENEYE#DC549873#4#0_all.png',
        }
    },
    {
        'num': 7,
        'name': 'Valentin',
        'src': '07-b.png',
        'rect': False,
        'tiles': {
            'UL': 'GOLDENEYE#BEF6F946#4#0_all.png',
            'UR': 'GOLDENEYE#312F5C05#4#0_all.png',
            'LL': 'GOLDENEYE#398B90BA#4#0_all.png',
            'LR': 'GOLDENEYE#FFC7FAA2#4#0_all.png',
        }
    },
    {
        'num': 8,
        'name': 'Mishkin',
        'src': '08-b.png',
        'rect': False,
        'tiles': {
            'UL': 'GOLDENEYE#E3A21420#4#0_all.png',
            'UR': 'GOLDENEYE#6FB88573#4#0_all.png',
            'LL': 'GOLDENEYE#7424950E#4#0_all.png',
            'LR': 'GOLDENEYE#DDEBCC3E#4#0_all.png',
        }
    },
    {
        'num': 9,
        'name': 'May Day',
        'src': '09-b.png',
        'rect': False,
        'tiles': {
            'UL': 'GOLDENEYE#AC889D25#4#0_all.png',
            'UR': 'GOLDENEYE#4B1A2805#4#0_all.png',
            'LL': 'GOLDENEYE#8918F97C#4#0_all.png',
            'LR': 'GOLDENEYE#1A555BAF#4#0_all.png',
        }
    },
    {
        'num': 10,
        'name': 'Jaws',
        'src': '10-b.png',
        'rect': False,
        'tiles': {
            'UL': 'GOLDENEYE#AD063D2A#4#0_all.png',
            'UR': 'GOLDENEYE#159C8001#4#0_all.png',
            'LL': 'GOLDENEYE#10652C25#4#0_all.png',
            'LR': 'GOLDENEYE#82CF1FC8#4#0_all.png',
        }
    },
    {
        'num': 11,
        'name': 'Oddjob',
        'src': '11-b.png',
        'rect': False,
        'tiles': {
            'UL': 'GOLDENEYE#61877003#4#0_all.png',
            'UR': 'GOLDENEYE#E2998A61#4#0_all.png',
            'LL': 'GOLDENEYE#13C333EF#4#0_all.png',
            'LR': 'GOLDENEYE#51BA7FD1#4#0_all.png',
        }
    },
    {
        'num': 12,
        'name': 'Baron Samedi',
        'src': '12-b.png',
        'rect': False,
        'tiles': {
            'UL': 'GOLDENEYE#F1A62FB4#4#0_all.png',
            'UR': 'GOLDENEYE#020A9F1E#4#0_all.png',
            'LL': 'GOLDENEYE#3A5F1481#4#0_all.png',
            'LR': 'GOLDENEYE#1DF348C0#4#0_all.png',
        }
    },
    {
        'num': 13,
        'name': 'Connery',
        'src': '13-b.png',
        'rect': True,
        'tiles': {
            'UL': 'GOLDENEYE#B66EBDAB#4#0_all.png',
            'UR': 'GOLDENEYE#1CCE2CF8#4#0_all.png',
            'LL': 'GOLDENEYE#1B3D0D66#4#0_all.png',
            'LR': 'GOLDENEYE#E10ED58D#4#0_all.png',
        }
    },
    {
        'num': 14,
        'name': 'Moore',
        'src': '14-b.png',
        'rect': True,
        'tiles': {
            'UL': 'GOLDENEYE#50829E34#4#0_all.png',
            'UR': 'GOLDENEYE#91E96DC0#4#0_all.png',
            'LL': 'GOLDENEYE#6394391D#4#0_all.png',
            'LR': 'GOLDENEYE#95BFBFBD#4#0_all.png',
        }
    },
    {
        'num': 15,
        'name': 'Dalton',
        'src': '15-b.png',
        'rect': True,
        'tiles': {
            'UL': 'GOLDENEYE#084EB86C#4#0_all.png',
            'UR': 'GOLDENEYE#21C54D53#4#0_all.png',
            'LL': 'GOLDENEYE#050A9568#4#0_all.png',
            'LR': 'GOLDENEYE#A517FA88#4#0_all.png',
        }
    },
    {
        'num': 16,
        'name': 'Random',
        'src': '16-b.png',
        'rect': False,
        'is_random': True,
        'tiles': {
            'UL': 'GOLDENEYE#B5757386#4#0_all.png',
            'UR': 'GOLDENEYE#50E8F4B9#4#0_all.png',
            'LL': 'GOLDENEYE#27D3189C#4#0_all.png',
            'LR': 'GOLDENEYE#E682F4F8#4#0_all.png',
        }
    },
]


def slice_portrait(img, is_random=False):
    """
    Given a composite PIL Image, resize to target composite dimensions,
    extract the 4 quadrants with 8-pixel seam overlap, and vertically flip
    each quadrant to match N64 Fast3D scanline inversion.
    """
    if is_random:
        # 65x67 per tile -> 520x536 at 8x. Composite is 1032x1064.
        comp_w, comp_h = 1032, 1064
        tile_w, tile_h = 520, 536
        seam_x, seam_y = 512, 528
    else:
        # 65x65 per tile -> 520x520 at 8x. Composite is 1032x1032.
        comp_w, comp_h = 1032, 1032
        tile_w, tile_h = 520, 520
        seam_x, seam_y = 512, 512

    scaled = img.resize((comp_w, comp_h), Image.Resampling.LANCZOS)

    # 4 quadrants:
    # UL: x in [0, tile_w], y in [0, tile_h]
    # UR: x in [seam_x, comp_w], y in [0, tile_h]
    # LL: x in [0, tile_w], y in [seam_y, comp_h]
    # LR: x in [seam_x, comp_w], y in [seam_y, comp_h]
    quads = {
        'UL': scaled.crop((0, 0, tile_w, tile_h)).transpose(Image.FLIP_TOP_BOTTOM),
        'UR': scaled.crop((seam_x, 0, comp_w, tile_h)).transpose(Image.FLIP_TOP_BOTTOM),
        'LL': scaled.crop((0, seam_y, tile_w, comp_h)).transpose(Image.FLIP_TOP_BOTTOM),
        'LR': scaled.crop((seam_x, seam_y, comp_w, comp_h)).transpose(Image.FLIP_TOP_BOTTOM),
    }

    # Convert all to RGBA
    for q in quads:
        quads[q] = quads[q].convert('RGBA')

    return quads


def main():
    parser = argparse.ArgumentParser(description='Slice character portraits into GoldenEye-007-HD tiles')
    parser.add_argument('source_dir', help='Path to 00-All directory')
    parser.add_argument('fork_dir', help='Path to GoldenEye-007-HD root directory')
    parser.add_argument('--clean-legacy', action='store_true', help='Remove legacy duplicate portrait tiles from From ROM/ and Title and menus/')
    args = parser.parse_args()

    portraits_dir = os.path.join(args.fork_dir, 'GOLDENEYE', 'AI', 'Portraits')
    os.makedirs(portraits_dir, exist_ok=True)

    all_tile_filenames = set()

    for item in PORTRAITS:
        src_path = os.path.join(args.source_dir, item['src'])
        if not os.path.exists(src_path):
            print(f"Error: {src_path} not found!")
            return 1

        im = Image.open(src_path)

        # If rectangular (13, 14, 15), center-crop vertically to 1:1 square
        if item.get('rect', False) and im.height > im.width:
            diff = im.height - im.width
            top_crop = diff // 2
            im = im.crop((0, top_crop, im.width, top_crop + im.width))

        quads = slice_portrait(im, is_random=item.get('is_random', False))

        for q_key, tile_img in quads.items():
            tile_filename = item['tiles'][q_key]
            all_tile_filenames.add(tile_filename)
            dest_path = os.path.join(portraits_dir, tile_filename)
            tile_img.save(dest_path, 'PNG')

        print(f"[{item['num']:02d}] {item['name']:14} -> 4 tiles written ({item['src']})")

    print(f"\nSuccessfully generated {len(all_tile_filenames)} portrait quadrant tiles in {portraits_dir}")

    # Clean legacy duplicates if requested
    if args.clean_legacy:
        legacy_dirs = [
            os.path.join(args.fork_dir, 'GOLDENEYE', 'AI', 'From ROM'),
            os.path.join(args.fork_dir, 'GOLDENEYE', 'AI', 'Title and menus'),
        ]
        removed_count = 0
        for ldir in legacy_dirs:
            if not os.path.exists(ldir):
                continue
            for fname in os.listdir(ldir):
                if fname in all_tile_filenames:
                    os.remove(os.path.join(ldir, fname))
                    print(f"Removed legacy duplicate: {os.path.join(ldir, fname)}")
                    removed_count += 1
        print(f"Removed {removed_count} legacy duplicate tiles.")

    # Ensure all 64 filenames are recorded in ge007.tdb
    tdb_path = os.path.join(args.fork_dir, 'GOLDENEYE', 'ge007.tdb')
    if os.path.exists(tdb_path):
        with open(tdb_path, 'r', encoding='utf-8', errors='ignore') as f:
            tdb_lines = f.readlines()
        existing_tdb_keys = {line.split(';')[0].strip().upper() for line in tdb_lines if ';' in line}

        added_tdb = []
        for item in PORTRAITS:
            is_rand = item.get('is_random', False)
            dim_str = '65x67' if is_rand else '65x65'
            for fname in item['tiles'].values():
                base_name = os.path.splitext(fname)[0]
                if base_name.upper() not in existing_tdb_keys:
                    added_tdb.append(f"{base_name};{dim_str}\n")
                    existing_tdb_keys.add(base_name.upper())

        if added_tdb:
            with open(tdb_path, 'a', encoding='utf-8', newline='\n') as f:
                f.writelines(added_tdb)
            print(f"Added {len(added_tdb)} missing entries to ge007.tdb.")
        else:
            print("All entries already present in ge007.tdb.")

    return 0


if __name__ == '__main__':
    sys.exit(main())
