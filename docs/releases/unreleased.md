# Changes after v0.4.9

- Investigating intermittent locomotion stutters: ignore tiny head jitter and
  wall-parallel movement when deciding whether collision should reset history.
- Add clamp/reset reasons, target lead, timeline phase and peak CPU draw-call
  diagnostics. See [investigation](../vr-locomotion-clamps.md); headset comparison
  is pending, and the diagnostic build has not been published.

See [v0.4.9](v0.4.9.md) for smooth VR locomotion and fitted muzzles,
[v0.4.8](v0.4.8.md) for settings persistence and co-op spawn unclogging,
and [v0.4.7](v0.4.7.md) for GE-X support.
