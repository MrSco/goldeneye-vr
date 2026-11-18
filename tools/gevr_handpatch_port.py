#!/usr/bin/env python3
"""
Carry the hand patches (tools/handpatch/*.patch.json) over to a ROM hack's
own copies of the hand models.

Why
---
A hack can keep GoldenEye's hands and change only their skin, or rename them:
Mario Characters has a copy of every pistol and knife hand per character
(Gwppk1Z, Gwppk2Z, Gwppk3Z for GwppkZ), the same vertices in the same places,
with each copy's textures in the same slots of its texture table. The game
takes those as they are (gevr_handpatch.c: a renamed copy with every node's
fingerprint uses the patch, its groups' textures by slot). This tool writes
what that needs and what the game cannot work out alone:

- tools/handpatch/textures.json: each patched model's texture table, by slot
  (texture numbers only), for the per-slot lookup;
- for a hack copy that moved or reshaped vertices (Mario's taser/grenade off
  hand keeps 194 of the patch's 195 vertices, under other indices; its watch
  arm keeps GoldenEye's watch in another node), a patch of its own: every
  reference followed to the vertex at the same position, the nodes'
  fingerprints taken from the hack, and the parts with a vertex the hack
  dropped left out. Names and numbers only, as the patches themselves.

Usage
-----
    python tools/gevr_handpatch_port.py ge.z64 [hack.z64 ...]

then python tools/gevr_handpatch_gen.py. A hack's patches are named after its
first copy (Gtaser1Z); its other copies with the same vertices use them too.
"""

import glob
import json
import math
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SRC = os.path.join(HERE, "handpatch")
sys.path.insert(0, HERE)
import gevr_model_export as ex  # noqa: E402
import gevr_model_probe as probe  # noqa: E402

V = 0x80020D90               # the data segment's address
COPY = re.compile(r"^(.*)(\d)Z$")
NUDGE = 4                    # model units a hack may have moved a vertex the patch hangs from
SHARED = 0.9                 # a renamed hack model has this much of one patch part's vertices


def window(rom):
    """The compressed data segment: retail's at 0x21992, Mario Characters' two bytes earlier."""
    for off in (0x21992, 0x21990):
        if rom[off:off + 2] == b"\x11\x72":
            return zlib.decompressobj(-15).decompress(rom[off + 2:])
    raise SystemExit("no data segment at 0x21990/0x21992")


def files(rom):
    """{name: decompressed model file} from the cartridge's own file table."""
    d = window(rom)
    t = next(t for t in range(0, len(d) - 2400, 4)
             if all(struct.unpack_from(">I", d, t + 12 * k)[0] == k for k in range(200)))
    rows = 0
    while struct.unpack_from(">I", d, t + 12 * rows)[0] == rows:
        rows += 1
    out = {}
    for k in range(1, rows - 1):
        _, nptr, addr = struct.unpack_from(">III", d, t + 12 * k)
        end = struct.unpack_from(">I", d, t + 12 * (k + 1) + 8)[0]
        if V <= nptr < V + len(d) and end > addr:
            name = d[nptr - V:d.index(b"\0", nptr - V)].decode("latin-1")
            raw = rom[addr:end]
            if raw[:2] == b"\x11\x72":
                out[name] = zlib.decompressobj(-15).decompress(raw[2:])
    return out


def fnv(block):
    h = 0x811C9DC5
    for xyz in block:
        for c in xyz:
            for b in (c & 0xFF, (c >> 8) & 0xFF):
                h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return "0x%08x" % h


def model(data, ns, nt):
    """(exported model, texture table by slot) with the GoldenEye model's switch and texture counts."""
    out = ex.Model(data, ns, nt).export("x")
    table = [struct.unpack_from(">I", data, ns * 4 + 12 * i)[0] for i in range(nt)]
    return out, table


def same_base(a, b):
    """b is a hack's copy of a (Gwppk2Z of GwppkZ), or both are copies of one model."""
    ma, mb = COPY.match(a), COPY.match(b)
    return bool(mb) and (mb.group(1) + "Z" == a or (ma and ma.group(1) == mb.group(1)))


def node_map(src, dst):
    """Each source node to (destination node, shift): the node holding most of
    its vertex positions, else one holding all of them moved by one shift
    (Mario's watch-arm glove is GoldenEye's hand 254 units along the arm, its
    vertices in another order, with a cuff of its own added)."""
    where = {}
    for p in dst["parts"]:
        for i, xyz in enumerate(p["block_xyz"]):
            where.setdefault(tuple(xyz), []).append((p["node"], i))
    out = {}
    for p in src["parts"]:
        votes = {}
        for xyz in p["block_xyz"]:
            for n, _ in where.get(tuple(xyz), []):
                votes[n] = votes.get(n, 0) + 1
        best = max(votes, key=lambda n: (votes[n], n == p["node"])) if votes else None
        if best is not None and votes[best] >= SHARED * len(p["block_xyz"]):
            out[p["node"]] = (best, (0, 0, 0))
            continue
        moved = shifted(p["block_xyz"], dst)
        if moved:
            out[p["node"]] = moved
        elif best is not None:
            out[p["node"]] = (best, (0, 0, 0))
    return out


