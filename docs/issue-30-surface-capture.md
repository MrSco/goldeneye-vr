# Issue #30: water and Dam truck reflections

Working branch: `codex/issue-30-surface-probe`. Water and glass fixes verified
in the headset by the user on 2026-10-02 and approved for main.
The supplied clips use HD textures. Glass is the Dam truck in the tunnel.

## Current evidence and validation

- User retested: odd water animation persists with both HD and original
  textures. HD replacement alone cannot explain it.
- A later screenshot shows repeating orange/red/yellow flecks in the water.
  Whether those colors persist with HD disabled is not yet established.
- No headset files/settings were changed during this investigation. The
  installed v0.3.9 package reports last update 2026-10-02 08:30:59; its game
  log identifies code `5ebcef4`. Existing logs were saved before installing
  anything.
- Diagnostic code `263814c`: `assembleRelease` passed (2m 3s); APK signature
  verification passed with v2 signing. Headset capture is pending install
  authorization at that point. The baseline made no rendering changes.

## Headset capture and candidate

The user authorized installing and capturing the baseline. Signed code
`263814c` was installed at 09:09:52, and gameplay was captured from about
09:10:33 to 09:11:51. Capture was stopped and its marker removed afterwards.
Frigate (26) and Dam (33) were both visited, in stereo and screen mode.
Almost all samples had HD enabled; the earlier user comparison establishes
that water's odd animation also happens with original textures.

The user clarified that returning to the launcher cleared only the colored
lines, not the odd water animation. No colored lines appeared in this run.
This symptom remains unresolved and is not claimed fixed by the candidate.

The trace recorded repeated water shift transitions between `(4,4)`, `(4,3)`
and `(3,4)` while the player looked around. No s16 UV overflow was captured,
but near-vertex perspective depth lost up to 9.08% when truncated for the
ported vertex format: a packed w of 10.999 becomes 10. That alters both
perspective interpolation and vertex screen position as the camera moves.

Candidate: water alone uses a host-only float vertex command (`0x7F`),
preserving projected x/y/w and full S/T through the existing triangle,
texture-scale, two-sampler, filtering and animated combiner pipeline.
It uses the original tile shifts rather than the packing compensation.
Clouds and the original cartridge rendering path retain their existing
paths. The original water cross-fade animation remains.

Dam reflection samples still varied with head angle while the body's
reflection axes stayed fixed. The stereo adjustment had passed world axes
to fast3d as though they were view axes. Candidate: convert the body's axes
to view space before fast3d's inverse-modelview transform. Screen mode keeps
the original camera-driven reflection behavior; this change targets the
stereo adjustment, not a new physical reflection model.

`python port/tests/test_surfaces.py` passes the production water loader's
full-range position/UV, perspective, texture scale, color, cache bounds and
aspect checks, plus 2,457 reflection yaw/pitch/roll poses (including vertical views). Maximum residual
axis error is 0.009216 from the 8-bit LookAt quantization.

Signed candidate `942e897` passed `assembleRelease` and APK v2 signature
verification, was installed, and was captured in the headset. The final log
identifies this exact build, visits Frigate (26) and Dam (33), and records
float water in both stereo (127 samples) and screen mode (111 samples).
There are no recorded crashes or unknown graphics commands, and no new GL
errors versus the baseline (the same desktop-function lookup messages occur
at startup in both). The user confirmed both water and glass look good and
requested merge, commit and push to main. Capture was stopped and the probe
marker removed; the app logged capture disabled. The intermittent colored
lines remain unconfirmed and are not claimed fixed.

The original water setup (`src/game/unk_092E50.c`, `sub_GAME_7F09343C`)
cross-fades two offset samples with a sine-driven PRIM_LOD_FRAC. That
animation is intentional. The fixes address the port's coordinate packing
and stereo reflection-axis conversion, as established by the captures above.

For a future authorized diagnostic capture, enable:

```powershell
adb shell "touch /sdcard/Android/data/com.gevr.port/files/gevr_surfaceprobe.txt"
adb shell "chmod 644 /sdcard/Android/data/com.gevr.port/files/gevr_surfaceprobe.txt"
adb logcat -v time -s GoldenEye:I > surface30.log
```

The marker is polled once a second. `surface30:` lines also reach the app's
saved debug log. Keep the live capture running until reproduction ends.

For Frigate water, then the Dam truck window:

1. Hold still for five seconds to capture animation without camera movement.
2. Slowly turn left/right, then look up/down for ten seconds.
3. In stereo, keep facing forward and lean side to side for five seconds.
4. Hold Menu and press X to toggle HD textures; repeat the same movements.
5. Repeat in screen mode, using the stick to turn the game camera. Moving
   your head only moves your view of the virtual screen.

Record which mode and texture setting show the problem. A short video with
the same movements helps correlate visual changes with the trace.

Stop logcat with Ctrl+C and disable the probe:

```powershell
adb shell "rm /sdcard/Android/data/com.gevr.port/files/gevr_surfaceprobe.txt"
```

Capture fields:

- `camera`: mode, position, view/up directions and reflection axes, 10 Hz.
- `water` / `water-v`: raw S/T, fan shift/fold, projection scale and values
  before conversion to s16; 10 Hz plus every shift change.
- `frame`: screen/HD state and monotonic timestamp, 5 Hz.
- `reflection`: normals, generated UVs, transformed reflection axes and
  modelview basis; up to four vertex batches per sampled frame.
- `material`: texture source, size, tile masks/offsets/shifts, blend fraction
  and combiner; up to eight draws per sampled frame.

All probe messages use the existing game logger. The marker controls logging
only. With the candidate, `water path=float` records the old packing values
for comparison; they are no longer the values used to draw water.
Water and glass are headset-verified. The separate colored-line symptom
remains unresolved; no GitHub issue closure is implied by these notes.
