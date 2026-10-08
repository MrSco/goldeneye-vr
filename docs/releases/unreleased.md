# Changes after v0.4.13

- **Bots and doors.** Bots open doors the way GoldenEye's guards do: the door
  swings away from them, and only a shut or closing door is touched. A bot
  pressed against a door opens it, steps back out of a door stalled against it,
  and never shuts a door from inside its doorway. Climbing counts as progress,
  and the first seconds of a round don't count as being stuck.
- **Soft collision online.** Players no longer block one another; overlapping
  players are pushed apart and slowed, so nobody can be pinned in a corner.
- **Out of objects.** A player who lands inside an object (Facility's vent drop
  onto the toilet) can walk out.
- **Hit immunity** is a host rule: Short (a quarter second between hits, the
  default), None (every hit counts, as in Perfect Dark) or GoldenEye (the
  original half second to a second). Solo and co-op keep GoldenEye's.
- **HUD.** Online, getting hit no longer hides the top message or the clock.
- **Protocol.** The new rule bits need network protocol 20 at the next release.

See [v0.4.13](v0.4.13.md) for the title-screen and full sound queue crash fixes,
[v0.4.12](v0.4.12.md) for the GoldenEye X remote mine detonator's
default fit, [v0.4.11](v0.4.11.md) for deathmatch bots, 3D game audio, GoldenEye X
models for every weapon and item, and co-op tank sync,
[v0.4.10](v0.4.10.md) for smoother movement at walls and curbs, fewer render stalls and shared co-op mission gadgets,
[v0.4.9](v0.4.9.md) for smooth VR locomotion and fitted muzzles, and
[v0.4.8](v0.4.8.md) for settings persistence and co-op spawn unclogging.
