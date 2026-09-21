#!/usr/bin/env python3
"""
Generate the port's ROM file manifest.

What this produces
------------------
port/src/gevr_rom_manifest.c: one {offset, size, compressed} row per entry of
file_resource_table, in the same order, so the loader can fill hw_address by
index without matching names at runtime.

Where the numbers come from
---------------------------
The upstream decomp (github.com/n64decomp/007) extracts its assets from a
retail cartridge, and the coordinates it uses are checked in as
scripts/filelist.u.csv - "offset,size,name,compressed,extract". Those are
exactly the ROM coordinates a port needs, already verified by a project that
rebuilds the cartridge byte-for-byte.

So this joins that list to file_resource_table by filename. Nothing is
reverse-engineered and, importantly, nothing ROM-derived is emitted: the output
is offsets and lengths, the same thing GEVR already ships as
filelist.gevr-images.csv. The player still supplies the bytes.

Region note
-----------
file_resource_table is shared across regions, so a US ROM legitimately has no
data for the PAL-only levels (ear, eld, lee, lip, lue, pam). Those rows get a
zero offset and the loader leaves their hw_address NULL, which is the same
state the table ships in.

Usage
-----
    python tools/gevr_gen_rom_manifest.py [--decomp ../goldeneye-decomp]
"""

import argparse
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
TABLE = os.path.join(REPO, "assets", "obseg", "file_resource_table.inc.c")
OUT = os.path.join(REPO, "port", "src", "gevr_rom_manifest.c")


def read_table():
    """Entries of file_resource_table, with the preprocessor lines that gate them.

    44 of the rows sit inside #ifdef VERSION_EU, so the array the compiler
    actually builds is shorter than the file reads. The manifest has to be
    indexed the same way or every row after the first conditional refers to the
    wrong file - and a loop over the manifest would run off the end of the
    table. So the directives are captured here and re-emitted around the
    matching manifest rows, which keeps the two arrays aligned in any region.

    Returns a list of ("row", ident, name) and ("directive", text) items.
    """
    items = []
    row_re = re.compile(r"\{\s*([A-Z0-9_]+)\s*,\s*\"([^\"]*)\"\s*,")
    for line in io.open(TABLE, encoding="utf-8", errors="surrogateescape"):
        stripped = line.strip()
        if stripped.startswith("#if") or stripped.startswith("#else") or stripped.startswith("#endif"):
            items.append(("directive", stripped))
            continue
        m = row_re.search(line)
        if m:
            items.append(("row", m.group(1), m.group(2)))
    if not any(k == "row" for k, _a, _b in items):
        raise SystemExit("error: could not parse %s" % TABLE)
    return items


def read_filelist(path):
    out = {}
    with io.open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) < 5:
                continue
            offset, size, name, compressed = parts[0], parts[1], parts[2], parts[3]
            key = os.path.splitext(os.path.basename(name))[0]
            try:
                out[key] = (int(offset), int(size), int(compressed))
            except ValueError:
                continue
    return out



# ---------------------------------------------------------------- segments
#
# Besides the file table, the engine romCopy()s whole segments out of the
# cartridge by linker symbol (_fontbankgothicSegmentRomStart and friends,
# produced by ge007.ld's BEGIN_SEG macro). Those symbols do not exist without a
# cartridge link, so the port needs their coordinates too.
#
# Each maps to one or more consecutive payloads in the extract list; where a
# segment spans several, the span runs from the first offset to the end of the
# last. The order here defines the GEVR_SEG_* enum in gevr_rom_segments.h.
SEGMENTS = [
    ("ANIMATION_DATA",    ["animationtable_data"]),
    ("ANIMATION_ENTRIES", ["animationtable_entries"]),
    ("GLOBALIMAGETABLE",  ["ge007.u.29D160.Globalimagetable"]),
    ("RAREWARELOGO",      ["rarewarelogo"]),
    ("FONTDL",            ["ge007.u.117880.jfont_dl"]),
    ("JFONTCHARDATA",     ["ge007.u.117940.jfont_chardata"]),
    ("EFONTCHARDATA",     ["ge007.u.123040.efont_chardata"]),
    ("FONTBANKGOTHIC",    ["fontBankGothic_kerning", "fontBankGothic_fontchartable"]),
    ("FONTZURICHBOLD",    ["fontZurichBold_kerning", "fontZurichBold_fontchartable"]),
]


