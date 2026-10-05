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
  [investigation](../vr-locomotion-clamps.md); further headset timing/visual
  comparison is pending, and the diagnostic builds have not been published.

See [v0.4.9](v0.4.9.md) for smooth VR locomotion and fitted muzzles,
[v0.4.8](v0.4.8.md) for settings persistence and co-op spawn unclogging,
and [v0.4.7](v0.4.7.md) for GE-X support.