def shifted(block, dst):
    """(node, shift) when one dst node holds every one of these positions moved by one shift."""
    want = len(set(map(tuple, block)))
    for q in dst["parts"]:
        have = set(map(tuple, q["block_xyz"]))
        if len(have) < want:
            continue
        x0 = block[0]
        for y in have:
            d = tuple(y[k] - x0[k] for k in range(3))
            if d != (0, 0, 0) and all(tuple(x[k] + d[k] for k in range(3)) in have for x in block):
                return q["node"], d
    return None


def port(patch, src, dst, name):
    """The patch moved onto dst by vertex position; None when nothing carries."""
    nodes = node_map(src, dst)
    sblock = {p["node"]: p["block_xyz"] for p in src["parts"]}
    dblock = {p["node"]: p["block_xyz"] for p in dst["parts"]}
    dmtx = {(v["node"], v["idx"]): v["mtx"] for v in dst["verts"]}

    def ref(node, idx):
        """The hack's vertex at this one's place; one it nudged (Mario's taser hand
        moved one by 2.8 units) by the nearest within NUDGE."""
        n = int(node, 16)
        if n not in nodes:
            return None
        to, d = nodes[n]
        xyz = [sblock[n][idx][k] + d[k] for k in range(3)]
        if idx < len(dblock[to]) and dblock[to][idx] == xyz:
            return to, idx
        hits = [i for i, q in enumerate(dblock[to]) if q == xyz]
        if hits:
            return to, min(hits, key=lambda i: abs(i - idx))
        near = min(range(len(dblock[to])), key=lambda i: math.dist(dblock[to][i], xyz))
        if math.dist(dblock[to][near], xyz) <= NUDGE:
            nudged.append("%s:%d" % (node, idx))
            return to, near
        return None

    nudged = []

    # Each texture by what the hack draws on the same triangles (its texture
    # table is read with GoldenEye's counts, which a renamed model may not have)
    def corners(m):
        return {i: (v["node"], v["idx"]) for i, v in enumerate(m["verts"])}
    sv, dv = corners(src), corners(dst)
    dtri = {frozenset(dv[i] for i in t["v"]): t["tex"] for t in dst["tris"] if t["tex"] is not None}
    votes = {}
    for t in src["tris"]:
        if t["tex"] is None:
            continue
        m = [ref("0x%04x" % sv[i][0], sv[i][1]) for i in t["v"]]
        if None not in m and frozenset(m) in dtri:
            v = votes.setdefault(t["tex"], {})
            v[dtri[frozenset(m)]] = v.get(dtri[frozenset(m)], 0) + 1
    texmap = {s: max(v, key=v.get) for s, v in votes.items()}
    nudged.clear()

    parts, dropped = [], []
    for part in patch["parts"]:
        ok, groups = True, []
        for g in part["groups"]:
            tex = texmap.get(int(g["tex"], 16))
            if tex is None:
                ok = False
                break
            verts = []
            for v in g["verts"]:
                v = dict(v)
                if "mix" in v:
                    mix = []
                    for node, idx, w in v["mix"]:
                        r = ref(node, idx)
                        if r is None:
                            ok = False
                            break
                        mix.append(["0x%04x" % r[0], r[1], w])
                    v["mix"] = mix
                    anchor = (int(v["mix"][0][0], 16), v["mix"][0][1]) if mix else None
                else:
                    r = ref(v["node"], v["ref"])
                    if r is None:
                        ok = False
                    else:
                        v["node"], v["ref"] = "0x%04x" % r[0], r[1]
                    anchor = r
                if not ok:
                    break
                if anchor in dmtx:
                    v["mtx"] = dmtx[anchor]
                verts.append(v)
            if not ok:
                break
            groups.append(dict(g, tex="0x%03x" % tex, verts=verts))
        host = int(part["host"], 16)
        if not ok or host not in nodes:
            dropped.append(part["host"])
            continue
        used = {nodes[int(n, 16)][0] for n in part["nodes"] if int(n, 16) in nodes}
        parts.append({
            "host": "0x%04x" % nodes[host][0],
            "nodes": {"0x%04x" % n: {"numvtx": len(dblock[n]), "fingerprint": fnv(dblock[n])} for n in sorted(used)},
            "groups": groups,
        })
    if not parts:
        return None, dropped
    note = ("%s's patch carried over by vertex position (tools/gevr_handpatch_port.py)"
            % patch["model"])
    if nudged:
        note += "; nearest vertex within %d units for %s" % (NUDGE, " ".join(sorted(set(nudged))))
    return {"model": name, "ported_from": patch["model"], "note": note, "parts": parts}, dropped


