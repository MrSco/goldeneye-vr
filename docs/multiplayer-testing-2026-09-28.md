# Multiplayer test follow-up — 2026-09-28

The attached Android report (`gevr-c701f360.txt`) is base64 encoded. Its four
native backtraces are in `textMeasure` or `textRenderOutlined` through
`hudmsgBottomRender` and `maybe_mp_interface`. They came from an older build;
the v0.3.3 release includes the HUD text fix (#78). The report does not contain
a distinct backtrace proving a new timer-expiry crash.

## Addressed in this branch

- Online respawn starts when the death fade completes and sends the reliable
  respawn event. Remote corpses stop consuming stale motion packets.
- Remote players use the received position without also replaying their stick
  input. The character ground height is derived from foot level.
- Multiplayer movement speed matches single player exactly, preserving the
  original game's speed, 1.08x forward multiplier, and 3-second speedboost.
  The temporary pace reduction (which made movement too slow) was removed.
- Remote firing input is cleared while dead, and level music is restored on
  local online respawn if a death left the music state in the death sting.
- Multiplayer ammo crates grant only the selected weapon set's ammunition;
  the erroneous unconditional assignment is removed (#74, multiplayer portion).
- The multiplayer radar uses the head-locked VR HUD layer, sits farther
  inward, doubles in size in VR, and skips unoccupied online player slots.
  The online pause menu uses its centered layout in 2D as well as VR.
- The online pause page shows a music level. Its left stick changes pages and
  its right stick changes volume; the right stick no longer moves the player
  while the menu is open. Volume uses the raw right-stick axis with a dead zone
  and hold repeat; the old normalized range reported down at neutral and
  caused an up flick to lower volume on release. The hint is shortened to fit
  the screen. Stage setup no longer reloads the save folder's music volume over the level chosen
  during the current multiplayer session.
- The new pause menu crash report (`gevr-a03b4798.txt`) shows an Android
  FORTIFY abort from writing a 9-byte rank label into a 4-byte stack buffer.
  The rank buffer and score formatting are corrected.
- The online scoreboard shows occupied player names and scores. Voice packets
  that contain audible speech briefly mark a player's name with `>))`. A later
  review found that the online rows passed a small `TEXTCOLORS` enum to a
  renderer expecting RGBA, giving the rows zero alpha. The rows now pass
  visible packed colors.
- The local VR crouch toggle and two-hand pose no longer update while remote
  player slots tick. Remote positions and room membership are restored after
  those ticks to limit intermittent disappearance.
- The client requests its chosen character in the first handshake, including
  when joining a match already underway. Stage-ready requests repeat until a
  match snapshot arrives; the host avoids respawning a player twice.
- Match start and player joins show a top HUD message.
- Online melee has a close-range fallback when an opponent temporarily drops
  out of the on-screen room list.
- Vertically sliding wall-panel doors keep their mesh in the door's stage
  vertex buffer and use undeformed geometry during opening. Screenshots from
  Facility showed large coloured triangles from the clipping path.

## Needs headset or level-specific follow-up

- Recheck first and subsequent deaths with two headsets, including sound and
  camera behavior, and verify movement speed against the intended feel.
- Check the timer-expiry path on v0.3.3 or later and capture a fresh tombstone
  if the client still crashes.
- Verify the revised music controls and persistence through a match start.
- Check opponent visibility, firing sounds, and muzzle flashes with two
  headsets. Voice working alone does not verify remote character rendering.
- Check a longer warmup countdown before starting a match after another player
  joins. The current HUD match-start message does not provide a countdown.
- Verify vertically opening wall-panel doors in both 2D and VR on several
  levels; the mesh change is based on the Facility screenshots and code path.
- Recheck melee hits, opponent visibility, two-hand grip, character choice,
  join-in-progress recovery, join messages, scores and speaking markers with
  two headsets. These are code fixes, not yet multiplayer-tested.
- Inspect a single-player ammo crate's source slots against the original game
  before changing its contents (#74, single-player portion).
- Verify radar placement and legibility in-headset; 2D rendering cannot show
  stereo comfort or fusion.

Issue #69 was completed by commit 4936b95 and released in v0.3.2; it was
closed after verifying the release. Open issues #23, #30, #60, and #74 still
represent unfinished work. None should be closed solely because v0.3.3 was
released.
