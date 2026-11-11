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
  under objTick: SIGSEGV, fixed). Each campaign keeps its own files
  (`ge-`, `gf-`, `tnd-`, `mirror-`), and a run is only raced on the mission,
  difficulty and campaign its header names. The header's flags say what the
  run was set under: VR `0x01`, Bean `0x02`, GE-X `0x04`, force pull `0x08`,
  the ally `0x10`, third person `0x20`, a changed movement speed `0x40`.
- **Ghost boards** (launcher tab **Ghosts**, `port/vr/vr_ghosts.cpp` drawn by
  `gevr_ghosts_ui.h`; the Worker's `services/lobbies/src/ghosts.ts` in the
  goldeneyevr.com repo): Dab's ghost server (`ghostnet.c` and its
  `tools/pdghostd`) on the lobby Worker, signed in with the Friends identity
  instead of Dab's name and PIN. One row per player per board (campaign,
  mission, difficulty), replaced only by a quicker run; the top 100 shown,
  eight to a page. The file is checked as the game writes it (sizes, time
  against samples, mission, hash); its owner field must be the uploader's
  friend code (stamped into a run of this headset's before its first upload,
  `gevrGhostStampOwner`); force pull, the ally and a changed speed are not
  ranked, as Dab's server refuses runs outside Ghost Trials' rules. The tab:
  mission, difficulty, all/VR/flat runs; your best and its rank, **Upload**,
  **Upload all my best runs** (each run once: `ghosts/uploaded.txt`); **Race**
  on a row downloads that run as `ge-sNN-dN-<friend code>.gvg`, names it in
  `ge-sNN-dN-race.txt` and turns Ghost race on; **Race my best** goes back.
  GoldenEye's boards only: a hack's missions race their own best. The
  Friends clients carry it (`FriendsClient.java`, `pc_friends.cpp`:
  `ghost-board`, `ghost-upload`, `ghost-get`, answered by `GHOSTS_*`,
  `GHOST`, `GHOST_UPLOADED`, `GHOST_SAVED`, `GHOST_FAIL`).

## Testing the boards on PC

No production involved: the Worker under `wrangler dev --local --port 8787`
in `services/lobbies`, seeded by registering players and posting runs to
`/v1/me/ghosts` (a real run with its owner set to the player's code and the
hash redone), then the PC build with `GEVR_FRIENDS_URL=http://127.0.0.1:8787/v1`
(and the updates and reports URLs at `127.0.0.1:9`) from a folder of its own
(the ROM, the exe, `phonon.dll`), driven by `--input-script`: `point 158 180`
+ `click` opens Ghosts at the folded Friends strip's canvas size. Checked
2026-11-11: the board, Race (download, race file, Ghost race on), Upload (#1,
the row in gold), then `--boot-stage 33` raced the downloaded run (the log's
`ghost: racing $S/ghosts/ge-s33-d0-<code>.gvg`, the see-through body, the
split). Worker tests: `npm test` there (`test/ghosts.test.mjs`); client:
`FriendsClientTest`, `test_launcher_frame` (the tab at its fullest).

## Testing on the headset

`gevr_cheat.txt` word `ghostsave` keeps the run so far as the mission's ghost.
The injected N64 stick does not walk Bond, so a test route is warps:
`gevr_warp.txt "at X 60 16902 90"` stepping X down by 120 from 20197 on Dam.
Checked 2026-11-11 on the Quest 3 in screen mode and stereo: the timer, the
split, the ghost following the route, no crash after the ghost's run ends.
Not checked: the walk and strafe animations from a real walked run (needs
hands), a full mission completion saving through the debrief.

## Next

- Ghost boards live: the Worker's branch `claude/ghost-boards` (goldeneyevr.com
  repo) merged and deployed, with the user's OK. Until then the Ghosts tab
  says the board did not load.
- LiveSplit client (TCP 16834: `starttimer`, `setgametime`, `split`, `reset`),
  never blocking the game.
- Debrief stamp: version, platform, VR or flat, art set, locomotion, settings.
- Check that VR stick movement keeps strafe-running's diagonal speed.
