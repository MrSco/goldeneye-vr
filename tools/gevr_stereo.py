#!/usr/bin/env python3
"""docs\\50 G1/G4 - turn a -Flat screenshot into a number.

Method is docs\\49's, unchanged:
  1. find the seam by minimising mean|L-R| over candidate half-widths
  2. cross-correlate the halves for the (dx, dy) that aligns them
  3. do it per depth band, because near and far disparities differ
  4. compare against the PREDICTION line the log printed BEFORE anyone looked

Usage:  gevr_stereo.py shot.png [--log gevr.log] [--expect -113]
"""

import argparse
import re
import sys

import numpy as np
from PIL import Image


def load_gray(path):
    im = Image.open(path).convert("L")
    return np.asarray(im, dtype=np.float64), im.size


def find_seam(g, nominal, search=24):
    """The seam is where the two halves are most alike.

    *** ONLY VALID ON A BROKEN BUILD. Not the default. ***

    docs\49 found the seam this way and it worked - because the two halves
    were the SAME IMAGE, so mean|L-R| really did bottom out at the true seam.
    The moment stereo works, the halves differ everywhere by construction and
    this minimum no longer marks the seam. Measured on a synthetic pair with a
    known -113 px disparity: it picked 956 instead of 960, and the 4 px error
    landed straight on the answer as -117.

    So: the seam comes from the CLIENT WIDTH, which the capture script
    guarantees is exact. This function is a cross-check for a suspect capture,
    behind --find-seam, and never the default. A method that only works while
    the bug is present cannot be used to verify the bug is gone.
    """
    h, w = g.shape
    best, best_score = nominal, None
    lo = max(8, nominal - search)
    hi = min(w - 8, nominal + search)
    for cut in range(lo, hi + 1):
        n = min(cut, w - cut)
        if n < 8:
            continue
        left = g[:, cut - n:cut]
        right = g[:, cut:cut + n]
        score = float(np.mean(np.abs(left - right)))
        if best_score is None or score < best_score:
            best, best_score = cut, score
    return best, best_score


def ncc_shift(a, b, max_dx, max_dy=6):
    """Best (dx, dy) aligning b onto a, by normalised cross-correlation.

    SIGN, stated once and verified against a synthetic pair:
      dx > 0  right-half content sits further RIGHT than the left half's
      dx < 0  right-half content sits further LEFT   <-- docs\50 G1 expects this

    docs\50 predicts -113 px: the right eye further LEFT.
    """
    a = a - a.mean()
    b = b - b.mean()
    if a.std() < 1e-6 or b.std() < 1e-6:
        return None, None, 0.0

    best = (0, 0, -2.0)
    h, w = a.shape
    for dy in range(-max_dy, max_dy + 1):
        for dx in range(-max_dx, max_dx + 1):
            ax0, ax1 = max(0, -dx), min(w, w - dx)
            bx0, bx1 = max(0, dx), min(w, w + dx)
            ay0, ay1 = max(0, -dy), min(h, h - dy)
            by0, by1 = max(0, dy), min(h, h + dy)
            if (ax1 - ax0) < w // 3 or (ay1 - ay0) < h // 3:
                continue
            pa = a[ay0:ay1, ax0:ax1]
            pb = b[by0:by1, bx0:bx1]
            denom = pa.std() * pb.std()
            if denom < 1e-6:
                continue
            c = float(np.mean(pa * pb) / denom)
            if c > best[2]:
                best = (dx, dy, c)
    return best


def parse_prediction(log_path):
    """Read the PREDICTION line the build printed before anyone looked."""
    try:
        text = open(log_path, "r", errors="ignore").read()
    except OSError:
        return None
    m = None
    for m in re.finditer(
        r"PREDICTION:.*?=\s*(-?[\d.]+)\s*ndc\s*=\s*(-?[\d.]+)\s*px per\s*(\d+)-px",
        text,
    ):
        pass
    if m is None:
        return None
    return {"ndc": float(m.group(1)), "px": float(m.group(2)), "half": int(m.group(3))}