def segment_spans(files, decomp, region):
    out = []
    for name, parts in SEGMENTS:
        known = [files[k] for k in parts if k in files]
        if len(known) != len(parts):
            out.append((name, 0, 0))
            continue
        start = min(o for o, _s, _c in known)
        end = max(o + s for o, s, _c in known)
        out.append((name, start, end - start))

    # The image bank is thousands of small payloads; take the whole span.
    lo = hi = 0
    imagelist = os.path.join(decomp, "imagelist.%s.csv" % region)
    if os.path.exists(imagelist):
        offs = []
        with io.open(imagelist, encoding="utf-8") as f:
            for line in f:
                bits = line.strip().split(",")
                if len(bits) >= 2 and bits[0].isdigit() and bits[1].isdigit():
                    offs.append((int(bits[0]), int(bits[1])))
        if offs:
            lo = min(o for o, _s in offs)
            hi = max(o + s for o, s in offs)
    out.append(("IMAGES", lo, hi - lo))

    # The gun-barrel intro's RLE backdrop, which title.c reaches through the
    # unknown2 link symbol (assets/romfiles2.s). Kept after IMAGES so the
    # GEVR_SEG_* order in gevr_rom_segments.h stays as it is.
    gb = files.get("ge007.u.2A4D50.usedby7F008DE4")
    out.append(("GUNBARREL", gb[0], gb[1]) if gb else ("GUNBARREL", 0, 0))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", default=os.path.join(os.path.dirname(REPO), "goldeneye-decomp"),
                    help="path to a checkout of n64decomp/007")
    ap.add_argument("--region", default="u", choices=["u", "e", "j"])
    args = ap.parse_args()

    filelist = os.path.join(args.decomp, "scripts", "filelist.%s.csv" % args.region)
    if not os.path.exists(filelist):
        raise SystemExit(
            "error: %s not found.\n"
            "       Clone the upstream decomp next to this repo:\n"
            "         git clone --depth 1 https://github.com/n64decomp/007 goldeneye-decomp"
            % filelist)

    table = read_table()
    files = read_filelist(filelist)

    rows = []
    hit = 0
    for item in table:
        if item[0] == "directive":
            rows.append(("directive", item[1]))
            continue
        _kind, ident, name = item
        if not name:
            rows.append(("row", ident, name, 0, 0, 0))
            continue
        key = os.path.splitext(os.path.basename(name))[0]
        if key in files:
            off, size, comp = files[key]
            rows.append(("row", ident, name, off, size, comp))
            hit += 1
        else:
            rows.append(("row", ident, name, 0, 0, 0))

    named = sum(1 for r in rows if r[0] == "row" and r[2])
    total = sum(1 for r in rows if r[0] == "row")
    guarded = sum(1 for r in rows if r[0] == "directive")
    print("table entries      : %d (%d named)" % (total, named))
    print("resolved from ROM  : %d" % hit)
    print("no data in this ROM: %d  (expected: PAL-only content)" % (named - hit))
    print("preprocessor lines : %d carried over to keep the arrays aligned" % guarded)

    with io.open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("/*\n"
                " * GENERATED by tools/gevr_gen_rom_manifest.py - do not edit.\n"
                " *\n"
                " * Where each file lives inside a region-%s GoldenEye cartridge, in the same\n"
                " * order as file_resource_table so the loader can fill hw_address by index.\n"
                " *\n"
                " * Offsets and lengths only. No cartridge data is stored here; the player\n"
                " * supplies the ROM and these say where to look inside it.\n"
                " *\n"
                " * Source: n64decomp/007, scripts/filelist.%s.csv\n"
                " */\n\n"
                "#include \"gevr_rom_manifest.h\"\n\n"
                "const struct gevrRomFile g_GevrRomFiles[] = {\n" % (args.region, args.region))
        for r in rows:
            if r[0] == "directive":
                f.write("%s\n" % r[1])
                continue
            _kind, ident, name, off, size, comp = r
            f.write("\t{ 0x%08X, %8d, %d },  /* %-28s %s */\n"
                    % (off, size, comp, ident, name))
        f.write("};\n\n"
                "const unsigned int g_GevrRomFileCount =\n"
                "\t\tsizeof(g_GevrRomFiles) / sizeof(g_GevrRomFiles[0]);\n\n")

        segs = segment_spans(files, args.decomp, args.region)
        f.write("/* Whole segments the engine copies by linker symbol. */\n")
        f.write("const struct gevrRomFile g_GevrRomSegments[] = {\n")
        for name, off, size in segs:
            f.write("\t{ 0x%08X, %8d, 0 },  /* GEVR_SEG_%s */\n" % (off, size, name))
        f.write("};\n")

    for name, off, size in segs:
        print("  segment %-20s off 0x%07X size %8d%s"
              % (name, off, size, "" if size else "   <-- NOT RESOLVED"))

    print("wrote %s (%d rows)" % (os.path.relpath(OUT, REPO), len(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
