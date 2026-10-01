# GoldenEye VR: status

Rewrite this file in place and keep it under about 120 lines. It replaced
HANDOFF.md (5,500 lines of session log) and the old STATUS.md on 2026-09-28;
both are kept in [docs/archive/](docs/archive/) as the evidence trail and are
not appended to any more. A feature branch keeps its own notes in a file on
the branch (MULTIPLAYER.md, tools/handpatch/NOTES.md) and updates this file
when it merges.

**Updated:** 2026-10-01. **Latest release:** v0.3.7 (tag v0.3.7, commit
e17928b, versionCode 47, protocol 15, SHA-256 bc123d08...): the merged
playtest branch (PR #82: Steam Audio voice, teams, clock sync and life IDs,
independent hand cycling, launcher tabs), the TURN fallback, and today's
fixes (#73, #32, #24, #64, #81, shots from the eye's depth). Before it,
v0.3.6 (tag v0.3.6, commit 081a1ae, versionCode 45, protocol 10): the 2026-09-30 playtest branch (below)
plus everything unreleased since v0.3.5 (launcher haptics screen #64,
motion-throw gaze assist, voice falloff by view, SFX volume and mic mute,
stereo without the walk sway and bob). v0.3.5 (versionCode 32): sniper scope
steadiness (#79), motion throwing and grenade cooking, room-aware voice
falloff, Music and Voice volume, the Dam truck's headlights (#71), bullet
holes no longer striped (#29). Earlier: v0.3.4 multiplayer testing fixes,
v0.3.3 HUD text crash guard (#78), v0.3.2 pause controls, rumble, tank audio,
left-hand weapons, previous weapon, damage handicap, v0.3.1 crash fixes and
"Send debug log", v0.3.0 drop-in multiplayer with names and tags.
Main 396dd64 (0.3.6, versionCode 43, protocol 9) carries the full multiplayer
punch list: in-level lobby (MENU_LOBBY), match config and restored options,
mute chord (hold Menu + right B), countdown and round transitions, late-join
spectator camera with its own voice group, spawn loadouts, online dual
wielding, ballots, shuffle and playlist rotation.
The 2026-09-30 playtest branch (claude/playtest-logging-feedback-731f59,
merged as f0c8e74, in v0.3.6) answered the tester report: net log lines now reach the
debug bundle (they were a separate logcat tag and rotated out); the lockup
was an unbounded inventory cycle (bondinv.c, now bounded and dumped); the
Bunker II to Facility switch was the ballots starting at zero, a vote for the
first stage (net_core.c netClearVotes at init and launch), plus the rotation
no longer turning before the first round; copies were posed from the barrel
(bent over, twisting: now the view's pitch and no yaw, as the flat game) and
had fists re-given every tick (a draw replayed forever); an overlap escape
lets two overlapping players walk apart (inside 60 units the game's volume
test refused every move; the copies' lag lets players get inside). Protocol
10 (090cd6c..9c032d3): the owner's health, armour and death ride in
PLAYER_STATE; a copy takes no damage of its own and dies when its owner
reports dead (credited to the last attacker), after grace periods each way
(a copy respawned in the tick it died once crashed the door tick; a stale
"dead" packet after a respawn once killed a fresh copy for a second point).
A copy's shot ammo boxes and guns fly on every headset. Kills scored twice
since 5974d49 (kill_count was both the game's kill message counter and the
drop-in score bank): the bank is player_data.gevr_score_bank now. The match
countdown rewrites its HUD message in place (the queue dropped numbers).
MP pause menu (3880832..dd5870b): left stick moves the LOBBY cursor, right
stick changes values any direction, START MATCH counts down on its row and
in the title, RETURN TO LOBBY says it cancels, hands hold still under the
menu (locate, camera snapshot and redraw delta all hold; memory note on
controller pose layers). The weapons panel (hold A) wraps.
Two-headset sessions 2026-09-30 afternoon (four rounds, both on the branch):
bodies upright, no more stuck players, ammo boxes move for both, a kill is
one point, the cap ends the match at 5 on both headsets with the results
screen for both, the chosen stage holds, the countdown counts. The Quest has
55c6ce1 (versionCode 44); 66f1613 adds only more door logging.

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
  Cloudflare Worker in `services/lobbies`, libjuice ICE, Cloudflare TURN as
  a fallback only since 2026-10-01: a join publishes on STUN alone, the
  relay is tried on UDP 3478 and 443, and the Worker caps credentials per
  month, TURN_MONTHLY_CAP, so the free tier cannot be exceeded).
  Owner-authoritative positions (the copy's stand tile follows them, so
  bodies render in the right room), hit reports relayed by the host, synced
  weapons, deaths and respawns. Protocol 9 (branch
  claude/multiplayer-visibility-bug-16b1a8, unreleased): the owner's
  projectiles, damaging explosions, door use and pickups are mirrored;
  remote gunfire fires from the owner's barrel and is placed by distance
  and direction; each copy gets its own view pass (shots, projectiles);
  seeded start pads; a countdown to each round; NEXT MAP / NEXT WEAPONS ballots in
  the in-level lobby; host migration (lowest slot takes over, the others rejoin through
  the same lobby or LAN beacon, 20 s grace); late-join spectator camera with isolated
  spectator voice groups; spawn loadouts and dual wielding (doubles and any-two). Voice chat: Opus on its own ENet channel, full volume in
  the lobby, distance and direction in a match: in view (a clear floor-tile
  walk to the speaker on the same floor; BG rooms are geometry chunks, too
  small to mean "same room") full to 500 units, easing to 60% at 4000; out
  of view quadratic to silence at 2500 (net_voice.c, logs "voice: slot ..."
  every 2 s). Mute: launcher "Mute Microphone", the watch, the pause menu,
  hold Menu + right B. Music, SFX (the game's own effects volume) and Voice: launcher
  sliders and pause menu rows (right stick adjusts, click steps MUSIC, SFX,
  VOICE, MIC; A still closes the menu), saved in goldeneye-vr.ini. The
  launcher never scrolls; its pages fit. Join Game is an accordion.
  Drop-in: games stay listed after the start, late joiners get match and
  world snapshots, slots reused, scores kept. Names: launcher "Your name"
  (ini PlayerName, default "Agent NNNN"), in the lobby lists and as
  depth-tested tags over other players (gunfire.c gevrDrawNameTags).
  Dashboard at lobbies.goldeneyevr.com. Details in MULTIPLAYER.md.

## What does not, or is untested

- Two headsets, 2026-09-29 night (0.3.6 test builds): players see each
  other, tags, guns, bullet holes; the v0.3.5 faults (invisible bodies,
  reload clicks, no explosions, fast walking) are gone. Its report's ten
  items are fixed on the branch but untested since, as are the NEXT MAP
  vote and host migration (the rejoin path needs three headsets). Untested:
  two home networks, a hotspot, four players mixed LAN and internet. Not
  done: other players' hands do not move; the held guns are the ROM's
  third-person models (no better ones exist).
- Open issues: #9 hand undersides (work in progress: shells patched in
  v0.1.18, gaps left such as the PP7 index finger), #23 multiplayer (kept
  open), #30 water shimmer (open in the PC port too, D245; the blue water
  goes through the same texSelect path #29 fixed, so re-check it),
  #60 laser watch arm changes size (#32 fixed 2026-10-01: the bullet's
  room-box pretest scaled its start by the visibility scale too, so on Dam
  and Surface, the 0.2 levels, no room beyond the tile walk's end was ever
  tested; chrprop.c). Closed
  2026-09-28: #18 (fixed), #50 (not reproduced), #56, #63, #64.
- 2026-10-01 (branch claude/goldeneye-vr-issues-5749bc, tested, merged):
  #73 NPCs above the floor and through walls was a port-only 256-entry
  ground callback table keyed by Model* that nothing emptied (gepc-ref D92
  ported: unka0 is a flag, the one callback called by name); #64 the watch
  laser and detonator rumble the watch arm, grenade cooking has a Haptics
  row (Grenade_Cook); #81 launcher "Aim: no lean" (AimNoLean); #24 the
  fist's white face dropped in the model converter (the mirrored left fist
  showed it from the other side); shots leave at the eye's depth along the
  barrel's line (bondview2.c gevrShotFromEye), so a gun poked through a
  door hits the door as on the N64. A beam past a door is the game's own
  rule: the laser and AR33 shoot through 2 objects, the RC-P90 3, the
  Magnum and Silver PP7 10, and door windows count as glass. Open: the
  Facility ceiling z-fighting and the see-through lit cone the user
  screenshotted (check v0.3.6 at the same spot; gepc-ref D308 is open too).
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
  as on disk (`* -text`); edit in place, never rewrite a whole file. Some
  files mix endings inside (src/game/mpmenu.c), and editors, including
  Claude's Edit tool, turn them all CRLF: before committing, compare
  `git diff --stat` with `git diff --stat --ignore-cr-at-eol`.

## Next

- v0.3.6 is out; both testers should update. The Facility swinging doors
  are the open question: the log line "move: blocked by door" (with the door's box and polygon corners since 66f1613) fired at
  90 degrees open several times, but the players passed through on the
  next round; possibly a leaf swung across the passage, as the original
  game's leaves also block. Open oddity: the copies' logged aim pitch
  printed near 350 where atan2 cannot reach (the barrel is only logged now).
  The log's own "version:" line prints the CMake-configure-time hash.
- Then MULTIPLAYER.md step 8 (quit and rejoin, NEXT MAP votes, host
  migration), the rest of step 7, the older list (mid-round joins, slot
  reuse, two networks, a hotspot, four players).
