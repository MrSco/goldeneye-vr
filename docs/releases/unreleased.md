# Changes after v0.4.9

- On-screen characters still get a model hit list after the cartridge's
  600 entries are in use. Report ee07735d: firing on solo Bunker 2
  (v0.4.9, BodiesStay=48) SIGSEGV'd because a living body's list was NULL
  once that pool was empty. Extra entries are allocated for the stage.
  A missing list still misses instead of crashing.

See [v0.4.9](v0.4.9.md) for smooth VR
locomotion and performance diagnostics, [v0.4.8](v0.4.8.md) for launcher settings
persistence and co-op spawn unclogging, and [v0.4.7](v0.4.7.md) for GE-X support.
