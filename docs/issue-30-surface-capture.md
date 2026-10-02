# Issue #30: water and Dam truck reflections

Diagnostic branch: `codex/issue-30-surface-probe`. Rendering is unchanged.
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
  authorization. No rendering fix or visual acceptance is claimed.

The original water setup (`src/game/unk_092E50.c`, `sub_GAME_7F09343C`)
cross-fades two offset samples with a sine-driven PRIM_LOD_FRAC. That
animation is intentional. The port's adaptive water coordinate packing and
its generated reflection coordinates need a live capture before a fix.

After the user authorizes installing the signed diagnostic APK, enable:

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

All probe messages use the existing game logger. No fixed-shift candidate
or reflection change is enabled by this marker. The issue remains open.
