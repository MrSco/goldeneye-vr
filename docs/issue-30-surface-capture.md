# Issue #30: water and Dam truck reflections

Diagnostic branch: `codex/issue-30-surface-probe`. Rendering is unchanged.
The supplied clips use HD textures. Glass is the Dam truck in the tunnel.

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
