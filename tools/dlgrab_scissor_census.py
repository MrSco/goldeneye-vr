#!/usr/bin/env python3
"""dlgrab scissor-span census -- 280 STEP 1. OFFLINE, no build, no run.

WHAT IT DOES
  Walks one GETV_DLGRAB dump in emission order and splits it into SCISSOR SPANS:
  a span is one SETSCISSOR and every command up to the next one. For each span it
  counts the geometry actually emitted under it (TRI1/TRI2 commands and the
  triangles inside them, plus TEXRECT/FILLRECT separately, which are 2D and are
  NOT room geometry).

  Then, over the spans that carry TRIANGLES, it computes the union of their
  rectangles and reports the part of the play view that is inside NO such
  rectangle -- the region no room could have painted, whatever it drew.

  All coordinates are the dump's own canvas units (10.2 fixed point, 1280x960 for
  a 320x240 canvas). No window term anywhere in this file, deliberately: with
  GETV_WIDESCREEN=0 there is none in the pipeline either (280 section 3).

WHAT IT PRINTS IF IT IS POINTED AT THE WRONG THING -- read this BEFORE the numbers
  * If a triangle-carrying span is the WHOLE VIEW, the union is the whole view and
    uncovered area is 0 by construction. It prints FULLVIEW-GEOMETRY and says the
    frame cannot be discriminated. That is a null, not a clean bill.
  * If a frame carries no per-room sub-rect at all it prints NO-SUBRECTS.
  * A frame with no view rect at all prints NO-VIEW and computes nothing.
  * Streets is an OUTDOOR level: sky above the rooflines is legitimately unpainted
    by any room. So a large uncovered band ADJACENT TO THE VIEW TOP is expected and
    proves nothing. The negative controls (2849, 4283) exist to show what that
    looks like. Only an uncovered region BOUNDED ON THE LEFT BY A SUB-RECT'S lrx,
    away from the view edges, is the artifact's shape.
"""
import re, sys, collections

SC = re.compile(r'^(\d+)\s+d\d+\s+\S+\s+\S+\s+SETSCISSOR\s+scissor \((\d+),(\d+)\)-(?:\()?(\d+),(\d+)\)')
TRI = re.compile(r'\bTRI(1|2)\b')
NTRI = re.compile(r'\(\s*\d+,\s*\d+,\s*\d+\)')
RECT2D = re.compile(r'\b(TEXRECT|TEXRECTFLIP|FILLRECT)\b')

def spans(path):
    out, cur = [], None
    for line in open(path, errors='replace'):
        m = SC.match(line)
        if m:
            if cur: out.append(cur)
            cur = dict(seq=int(m.group(1)),
                       r=(int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5))),
                       tris=0, tricmds=0, rects=0)
            continue
        if cur is None: continue
        if TRI.search(line):
            cur['tricmds'] += 1
            cur['tris'] += len(NTRI.findall(line))
        elif RECT2D.search(line):
            cur['rects'] += 1
    if cur: out.append(cur)
    return out

def area(r):
    return max(0, r[2]-r[0]) * max(0, r[3]-r[1])

def subtract(view, rects):
    """Return the uncovered part of `view` as a list of maximal-ish rectangles,
    by scanline decomposition over the x-cuts. Exact in area."""
    xs = sorted({view[0], view[2]} | {c for r in rects for c in (r[0], r[2])
                                      if view[0] < c < view[2]})
    out = []
    for i in range(len(xs)-1):
        x0, x1 = xs[i], xs[i+1]
        ivals = sorted((r[1], r[3]) for r in rects if r[0] <= x0 and r[2] >= x1
                       and r[3] > r[1])
        y = view[1]
        for a, b in ivals:
            if a > y: out.append((x0, y, x1, min(a, view[3])))
            y = max(y, b)
            if y >= view[3]: break
        if y < view[3]: out.append((x0, y, x1, view[3]))
    return [r for r in out if area(r) > 0]

def census(path):
    sp = spans(path)
    if not sp:
        print(f"{path}: NO SCISSORS -- not a dump this tool understands"); return
    # The view: the widest rect that any span uses, excluding the full canvas.
    canvas = (0, 0, 1280, 960)
    cands = [s['r'] for s in sp if s['r'] != canvas]
    if not cands:
        print(f"{path}: NO-VIEW (every scissor is the full canvas)"); return
    view = max(cands, key=area)
    geo = [s for s in sp if s['tris'] > 0]
    full = [s for s in geo if s['r'] == view or area(s['r']) >= 0.98*area(view)]
    sub  = [s for s in geo if s not in full]
    print(f"\n=== {path}")
    print(f"  spans={len(sp)}  geometry-carrying={len(geo)}  view={view} area={area(view)}")
    if not sub:
        print("  NO-SUBRECTS -- no per-room sub-rectangle carries geometry. "
              "Nothing to measure; this frame cannot support or refute 279 section 4.")
    if full:
        tot = sum(s['tris'] for s in full)
        print(f"  FULLVIEW-GEOMETRY: {len(full)} span(s), {tot} tris, drawn under the "
              f"FULL VIEW rect.")
        print("  -> the union is the whole view by construction and the uncovered area "
              "below is 0 REGARDLESS of the artifact. THIS FRAME IS A NULL.")
    rects = [s['r'] for s in geo]
    unc = subtract(view, rects)
    ua = sum(area(r) for r in unc)
    print(f"  uncovered = {ua} canvas units^2 = {100.0*ua/area(view):.2f}% of the view")
    unc.sort(key=area, reverse=True)
    for r in unc[:8]:
        edges = []
        if r[0] == view[0]: edges.append("view-left")
        if r[2] == view[2]: edges.append("view-right")
        if r[1] == view[1]: edges.append("view-TOP(sky)")
        if r[3] == view[3]: edges.append("view-bottom")
        lrx = [s['r'] for s in sub if s['r'][2] == r[0]]
        if lrx: edges.append(f"LEFT EDGE IS A SUB-RECT lrx ({len(lrx)} of them)")
        print(f"    {r}  area={area(r)}  {' + '.join(edges) if edges else 'interior'}")
    print("  per-room sub-rects carrying geometry:")
    for s in sorted(sub, key=lambda s: s['seq'])[:24]:
        print(f"    seq={s['seq']:6d} {s['r']} tris={s['tris']}")
    if len(sub) > 24: print(f"    ... {len(sub)-24} more")

if __name__ == '__main__':
    for p in sys.argv[1:]:
        census(p)
