# Changes after v0.4.14

- **Co-op briefing hang (reports 7b162d90, 919e73ee).** After a mission, a
  follower moving from the briefing to mission select spun forever in
  `langGetLangBankIndexFromStagenum`. The host's next screen had no folder
  entry (`-1`), and that was applied before the briefing cleared its text
  bank. The screen being left now keeps its mission until its own cleanup.
  An unset page no longer looks up a text bank, and an unknown level logs
  and returns instead of spinning. Manual report 3e30f876 is the same
  session as 919e73ee.
- **Music stop wait (report c3ad6b13).** Starting action music on the
  Caverns intro waited forever for sequence player 2 to reach stopped.
  The stop is a queue event and can be dropped. The wait now posts the
  stop again while the player is still playing, and after two seconds
  stops and frees remaining voices, cancels old events, and restarts the
  audio heartbeat before the new track loads. The same bound covers
  players 1 and 3.
- Two-handed grips work with a gun in either hand, in both handedness modes.
  When the off hand holds a gun, Unarmed stays bare even if a sniper is owned,
  so the free hand can support it. Holstering the off-hand gun restores the
  original sniper-butt substitution.

See [v0.4.14](v0.4.14.md) for bots handling doors, soft collision between players
and the hit immunity rule,
See [v0.4.13](v0.4.13.md) for the title-screen and full sound queue crash fixes,
[v0.4.12](v0.4.12.md) for the GoldenEye X remote mine detonator's
default fit, [v0.4.11](v0.4.11.md) for deathmatch bots, 3D game audio, GoldenEye X
models for every weapon and item, and co-op tank sync,
[v0.4.10](v0.4.10.md) for smoother movement at walls and curbs, fewer render stalls and shared co-op mission gadgets,
[v0.4.9](v0.4.9.md) for smooth VR locomotion and fitted muzzles, and
[v0.4.8](v0.4.8.md) for settings persistence and co-op spawn unclogging.
