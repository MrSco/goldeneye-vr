# Live player movement speed

Development build v0.4.16, versionCode 71, protocol 21.

Movement speed adjusts on-foot stick travel from 50% to 200% in 25% steps.
100% preserves the original speed. Solo and multiplayer host preferences are
saved separately as percentages in `MovementSpeed` and `MpMovementSpeed` in
goldeneye-vr.ini; missing settings use 100%.

Choose it before starting under launcher Comfort for solo or Multiplayer >
Host > Match for a host. During solo gameplay, open the watch's Game Options >
VR Settings > Comfort > Movement speed. During a multiplayer session, open
pause > Rules > Movement speed. The multiplayer watch's Fun options also
expose the host control. Clients can view the host's percentage but cannot
change the session speed or override it with their saved solo preference.
The launcher slider marks 100% as the default and has a Reset button beside
it. Reset applies immediately to solo or the host's whole session and saves
the corresponding preference. The multiplayer pause picker marks 100% as
the default; the solo watch shows `100% DEFAULT` at normal speed.

Solo changes take effect on the next movement tick. Host changes update the
active and pending match config immediately and broadcast the updated rule
reliably to every connected client. Each client uses it once the update
arrives. No restart, next round, reload or ready reset is needed. Co-op and
deathmatch use the same rule, including bots. Late joins and host migration
retain it. The changed config layout requires every participant to use
protocol 21; older builds are rejected by the existing version check.

The multiplier applies to the player's horizontal animation-driven travel
before collision. Physical room-scale walking, vertical motion, tank travel,
turning and knockback use their existing paths. Guards' and Natalya's movement
is unchanged.

Validation:

- `port/tests/test_movement_speed.py`: production movement scaler, solo and
  host watch steps, client refusal, shared multiplayer override, normal
  fallback, and the physical movement integration boundary.
- `port/tests/test_multiplayer.py`: all 73 native checks, including both modes'
  live speed changes without disturbing readiness/countdown, invalid values,
  client restrictions, snapshots, host migration and packet truncation.
- `port/tests/test_vr_display.py`: actual settings reader/writer and defaults
  on Quest and desktop, all 49 independent solo/host combinations, missing
  keys, rounding and range clamping.
- `port/tests/test_locomotion.py` and `port/tests/test_pause_ui.py`: movement
  interpolation/physical collision and the existing pause controls pass.

Headset playtest still required:

1. Solo: walk the same corridor at 50%, 100% and 200%. Change speed in the
   watch without leaving the mission, resume and verify it applies at once.
   Try strafing, backwards walking, crouching, stairs and walls. Check
   physical walking remains 1:1 and settings survive an app restart.
2. Two headsets on this build: try deathmatch and co-op. Have the host change
   speed while the other player keeps walking; confirm the other player
   changes speed as soon as the update arrives. Verify a client cannot change
   it. Repeat during countdown, with bots, and after late join/host migration.

This branch also includes the earlier campaign fixes for #161-#164; their
separate playtest status is in `issue-161-164-campaign-fixes.md`.
