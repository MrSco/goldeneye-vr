#!/usr/bin/env python3
"""
Locate GoldenEye's file_resource_table inside a retail ROM and emit a manifest
of name -> (rom offset, size).

Why this exists
---------------
The decomp reaches its assets through link-time symbols: file_resource_table
pairs each file id with a name and a hw_address that the N64 linker filled in.
A port has no linker doing that, so it needs the same information for whichever
ROM the player supplies.

That information is already in the ROM. The table is data, the filenames are
string literals, and the entries point at them. So the table can be found
without hardcoding any offset, which also means this keeps working if the ROM
is not the exact revision assumed here.

How it finds the table
----------------------
A fileentry on the N64 is 12 bytes, big-endian:

    s32 index; char *filename; u8 *hw_address;

The filename pointers are RAM virtual addresses, so they are the ROM offset of
the string plus some constant the loader applies to that segment. That constant
is unknown, but it is the *same* for every entry, so the differences between
consecutive filename pointers do not depend on it at all. Those differences are
exactly the gaps between the strings in the ROM, which we can measure directly.

So: find the strings, build the expected gap sequence, then scan the ROM for a
12-byte-strided run whose filename pointers have the same gaps. A run of a few
dozen matches is not a coincidence. Once found, the constant falls out of any
single entry, and every hw_address can be read straight from the table.

Usage
-----
    python tools/gevr_rom_probe.py <rom.z64> [-o manifest.csv]

Reads the ROM. Writes nothing but the manifest.
"""

import argparse
import array
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
TABLE_SRC = os.path.join(REPO, "assets", "obseg", "file_resource_table.inc.c")

ENTRY_SIZE = 12
Z64_MAGIC = b"\x80\x37\x12\x40"   # big-endian, the layout the game itself uses
V64_MAGIC = b"\x37\x80\x40\x12"   # byteswapped
N64_MAGIC = b"\x40\x12\x37\x80"   # little-endian


# ---------------------------------------------------------------- ROM loading

def load_rom(path):
    """Return ROM bytes in big-endian (.z64) order, whatever order it arrived in."""
    with open(path, "rb") as f:
        data = f.read()

    if len(data) < 0x1000:
        raise SystemExit("error: %s is too small to be a ROM (%d bytes)" % (path, len(data)))

    head = data[:4]
    if head == Z64_MAGIC:
        return data, "z64 (big-endian)"
    if head == V64_MAGIC:
        swapped = bytearray(data)
        swapped[0::2], swapped[1::2] = data[1::2], data[0::2]
        return bytes(swapped), "v64 (byteswapped, converted)"
    if head == N64_MAGIC:
        out = bytearray(len(data))
        for i in range(0, len(data) - 3, 4):
            out[i:i + 4] = data[i:i + 4][::-1]
        return bytes(out), "n64 (little-endian, converted)"

    raise SystemExit(
        "error: %s does not start with an N64 ROM magic (got %s).\n"
        "       Expected a cartridge dump, not a compressed or patched file."
        % (path, head.hex())
    )


# ------------------------------------------------------- the decomp side data

def parse_table_source():
    """Filenames from file_resource_table.inc.c, in table order."""
    if not os.path.exists(TABLE_SRC):
        raise SystemExit("error: cannot find %s" % TABLE_SRC)
    with open(TABLE_SRC, "r", encoding="utf-8", errors="surrogateescape") as f:
        src = f.read()
    rows = re.findall(r"\{\s*([A-Z0-9_]+)\s*,\s*\"([^\"]*)\"\s*,\s*([^}]+)\}", src)
    if not rows:
        raise SystemExit("error: no entries parsed from %s" % TABLE_SRC)
    return [(ident, name) for ident, name, _addr in rows]


def find_strings(rom, names):
    """Map name -> ROM offset, keeping only names that occur exactly once.

    Ambiguous names are dropped rather than guessed at; the fingerprint below
    only needs a few dozen certain ones.
    """
    located = {}
    ambiguous = 0
    for name in names:
        if not name:
            continue
        needle = name.encode("ascii", "replace") + b"\x00"
        first = rom.find(needle)
        if first < 0:
            continue
        if rom.find(needle, first + 1) >= 0:
            ambiguous += 1
            continue
        located[name] = first
    return located, ambiguous


# ------------------------------------------------------------ table detection