def shared(patch, src, dst):
    """The most of one patch part's vertex positions dst has: a renamed copy that
    kept some of the model (Mario's watch arm keeps GoldenEye's watch, not its hand)."""
    blocks = {p["node"]: p["block_xyz"] for p in src["parts"]}
    have = {tuple(x) for p in dst["parts"] for x in p["block_xyz"]}
    best = 0.0
    for part in patch["parts"]:
        pos = [tuple(x) for n in part["nodes"] for x in blocks.get(int(n, 16), [])]
        best = max(best, sum(1 for x in pos if x in have) / max(1, len(pos)))
        if best < SHARED and all(int(n, 16) in blocks and shifted(blocks[int(n, 16)], dst) for n in part["nodes"]):
            best = 1.0
    return best


def exact(patch, dst):
    """Every node the patch uses is in dst at the same offset, with the same vertices."""
    by = {p["node"]: p for p in dst["parts"]}
    for part in patch["parts"]:
        for n, info in part["nodes"].items():
            p = by.get(int(n, 16))
            if p is None or len(p["block_xyz"]) != info["numvtx"] or fnv(p["block_xyz"]) != info["fingerprint"]:
                return False
    return True


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    ge = files(probe.load_rom(sys.argv[1]))
    tables = {}
    patches = {}
    for path in sorted(glob.glob(os.path.join(SRC, "*.patch.json"))):
        with open(path, encoding="utf-8") as f:
            p = json.load(f)
        if "ported_from" not in p:
            patches[p["model"]] = p
    for name, p in patches.items():
        ns, _, nt = ex.header_counts(name)
        patches[name] = (p, *model(ge[name], ns, nt))
        tables[name] = patches[name][2]

    for hack in sys.argv[2:]:
        hf = files(probe.load_rom(hack))
        own = sorted(n for n in hf if n not in ge and n[:1] == "G")
        for name, (p, src, src_table) in patches.items():
            ns, _, nt = ex.header_counts(name)
            copies = sorted(n for n in hf if n == name or same_base(name, n))
            if not copies:
                for n in own:
                    try:
                        if shared(p, src, model(hf[n], ns, nt)[0]) >= SHARED:
                            copies.append(n)
                    except Exception:
                        pass
            ported = None
            for c in copies:
                try:
                    dst, table = model(hf[c], ns, nt)
                except Exception as e:  # a copy with other counts is not this model
                    print("  %s: not read as %s (%s)" % (c, name, e))
                    continue
                if exact(p, dst):
                    print("%s: %s has the same vertices (the patch applies as it is)" % (name, c))
                    continue
                if ported is not None and exact(ported, dst):
                    print("%s: %s has %s's vertices (its patch applies)" % (name, c, ported["model"]))
                    continue
                q, dropped = port(p, src, dst, c)
                if q is None:
                    print("%s: %s keeps none of its vertices; not carried" % (name, c))
                    continue
                ported = q
                out = os.path.join(SRC, c + ".patch.json")
                write_json(out, q)
                print("%s -> %s: %d of %d parts%s" % (name, c, len(q["parts"]), len(p["parts"]),
                      ("; left out (a vertex it dropped): " + " ".join(dropped)) if dropped else ""))

    with open(os.path.join(SRC, "textures.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write("{\n" + ",\n".join('  "%s": [%s]' % (n, ", ".join("\"0x%03x\"" % t for t in tables[n]))
                                     for n in sorted(tables)) + "\n}\n")
    return 0


def _depth(o):
    if isinstance(o, (dict, list)):
        return 1 + max((_depth(v) for v in (o.values() if isinstance(o, dict) else o)), default=0)
    return 0


def json_text(o, indent=0):
    """gevr_hp_common.json_text (Blender's tools import bpy): one corner or triangle per line."""
    flat = json.dumps(o)
    if not isinstance(o, (dict, list)) or (_depth(o) <= 3 and indent + len(flat) <= 200):
        return flat
    pad = " " * (indent + 1)
    if isinstance(o, dict):
        body = ",\n".join("%s%s: %s" % (pad, json.dumps(k), json_text(v, indent + 1)) for k, v in o.items())
        return "{\n%s\n%s}" % (body, " " * indent)
    return "[\n%s\n%s]" % (",\n".join(pad + json_text(v, indent + 1) for v in o), " " * indent)


def write_json(path, data):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(json_text(data) + "\n")


if __name__ == "__main__":
    sys.exit(main())