def report_log_gates(log_path):
    """docs\\50 G3. The instrument, before the test."""
    try:
        text = open(log_path, "r", errors="ignore").read()
    except OSError:
        print("  (no log given - G3 NOT verified)")
        return
    print("\n=== G3, the instrument gate ===")
    for pat in ("eye targets:", "render passes:", "fb verdict:", "present source:"):
        hits = [l.strip() for l in text.splitlines() if pat in l]
        if hits:
            print(f"  {hits[-1]}")
        else:
            print(f"  MISSING: '{pat}' never printed")

    last_targets = [l for l in text.splitlines() if "eye targets:" in l]
    if last_targets and "shared=1" in last_targets[-1]:
        print("\n  *** shared=1 - ONE TARGET FOR BOTH EYES. The fix did not take.")
        print("  *** Everything below is void. docs\\50 G3.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--log")
    ap.add_argument("--expect", type=float, default=None,
                    help="expected dx in px for the FAR band (negative = right eye further left)")
    ap.add_argument("--bands", type=int, default=4)
    ap.add_argument("--find-seam", action="store_true",
                    help="search for the seam instead of using client-width/2. "
                         "Only meaningful on a build with NO stereo - see find_seam().")
    args = ap.parse_args()

    if args.log:
        report_log_gates(args.log)

    g, (w, h) = load_gray(args.image)
    print(f"\n=== image ===\n  {args.image}  {w}x{h}")

    if args.find_seam:
        seam, score = find_seam(g, w // 2)
        if seam != w // 2:
            print(f"  !! --find-seam moved the seam {w // 2} -> {seam}.")
            print("     On a WORKING build that is the search misfiring, not a")
            print("     finding - see find_seam's docstring. Re-run without it.")
    else:
        seam = w // 2
        n0 = min(seam, w - seam)
        score = float(np.mean(np.abs(g[:, seam - n0:seam] - g[:, seam:seam + n0])))
    n = min(seam, w - seam)
    left = g[:, seam - n:seam]
    right = g[:, seam:seam + n]
    print(f"  seam at {seam} (client width / 2), half-width {n}")
    print(f"  mean|L-R| at seam = {score:.3f}")

    diff_frac = float(np.mean(np.abs(left - right) > 2.0))
    print(f"  pixels differing by >2 levels: {diff_frac * 100:.2f}%")
    if diff_frac < 0.02:
        print("  *** THE TWO HALVES ARE THE SAME IMAGE. This is the docs\\49 failure.")

    pred = parse_prediction(args.log) if args.log else None
    expect = args.expect
    if pred is not None:
        scaled = pred["px"] * (n / pred["half"])
        print(f"\n=== PREDICTION, from the log ===")
        print(f"  {pred['ndc']:+.4f} ndc = {pred['px']:+.1f} px per {pred['half']}-px half")
        print(f"  scaled to this {n}-px half: {scaled:+.1f} px")
        if expect is None:
            expect = scaled

    max_dx = max(40, int(abs(expect) * 2) if expect else 160)
    print(f"\n=== measured disparity, {args.bands} depth bands (top = far) ===")
    print(f"  {'band':<12}{'dx':>8}{'dy':>6}{'corr':>8}")
    bh = h // args.bands
    far_dx = None
    for i in range(args.bands):
        a = left[i * bh:(i + 1) * bh, :]
        b = right[i * bh:(i + 1) * bh, :]
        dx, dy, c = ncc_shift(a, b, max_dx=max_dx)
        if dx is None:
            print(f"  {i:<12}{'flat':>8}")
            continue
        label = ["far/sky", "mid", "near", "gun/HUD"][i] if args.bands == 4 else str(i)
        print(f"  {label:<12}{dx:>+8d}{dy:>+6d}{c:>8.3f}")
        if i == 0:
            far_dx = dx

    print("\n=== VERDICT ===")
    if far_dx is None:
        print("  no measurable structure in the far band. Try a different spot.")
        return 2
    if expect is None:
        print(f"  far band dx = {far_dx:+d} px. No prediction supplied to compare against.")
        return 0

    ok = abs(far_dx - expect) <= 5
    print(f"  far band dx = {far_dx:+d} px, expected {expect:+.1f} +/- 5")
    print(f"  vertical dy must be ~0 (it is the thing docs\\47 measured correct)")
    print(f"\n  G1: {'PASS' if ok else 'FAIL'}")
    if not ok and abs(far_dx) < 3:
        print("  0 px means the halves still carry one image - the docs\\49 failure.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
