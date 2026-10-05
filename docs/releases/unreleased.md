# Changes after v0.4.9

- A shot at an on-screen character with no model hit list misses that
  character instead of crashing. Report ee07735d: firing on solo Bunker 2
  (v0.4.9) SIGSEGV'd in the hit-list walk because `field_20` was NULL.

See [v0.4.9](v0.4.9.md) for smooth VR
locomotion and performance diagnostics, [v0.4.8](v0.4.8.md) for launcher settings
persistence and co-op spawn unclogging, and [v0.4.7](v0.4.7.md) for GE-X support.
