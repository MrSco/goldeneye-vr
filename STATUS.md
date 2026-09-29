# GoldenEye VR: status

Rewrite this file in place and keep it under about 120 lines. It replaced
HANDOFF.md (5,500 lines of session log) and the old STATUS.md on 2026-09-28;
both are kept in [docs/archive/](docs/archive/) as the evidence trail and are
not appended to any more. A feature branch keeps its own notes in a file on
the branch (MULTIPLAYER.md, tools/handpatch/NOTES.md) and updates this file
when it merges.

**Updated:** 2026-09-29. **Latest release:** v0.3.4 (versionCode 29):
restore original multiplayer movement speed, multiplayer testing fixes.
Unreleased on main (versionCode 31): sniper scope steadiness (#79), VR
motion throwing and grenade cooking, room-aware voice falloff, Music and
Voice volume in the launcher and the multiplayer pause menu, the Dam
truck's headlights no longer showing the wheel behind them (#71: props'
blended layers write depth where opaque, model.c + gfx_opengl.cpp),
bullet holes no longer striped from some positions (#29: texSelect's
mip-mapped draws inherited rooms' G_TD_DETAIL and fast3d read tile 1 as
the base; now G_TD_CLAMP, othermodemicrocode.c; gepc-ref D107's renderer
fix not taken, it would move our rooms onto their detail tile).
v0.3.3 (49f2ac6, versionCode 28) brought multiplayer HUD text crash guard (#78),
player and lobby version tooltips, and TURN credential rate limit fix. v0.3.2 (0852b9f, versionCode 27) brought
multiplayer pause controls (#76), calibrated recoil and damage rumble (#64),
tank runover audio (#68), left-hand weapons (#56), previous weapon with right
grip + A (#63), left X for weapons and Y for use/reload, and normal online
damage handicap (#69). v0.3.1 (df311a0) fixed multiplayer crashes and added
"Send debug log". v0.3.0 (5fa83da) brought drop-in multiplayer, names and
name tags, network protocol 6.

## What it is

A native standalone Quest port of the n64decomp/007 decompilation, with
Perfect Dark VR's port layer (`port/`: OpenXR, SDL2, OpenGL ES, the fast3d
renderer) and the GEVR PC port's presentation (true stereo play, a virtual
screen for menus and cutscenes). The app ships no Rare content; the player
supplies the NTSC-U ROM. Releases reach players through the in-headset
updater and SideQuest; the site is goldeneyevr.com (its own repo).

## What works

- Solo missions in true stereo: the headset drives the camera, guns aim from
  the controllers and fire from the muzzle, two-handed hold, scopes with real
  magnification, gadgets and the watch on the left arm, hand and arm shells,
  melee from either hand, casings, HUD on a head-locked panel, snap or smooth
  turning, comfort vignette, 90 Hz. Or everything on the virtual screen (flat
  or curved).
- Weapons in stereo: tap A or X cycles, grip + A goes back (#63, the game's
  own hold A + Z), hold A is the weapon panel (#10), hold X the left hand's
  panel (#56): any doubles-capable gun the player carries, kept as the
  game's INV_ITEM_DUAL pair for the level; the right's own gun only with
  two of it. Solo only (the left hand's model buffer exists only then).
- In-VR launcher: ROM pick, play mode, turning and comfort, gun fit, unlock
  all (RAM only, saves untouched), texture packs (GoldenEye 007 HD and the
  HD + AI pack from the MrSco/GoldenEye-007-HD fork) and the updater.
- Multiplayer, experimental (#23): host and join on Wi-Fi, by direct IP, or
  through public and private internet lobbies (lobbies.goldeneyevr.com, a
  Cloudflare Worker in `services/lobbies`, libjuice ICE, Cloudflare TURN).
  Owner-authoritative positions with input extrapolation, hit reports relayed
  by the host, synced weapons, deaths, respawns and explosions. Voice chat:
  Opus on its own ENet channel, full volume in the lobby, distance and
  direction in a match (same room: gentle falloff to a 50% floor out to
  3500 units; other rooms: quadratic to silence at 2500, net_voice.c
  playersShareRoom), mute in the lobby, on the watch, or with left X+Y.
  Music and Voice volume: launcher sliders and the multiplayer pause menu
  (right stick adjusts, right stick click swaps; A still closes the menu),
  saved as MusicVolume and VoiceVolume under [VR] in goldeneye-vr.ini.
  Launcher Join Game is an accordion (Public, Private, LAN, Direct IP).
  Drop-in (v0.3.0): games stay listed after the start, solo warmup, late
  joiners get match and world snapshots, slots are reused, scores carry
  over. Names: launcher "Your name" (ini PlayerName, typed on the Quest
  system keyboard, default "Agent NNNN"), shown in the lobby lists and as
  depth-tested tags over other players (gunfire.c gevrDrawNameTags). Live
  dashboard at lobbies.goldeneyevr.com. Details in MULTIPLAYER.md.

## What does not, or is untested

- Multiplayer has never been played on two headsets. Untested: two home
  networks, a phone hotspot, four players mixed LAN and internet, voice heard
  by anyone. Not done: the match end is not shared, the host leaving is not
  handled, other players' hands do not move.
- Open issues: #9 hand undersides (work in progress: shells patched in
  v0.1.18, gaps left such as the PP7 index finger), #23 multiplayer (kept
  open), #30 water shimmer (open in the PC port too, D245; the blue water
  goes through the same texSelect path #29 fixed, so re-check it),
  #32 Surface ground patches without impacts (the PC port's D313 fix is
  already in bg.c; re-check), #60 laser watch arm changes size. Closed
  2026-09-28: #18 (fixed), #50 (not reproduced), #56, #63, #64.
- Unmerged branches: fix/60-watch-grip-hand (shelved: the laser watch
  gripped with a mirrored hand, which didn't lock to the wrist), plus
  whatever `git worktree list` shows in progress (several worktrees exist
  again, including Codex ones).

## How to work on it

- Build: `android/gradlew.bat assembleRelease`, signed from the gitignored
  `android/keystore.properties` and the release .jks (losing that key forces
  players to uninstall). Debug loop: `tools/gevr_boot_test.ps1`. CMake fetches
  SDL2 and Opus at configure time, so a clean build needs the internet once.
- Release: bump versionCode and versionName, tag = the build commit, asset
  named `GoldenEye-VR-vX.Y.Z.apk`, release-signed; the updater depends on that
  name.
- Workflow: one branch per issue or feature, commit before building (the
  launcher shows the hash), the user tests in the headset and says merge.
  Never install to the headset unasked. Port what GEVR PC and Perfect Dark
  VR already do rather than inventing; original game behaviour stays, even
  where VR makes it feel odd. The ROM sits at the repo root and is never
  committed.
- References on disk, beside this repo: `gevr-up` (GEVR PC docs and release
  notes), `pdvr` (Perfect Dark VR source), `gepc-ref` (its
  `docs/dev/findings-index.csv` answers most level-data questions),
  `goldeneyevr.com` (site), `GoldenEye-007-HD` (packs).
- Code map: `src/` the decomp; `port/src` the platform layer (`main.c`,
  `audio.c`, `input.c`, `gevr_engine_shim.c`, `net/`); `port/vr` OpenXR,
  the launcher and settings; `port/fast3d` the renderer; `android/` the app
  and its Java bridges (updater, lobbies, mods, microphone permission);
  `tools/` scripts; `services/lobbies` the Worker.
- Two defect classes explain most crashes: byte order of ROM data, and
  32-bit pointer slots in N64 structs (docs/RARE-LOGO-AUDIO-HANDOFF.md,
  docs/gepc-port-guard-sweep.md). Line endings are mixed per file and stored
  as on disk (`* -text`); edit in place, never rewrite a whole file.

## Next

- A two-headset session on v0.3.0 for the test list in MULTIPLAYER.md and
  PR #62 (mid-round joins, slot reuse, scoring, pickups, doors, objects
  dropped before a late join, voice heard, distance and direction, name tags
  hidden by walls, the host leaving, two networks, a hotspot, four players).
