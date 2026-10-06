# Changes after v0.4.9

- Investigating intermittent locomotion stutters: ignore tiny head jitter and
  wall-parallel movement when deciding whether collision should reset history.
- Add clamp/reset reasons, target lead, timeline phase and peak CPU draw-call
  diagnostics. The first 90/120 Hz diagnostic screenshots and 55 retained
  120 Hz windows show no clamps/resets; render/submission gaps remain.
- Reduce redundant texture/filter state calls in intermediate eye redraws and
  gate room-decal diagnostic scanning/logging behind its debug markers.
- Add timings for the actual frames around submission gaps, swapchain/fence
  waits, XR submission and asynchronous GPU eye passes. See
  [investigation](../vr-locomotion-clamps.md). The user reports smooth 120 Hz
  snap/smooth turning; measured eye GPU time is under 3 ms, while CPU work and
  swapchain waits warrant further investigation. Diagnostic builds are unpublished.
- Fix nanosecond rounding that could select 25 ms interpolation delay at 120 Hz;
  retain the intended 16.7 ms delay despite tiny runtime-period variations.
  Native regressions pass; the latest 120 Hz screenshot confirms 16.7 ms.
- Quest screenshot confirms the corrected 16.7 ms delay at 120 Hz. Pushing into
  Dam's starting curbs reproduces stutter, including a zero-clamp/reset window.
  Add paired motion and detailed renderer CPU diagnostics to separate movement
  jumps, physical-history resets and submission pacing; the report remains open.
- Replace physical-collision history resets with immediate root correction and
  contact-aware sub-tick head translation. This targets the measured curb/wall
  camera bounce while preserving joystick interpolation and gameplay collision.
  Native scheduling and fresh/redraw regressions pass; Quest verification is pending.
- Replace the ammo-panel crop's synchronous pixel readback (measured around
  5-6 ms on hitch frames) with fenced asynchronous transfers and later bounds
  updates. Preserve GL state and aim/layout generations; native driver tests
  pass. Headset pacing and ammo-panel transition checks remain pending.
- The next Quest capture shows the 5-6 ms ammo readback stall is absent, but
  curb binding and runtime image waits remain. Add per-tick requested/accepted
  movement and collision fallback traces to distinguish player sticking from
  presentation pacing. Native checks preserve the game's collision call order.
- Live curb traces show intermittent stopped simulation movement while the
  same edge usually allows sliding. Retry rejected local stereo edge slides
  with 0.1 mm outward clearance, still subject to the full collision test.
  Native checks reproduce/recover oblique contact rounding without bypassing
  corners or changing head-on, remote-player or virtual-screen behavior.
- Recover interpolation when a short session/refresh interruption leaves its
  logical timeline tens of milliseconds behind predicted display time. Reset
  the misaligned presentation history explicitly; retain 60 Hz simulation,
  normal sample spacing and no extrapolation. Quest retest remains pending.

- On-screen characters still get a model hit list after the cartridge's
  600 entries are in use. Report ee07735d: firing on solo Bunker 2
  (v0.4.9, BodiesStay=48) SIGSEGV'd because a living body's list was NULL
  once that pool was empty. Extra entries are allocated for the stage.
  A missing list still misses instead of crashing.

- Unattended Quest profiling ([report](../vr-render-stalls.md)): at 120 Hz,
  heavy Dam views exceed the frame budget on game-tick frames (p99 ~9.5-10 ms;
  about 2 dropped frames/s), driven by draw-call count; 90 Hz holds. The vertex
  ring now drains the GPU instead of reusing storage after a failed or timed-out
  fence. Room boundaries no longer split non-decal batches and batches hold 256
  triangles: 8% fewer draw calls (timing gain within measurement noise).

See [v0.4.9](v0.4.9.md) for smooth VR locomotion and fitted muzzles,
[v0.4.8](v0.4.8.md) for settings persistence and co-op spawn unclogging,
and [v0.4.7](v0.4.7.md) for GE-X support.
