"""The stock hand in a gun model: model.c modelGunHandList().

The rifle bat's hand and a pack mesh gun's draw only the lists of the stock
model that are the hand and sleeve: those under Switches 8-13 and 35, less
the gun pieces a few of GoldenEye's own models file there (s_gunPiecesOnHand).
Both callers use the one rule. With a GoldenEye ROM (argument or GEVR_GE_ROM),
every hand item's model is walked (tools/gevr_model_export.py) and the rule
must keep every list that draws the hand's pictures and nothing that draws
the gun. Reads the ROM; writes nothing.

Usage: python port/tests/test_gun_hand_lists.py [path/to/ge.z64]
"""
import os
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
# The hand and sleeve pictures (tools/handpatch/NOTES.md: 0x701-0x706 in the
# gun models; 0x700 is the fist's).
HAND_TEXTURES = set(range(0x700, 0x707))
# Models whose hand is under none of those switches. The fist's model is the
# hand; the watch items draw the tracked watch arm and no pack mesh
# (gunfire.c gevrStereoWatchItem).
NO_STOCK_HAND = {"GfistZ", "GwatchlaserZ", "GtriggerZ"}


def fail(message):
    print("FAIL: " + message)
    sys.exit(1)


def source(path):
    return (ROOT / path).read_bytes().decode("utf-8", "replace")


def gun_pieces():
    model = source("src/game/model.c")
    table = re.search(r"s_gunPiecesOnHand\[\]\s*=\s*\{(.*?)\};", model, re.S)
    if not table:
        fail("model.c has no s_gunPiecesOnHand table")
    pieces = {(f, int(s)) for f, s in re.findall(r'\{\s*"(\w+)"\s*,\s*(\d+)\s*\}', table.group(1))}
    if not pieces:
        fail("s_gunPiecesOnHand is empty")
    return pieces


def callers():
    """One rule: the bat's hand and the pack mesh gun's hand both ask modelGunHandList."""
    gun = source("src/game/gunfire.c")
    load = re.search(r"static s32 gevrBatHandLoad\(void\)\s*\{(.*?)\n\}", gun, re.S)
    if not load or "modelGunHandList(&s_gevrBatHandHeader, node, (const char *) name)" not in load.group(1):
        fail("gevrBatHandLoad must keep the lists modelGunHandList names")
    if re.search(r"Switches\[35\]", gun):
        fail("gunfire.c has its own test of the hand's switches; use modelGunHandList")
    if not re.search(r"modelDrawGunHand\(&renderdata, &handptr->weaponModel,\s*"
                     r"\(const char \*\)get_ptr_item_text_call_line\(item\)\)", gun):
        fail("the pack mesh gun's hand must be drawn with the item's cartridge file")
    model = source("src/game/model.c")
    if "modelGunHandList(mdl->obj, root, file)" not in model:
        fail("the hand-only draw must keep the lists modelGunHandList names")


def hand_items():
    layout = source("port/src/gevr_romlayout.c")
    files = re.search(r"g_GeItemFiles\[\d+\]\s*=\s*\{(.*?)\};", layout, re.S)
    if not files:
        fail("gevr_romlayout.c has no g_GeItemFiles")
    return re.findall(r'"(\w+)"', files.group(1))


def check_rom(rom_path, pieces):
    sys.path.insert(0, str(ROOT / "tools"))
    import gevr_model_probe as probe
    import gevr_model_export as export

    rom = probe.load_rom(rom_path)
    used = set()
    for name in hand_items():
        switches, _matrices, textures = export.header_counts(name)
        offset, size = probe.manifest_lookup(name)
        model = export.Model(probe.decompress_1172(rom[offset:offset + size]), switches, textures)
        out = model.export(name)
        hand_switch = {}
        for s in list(range(8, 14)) + ([35] if switches > 35 else []):
            if s < switches:
                node = model.ptr(4 * s)
                if node is not None:
                    hand_switch.setdefault(node, s)
        total = kept = 0
        for part in out["parts"]:
            node = part["node"]
            # the nearest of the hand's switches above the list, as modelGunHandList walks
            hand, up = None, node
            while up is not None and hand is None:
                hand = hand_switch.get(up)
                up = model.ptr(up + 8)
            tris = [t for t in out["tris"] if t["node"] == node]
            on_hand = sum(1 for t in tris if t["tex"] in HAND_TEXTURES)
            total += on_hand
            if part["op"] != 4 or hand is None:
                continue
            if (name, hand) in pieces:
                used.add((name, hand))
                if on_hand:
                    fail("%s: list 0x%x under switch %d draws the hand, but is left out as a gun piece"
                         % (name, node, hand))
                continue
            if on_hand != len(tris):
                fail("%s: list 0x%x under switch %d draws %d of the gun's triangles with the hand"
                     % (name, node, hand, len(tris) - on_hand))
            kept += on_hand
        if name in NO_STOCK_HAND:
            if kept:
                fail("%s: expected no stock hand under the switches, kept %d triangles" % (name, kept))
        elif kept != total:
            fail("%s: keeps %d of the hand's %d triangles" % (name, kept, total))
    if used != pieces:
        fail("s_gunPiecesOnHand names lists no model has: %s" % sorted(pieces - used))


def main():
    pieces = gun_pieces()
    callers()
    rom_path = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("GEVR_GE_ROM")
    if not rom_path or not Path(rom_path).is_file():
        print("PASS: one hand rule for the bat and pack mesh guns (SKIP the cartridge models: "
              "pass a GoldenEye ROM or set GEVR_GE_ROM)")
        return
    check_rom(rom_path, pieces)
    print("PASS: one hand rule for the bat and pack mesh guns; on the cartridge it keeps every "
          "hand item's hand and sleeve and none of its gun (%d gun pieces left out)" % len(pieces))


if __name__ == "__main__":
    main()
