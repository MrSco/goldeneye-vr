# Speedrun timer and ghost race

Branch notes (2026-11-11). Research behind it: what other games ship, what
GoldenEye runners and streamers use, in the untracked
`reports/Speedrun features and community standards.md` of the session
worktree; the short version is below.

## What runners expect

- **The clock is GoldenEye's own.** Boards rank by a trusted in-game time,
  and the GoldenEye community times levels by the debrief's mission time.
  `mission_timer` stays exactly as the decompilation has it: 1/60 s fields
  while Bond has control, none while paused, with controls locked or in a
  scripted cutscene. Celeste wiped its level times when it changed what its
  clock counted; ours must never change.
- **A port gets its own board.** The Elite takes N64 cartridges only; every
  re-release and port (XBLA, 1964, Xbox/Switch 2023, Project Midas) got its
  own speedrun.com board within days. No VR GoldenEye board exists yet.
- **Streamers run OBS + LiveSplit** (racetime.gg for races, therun.gg for
  live stats; splits.io is gone). Autosplitters can't read a Quest; LiveSplit's
  built-in TCP server (port 16834) is the one hook a headset can reach.

## In this branch

- **Speedrun timer** (`src/game/gevr_speedrun.c`, ini `SpeedrunTimer`,
  launcher Play, in game Game rules): Dab's Mod's HUD timer
  (`gehud.c geHudRenderMissionTimer`) in GoldenEye's countdown figures, mm:ss:hh,
  lower left. Stereo: on the HUD panel, full size, 22% in. Solo and co-op.
- **Ghost race** (`src/game/gevr_ghost.c`, ini `GhostRace`, same places):
  Dab's Ghost Time Trials (`modghost.c`). A run is recorded at 20 samples a
  second (position, facing, aim, speeds, crouch, gun, firing); a completed
  mission that counts for a best time (file.c `end_of_mission_briefing`)
  keeps it if it is the fastest, as `files/ghosts/ge-sNN-dN-best.gvg`
  (128-byte header with what the run was set under, 24-byte samples, hashed).
  The best run is replayed into a see-through Bond in the mission's outfit
  (`gevrSoloBondBodyHead`), walked by the multiplayer body's chooser
  (`gevrPlayerBodyChooseAnim`, moved out of `playerTick` unchanged). Nothing
  collides with it, shoots it or aims at it; it never takes the death pose, so
  it draws no random numbers. The split beside the timer is Dab's: the time
  the ghost took to reach the sample nearest Bond, green ahead, red behind.
  When the ghost's run ends it is hidden, not removed (removal freed its gun
  under objTick: SIGSEGV, fixed).

## Testing on the headset

`gevr_cheat.txt` word `ghostsave` keeps the run so far as the mission's ghost.
The injected N64 stick does not walk Bond, so a test route is warps:
`gevr_warp.txt "at X 60 16902 90"` stepping X down by 120 from 20197 on Dam.
Checked 2026-11-11 on the Quest 3 in screen mode and stereo: the timer, the
split, the ghost following the route, no crash after the ghost's run ends.
Not checked: the walk and strafe animations from a real walked run (needs
hands), a full mission completion saving through the debrief.

## Next

- Ghost server: Dab's `ghostnet.c` (accounts, uploads, leaderboards,
  downloads) against our own Worker on goldeneyevr.com, signed in with the
  Friends identity rather than Dab's name and PIN. Needs a deploy OK.
- LiveSplit client (TCP 16834: `starttimer`, `setgametime`, `split`, `reset`),
  never blocking the game.
- Debrief stamp: version, platform, VR or flat, art set, locomotion, settings.
- Check that VR stick movement keeps strafe-running's diagonal speed.