def find_table(rom, entries, located, run_len=24):
    """Find the table by matching gaps between consecutive filename pointers.

    Returns (table_offset, delta) where delta = filename_pointer - rom_offset,
    or (None, None).
    """
    # Longest run of consecutive table entries whose strings we located.
    best_run = []
    current = []
    for idx, (_ident, name) in enumerate(entries):
        if name and name in located:
            current.append((idx, name))
            if len(current) > len(best_run):
                best_run = list(current)
        else:
            current = []

    if len(best_run) < 8:
        raise SystemExit(
            "error: only %d consecutive filenames could be located in the ROM.\n"
            "       Not enough to fingerprint the table. Is this a GoldenEye ROM?"
            % len(best_run)
        )

    run = best_run[:run_len]
    gaps = [located[run[i + 1][1]] - located[run[i][1]] for i in range(len(run) - 1)]

    # Read the ROM as big-endian 32-bit words once; scanning byte-by-byte with
    # struct.unpack_from is far too slow over 12 MiB.
    nwords = len(rom) // 4
    words = array.array("I")
    words.frombytes(rom[:nwords * 4])
    if sys.byteorder == "little":
        words.byteswap()

    # An entry is three words, so consecutive entries are three words apart.
    # Deliberately no range check on the pointer values: the only assumption
    # worth making is the gap fingerprint itself, and 20-odd exact gaps in a row
    # is not something that happens by chance.
    first_gap = gaps[0]
    last_start = nwords - (len(gaps) + 1) * 3 - 1

    for k in range(last_start):
        base = words[k + 1]
        if words[k + 4] - base != first_gap:
            continue
        ok = True
        prev = words[k + 4]
        for i in range(1, len(gaps)):
            nxt = words[k + 1 + 3 * (i + 1)]
            if nxt - prev != gaps[i]:
                ok = False
                break
            prev = nxt
        if ok:
            first_idx = run[0][0]
            table_offset = k * 4 - first_idx * ENTRY_SIZE
            delta = base - located[run[0][1]]
            if table_offset >= 0:
                return table_offset, delta

    return None, None


# --------------------------------------------------------------- manifest out

def va_to_rom(va):
    """N64 addresses carry a segment in the top nibble; the low 28 bits are the offset."""
    return va & 0x0FFFFFFF


def read_manifest(rom, entries, table_offset, delta):
    unpack = struct.Struct(">iII").unpack_from
    out = []
    for i in range(len(entries)):
        off = table_offset + i * ENTRY_SIZE
        if off + ENTRY_SIZE > len(rom):
            break
        index, name_ptr, hw = unpack(rom, off)
        name = ""
        if name_ptr:
            s = name_ptr - delta
            if 0 <= s < len(rom):
                end = rom.find(b"\x00", s)
                if 0 <= end - s < 128:
                    name = rom[s:end].decode("ascii", "replace")
        out.append({
            "ident": entries[i][0] if i < len(entries) else "",
            "index": index,
            "name": name,
            "expected_name": entries[i][1] if i < len(entries) else "",
            "hw_address": hw,
            "rom_offset": va_to_rom(hw) if hw else 0,
        })

    # Size is the gap to the next entry's data, which is how obInit derives it.
    for i, row in enumerate(out):
        nxt = out[i + 1]["rom_offset"] if i + 1 < len(out) else 0
        row["size"] = (nxt - row["rom_offset"]) if (nxt and row["rom_offset"] and nxt > row["rom_offset"]) else 0
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom", help="path to your GoldenEye .z64 (not modified)")
    ap.add_argument("-o", "--out", default=None, help="write manifest CSV here")
    args = ap.parse_args()

    rom, order = load_rom(args.rom)
    print("ROM   : %s" % args.rom)
    print("        %d bytes (%.1f MiB), %s" % (len(rom), len(rom) / (1024.0 * 1024.0), order))

    entries = parse_table_source()
    names = [n for _i, n in entries]
    print("Table : %d entries in file_resource_table.inc.c" % len(entries))

    located, ambiguous = find_strings(rom, names)
    print("Strings: %d of %d filenames found in the ROM (%d ambiguous, skipped)"
          % (len(located), len([n for n in names if n]), ambiguous))

    if not located:
        raise SystemExit(
            "\nVERDICT: no filenames found in this ROM.\n"
            "  Either it is not GoldenEye, or this build stores names differently.\n"
            "  The symbol-table approach does not apply; we would need another route."
        )

    table_offset, delta = find_table(rom, entries, located)
    if table_offset is None:
        raise SystemExit(
            "\nVERDICT: filenames are present but no 12-byte-strided table matched them.\n"
            "  The table layout in this ROM differs from struct fileentry.\n"
            "  Worth dumping a few candidate regions by hand before going further."
        )

    print("Found : file_resource_table at ROM offset 0x%06X" % table_offset)
    print("        filename pointers are rom_offset + 0x%08X" % (delta & 0xFFFFFFFF))

    rows = read_manifest(rom, entries, table_offset, delta)
    matched = sum(1 for r in rows if r["name"] and r["name"] == r["expected_name"])
    with_data = sum(1 for r in rows if r["rom_offset"])
    sized = sum(1 for r in rows if r["size"] > 0)

    print("Verify: %d of %d entry names match the decomp table exactly" % (matched, len(rows)))
    print("        %d entries carry a data address, %d have a derivable size" % (with_data, sized))

    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="") as f:
            f.write("ident,index,name,rom_offset,size\n")
            for r in rows:
                f.write("%s,%d,%s,%d,%d\n" % (r["ident"], r["index"], r["name"],
                                              r["rom_offset"], r["size"]))
        print("Wrote : %s" % args.out)

    print()
    if matched >= len(rows) * 0.9 and sized >= len(rows) * 0.8:
        print("VERDICT: table located and verified. The port can build its file index")
        print("         from the player's ROM at startup - no offsets to hardcode.")
        return 0
    print("VERDICT: table located but verification is weak (%d/%d names, %d/%d sizes)."
          % (matched, len(rows), sized, len(rows)))
    print("         Usable as a lead, but read the manifest before trusting it.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
