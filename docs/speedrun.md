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

- **LiveSplit** (launcher Play: the LiveSplit box and the PC's address,
  `port/src/gevr_livesplit.c`, ini `LiveSplit`, `LiveSplitHost`): the
  mission clock to LiveSplit's own TCP server (LiveSplit 1.8.29+, Control >
  Start TCP Server, port 16834), the one hook that reaches a Quest; Dab's Mod
  has none. Game time is the debrief's clock (`alwayspausegametime`, then
  `setgametime` ten times a second): this mission's plus every earlier one in
  the run, a failed try included. A mission's clock starting with LiveSplit
  stopped or finished starts it (`starttimer`, after `reset` for a finished
  run); a completed mission splits at the debrief's time
  (`end_of_mission_briefing`); a mission that ends unfinished while the run
  is on its first segment resets it (a level's retry), later it does
  nothing. A thread of its own does the talking, 1.5 s timeouts, a retry
  every 5 s. Empty address: this computer (a headset asks for the PC's).
  Tested by `port/tests/test_livesplit.py` (the client against a mock of
  LiveSplit's server) and on PC against the mock on 16834 (Dam: start, the
  clock streaming).

- **Debrief stamp** (`gevr_speedrun.c gevrSpeedrunStampDraw`, drawn by
  front.c's statistics page): what a moderator needs from a run's video,
  after Celeste's assist stamps and the port boards' rules. Only with the
  timer, the ghost race or LiveSplit on, so the debrief is the original's
  otherwise; in GoldenEye's HUD font, grey, in the room the page leaves: the
  build ("v0.5.15 9d9f84d") and the platform ("Quest, VR", "Windows, flat")
  in the left column under the kill total, and across the foot the rules
  (any addition: force pull, ally, speed, fast reinforcements, sticky mines,
  DK mode, paintball, line mode; else "original rules"), the frame clock's
  steps ("step 2 cap 0": the longest step in fields and how many hit
  frametiming.c's six-field cap, the port's equivalent of Midas's FPS
  counter), auto-aim, the turning (VR), the display rate and SpaceWarp, the
  art; what does not fit is counted ("+2 more"). Taken while the clock runs
  (the debrief itself is drawn flat), an addition counting if it was on at
  any time. Checked on PC through the menus: Dam, the watch's abort, the
  debrief's statistics page.

## Strafe running is kept

GoldenEye's strafe running (forward and sideways at once is faster than
forward) comes from Bond's speeds being clamped each on its own, never as a
vector (bondview2.c: speedforwards and speedsideways to +-1 apiece, the walk
animation's stride adding both). N64 runners get the full sideways from the
C buttons with the stick forward. Every input of the port reaches the same
place the same way: the Quest's move stick presses the C buttons past a
quarter of its throw (input.c, "Solitaire: C directions move"), as do a
pad's left stick, the touch stick and WASD on PC; no setting turns the stick
by the hand or the head. Measured 2026-11-11 on PC from Dam's start, two
seconds held: forward 503 units a second (speeds 1.08, 0), forward and
right 710 (1.08, 1.0): 1.41 times. The movement speed setting scales both
alike (and is stamped and not ranked).

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

- (The ghost boards went live 2026-11-11 with v0.5.16: goldeneyevr.com main
  78dbb7c, Worker version 4eb1d4a3.)
- LiveSplit against a real LiveSplit (Windows), from the headset over Wi-Fi.
- The stamp on a headset's debrief (Quest VR: the turning, the display rate).
