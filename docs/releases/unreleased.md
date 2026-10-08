# Changes after v0.4.12

- Returning to the title no longer walks the previous stage's portals.
  Quest 2 crash 65a366e6 faulted in sound-path setup after Statue, because
  the title does not load a background and the old portal table had been
  freed with the stage bank. See [crash 65a366e6](../crash-65a366e6-title-portals.md).
- A full sound queue no longer spins the game thread. Quest 3 crash
  bfb06b31 (the same session as manual report 15cb7aaa) hung in the sound
  handler when five bots started a live Archive round and the queue's
  frame heartbeat was dropped. An empty or all-zero queue now waits one
  frame. See [crash bfb06b31](../crash-bfb06b31-sfx-queue.md).

See [v0.4.12](v0.4.12.md) for the GoldenEye X remote mine detonator's
default fit, [v0.4.11](v0.4.11.md) for deathmatch bots, 3D game audio, GoldenEye X
models for every weapon and item, and co-op tank sync,
[v0.4.10](v0.4.10.md) for smoother movement at walls and curbs, fewer render stalls and shared co-op mission gadgets,
[v0.4.9](v0.4.9.md) for smooth VR locomotion and fitted muzzles, and
[v0.4.8](v0.4.8.md) for settings persistence and co-op spawn unclogging.
