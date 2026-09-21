#!/usr/bin/env python3
"""
Rewrite assets/obseg/file_resource_table.inc.c so its data pointers are filled
at runtime instead of by the linker.

Why
---
Each entry currently ends in a link-time symbol:

    {BG_DAM_ALL_P, "bg/bg_dam_all_p.seg", &bg_dam_all_p_seg},

Those symbols only exist when the decomp links a cartridge: every asset is
.incbin'd into the ROM image by assets/obseg/ob_seg.s and the linker assigns it
an address. A port has no such link step, and the per-level segments could not
all be resident at once anyway - 3431 of their internal symbol names collide.
That single column is what leaves 694 symbols undefined.

So the column becomes NULL and the port fills hw_address at startup from the
player's own ROM. The engine already expects this: ob.c branches on
hw_address == 0 and, either way, ends up in decompressdata(), so nothing
downstream changes.

The original symbol is preserved in a trailing comment, because that is the
name the upstream link map uses - it is the join key between this table and the
offsets harvested from a decomp build.

Usage
-----
    python tools/gevr_make_runtime_file_table.py [--check]

--check reports what would change and exits non-zero if the file still binds
symbols, so a build can assert the transform has been applied.
"""

import argparse
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
TABLE = os.path.join(REPO, "assets", "obseg", "file_resource_table.inc.c")

BANNER = """/*
 * PORT NOTE - data pointers are filled at runtime, not by the linker.
 *
 * Upstream this column held &<segment>, a symbol the cartridge link supplied.
 * There is no such link here, so each entry carries NULL and the ROM loader
 * fills hw_address during startup. The original symbol name is kept in the
 * comment on each line; it is the key the upstream link map is read by.
 *
 * Regenerate with tools/gevr_make_runtime_file_table.py after refreshing this
 * file from upstream.
 */
"""

ENTRY_RE = re.compile(
    r"(\{\s*[A-Z0-9_]+\s*,\s*\"[^\"]*\"\s*,\s*)"     # 1: id + name, up to the pointer
    r"(&?[A-Za-z_][A-Za-z0-9_]*|0)"                   # 2: the pointer expression
    r"(\s*\})"                                        # 3: closing brace
)


def transform(src):
    changed = 0

    def repl(m):
        nonlocal changed
        ptr = m.group(2)
        if ptr == "0" or ptr == "NULL":
            return m.group(0)
        changed += 1
        sym = ptr[1:] if ptr.startswith("&") else ptr
        return "%sNULL%s /* %s */" % (m.group(1), m.group(3), sym)

    out = ENTRY_RE.sub(repl, src)
    if changed and not out.lstrip().startswith("/*\n * PORT NOTE"):
        out = BANNER + out
    return out, changed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="report only; exit 1 if symbols are still bound")
    args = ap.parse_args()

    if not os.path.exists(TABLE):
        print("error: %s not found" % TABLE, file=sys.stderr)
        return 2

    src = io.open(TABLE, encoding="utf-8", errors="surrogateescape").read()
    out, changed = transform(src)

    if args.check:
        if changed:
            print("%d entries still bind link-time symbols" % changed)
            return 1
        print("file table is already runtime-filled")
        return 0

    if not changed:
        print("no change: file table is already runtime-filled")
        return 0

    io.open(TABLE, "w", encoding="utf-8", errors="surrogateescape").write(out)
    print("rewrote %d entries in %s" % (changed, os.path.relpath(TABLE, REPO)))
    print("hw_address is now NULL for every file; the ROM loader fills it at startup.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
