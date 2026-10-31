# GoldenEye VR: status

Rewrite this file in place and keep it under about 120 lines. Older session
logs are in docs/archive/; feature investigations keep their own notes.

**Current release:** v0.5.13 (versionCode 93, protocol 23), 2026-10-20.
v0.5.7 to v0.5.13 play together; v0.5.6 and older can't join. Building does
not publish a GitHub release. No game data is shipped.

v0.5.13 (PR #217, #216): co-op mission world from the host. NET_MSG_COOP_CLOCK
(countdown value/running/mission hide bits 1|2|16, gas, blast sequence; 4/s
while changing, 2 s idle) and NET_MSG_COOP_SCRIPT (list ID + offset into the
receiver's own setup AI list, whitelisted context-free ops replayed through
ai() on a scratch chr; durable ones kept in a 512-entry last-execution history
for late joiners and host changes). Glass break object event + glass in the
world snapshot; host world blasts carry explosion flag 4 (no grenade
consumed, created as owner -1, client replays suppress their own blast).
Additive on protocol 23. Not played on two headsets: #216 stays open until a
player confirms Control glass, Train escape and a midway join. Limits: blast
sequence around the host only; vehicles/aircraft still local; late joiners
see past destruction replayed. [v0.5.13](docs/releases/v0.5.13.md),
[#216 audit](docs/issue-216-coop-world.md).

v0.5.12 (PR #212, #213, #214): Bean frame rate in VR. Only rooms whose Bean
geometry reaches 50+ units past their box are drawn past the portals, and
such a room draws that geometry alone (Facility route 2,334 drops -> 8).
The decal stencil band is kept to impacts and hand-held guns (it killed
Adreno LRZ). Every Bean prop is a GPU mesh (rigid_min 0) on a static
vertex-shader variant. gfx_sp_tri1 works out per-vertex constants once per
triangle. Cut ragdoll skins are built once per body and cut set, and
skinned on the GPU (gevr_bean_pose.inc.h beanCutGpuDraw). Jungle VR start
100 -> 120, drone gun 102 -> 120, cut bodies 20-30 fps drop -> none. Test
hooks: gevr_cheat.txt where/kill<R>, gevr_warp.txt at/near. Open: #215
(mission not saved after HD -> Bean, a player report; check cheats first).
[v0.5.12](docs/releases/v0.5.12.md), [Jungle notes](docs/jungle-bean-perf.md).

v0.5.11 (PR #210): the XBLA Community Edition from the player's own
CommunityEditionUpdaterV6.zip, Dab's Mod's way: the import keeps the 145 files
the patch reads; **CE...** on the Bean row applies files.diff (vendored
HDiffPatch) into ge007-bean/ce/files, read first while the **CE** box (ini
BeanCommunityEdition) is ticked; CE fog distances and colours built in (Dab's
values). Copies imported earlier must be imported again. Fixes found testing
it: the vertex ring's per-frame segments 4 -> 16 MB (Jungle frames 5.0-5.6 MB
spilled, and the eye redraw read overwritten vertices: a band over the
canopy); prop part bones bind only to groups with a list (Runway's plane and
gun lost their bodies to the list-less root); a head's LOD alternatives all
carry it (Bond headless in Frigate's intro); block-sized pictures capped at
the tile mask (Frigate water speckles); SpaceWarp row on the watch. Headset:
all checked. [v0.5.11](docs/releases/v0.5.11.md),
[Community Edition](docs/bean-community-edition.md).

v0.5.10 (PR #208, PR #209): Facility's one-frame fog/black wedges with Bean
were guards' detailed guns (gevr_heldgun.c) drawn under float matrices read
as fixed point: every rebuilt guard took one of 48 slots, so a guard evicted
between its opaque draw and its alpha pass kept an unconverted pose. Only a
pose takes a slot now, and an evicted pending pose is converted first.
SpaceWarp's motion pass leaves out blended draws that write no depth (glass
no longer drags what is behind it) and cuts blended Z_UPD draws at 0.99.
GEVR_DYN_GFX_MIN 1 MiB (Silo crash 3706d27a); a Lepton container restart is
not a crash (79a638bb). Headset: 10 min Facility, 0 broken frames (~18
before), glass clean with SpaceWarp. [v0.5.10](docs/releases/v0.5.10.md),
[facility-fog-flash](docs/facility-fog-flash.md).

v0.5.9 (PR #206): teammates are always heard at full gain, so Horde and
co-op (everyone a teammate) use Couch gain whatever the host's Voice mode
(netVoiceModeForPair); the mode only reaches FFA opponents, and the launcher
greys it out in Horde, co-op and team battles. Gain is per listener, so
older peers keep the old rule until they update. Headset-accepted on the PR
build. [v0.5.9](docs/releases/v0.5.9.md).

v0.5.8 (PR #205): Quit from the Meta menu ends the process in
MainActivity.onDestroy after its own teardown, instead of SDLActivity's join
on a game thread that never returns on Android (every quit had ended in a
ten-second destroyTimeout; a Library launch in that window gave report
37093815's ANR). Headset: quit, quit + immediate reopen, hold-Menu relaunch;
exit-info shows SIGNALED status 9. [v0.5.8](docs/releases/v0.5.8.md),
[crash 37093815](docs/crash-37093815-quit-anr.md).

v0.5.7 (PR #201, PR #204 and the playtest fixes on their branch): Horde maps
Bunker I/II, Control and Facility and a starting wave (host's Horde rules),
Oddjob swarm (5, 15...) and boss rounds (10, 20...), armoured and Janus
enemies, faster and more accurate guards, two guns, a finite laser; every
openable door held open and unusable (a door start clears KEEPOPEN, so they
are re-opened if moved), stations clear of doors, one crate per three deaths
(no collision, taken at full ammo), 12 ragdolls at most in Horde. Facility's
green flashes: a multisampled eye drawn straight in reached the compositor
holding only its fog fill; every multisampled frame, the pause menu's too,
takes the one-pass replay ([facility-fog-flash](docs/facility-fog-flash.md)).
SpaceWarp: decals keep their surface's motion. Spawns everywhere go through
gevrSpawnSafeSpot (floor found from all rooms, clear of players/objects, a
pad in the room as fallback). Mirror Mode (Wreck) as campaign 3 with Bean and
HD packs flipped to match (gevr_mirror.c; lists from tools/gevr_mirror_lists.py).
Headset: Archives Horde and pause, Facility, posters, Mirror Mode with and
without Bean. Not played: the new Horde maps and rounds, two-headset Horde, a
Facility co-op join. [v0.5.7](docs/releases/v0.5.7.md),
[Mirror Mode](docs/mirror-mode.md), [Horde](docs/horde-mode.md).

v0.5.6 (PR #199): SpaceWarp slow motion below 120 Hz fixed, sustained-high
CPU on battery (ini CpuLevel), Bean pictures mipmapped, cutouts with
alpha-to-coverage, Anti-aliasing Off/2x. Ask "plugged in or on battery?" for
any stutter report. [v0.5.6](docs/releases/v0.5.6.md).

v0.5.4: Horde in Archives (PR #191; points, Mystery Box, Wall Buys, PR #193),
solo or 1-4 humans, revives, results and retry; the optional AI ally Natalya
(#194; solo, co-op and Horde, host-owned, spawns clear of players, not in
missions with their own Natalya); tracked remote heads and arms (PR #190);
Application SpaceWarp, on by default while Bean is; 4x eye anti-aliasing by
default (the launcher's 1.5x supersampling was tried and dropped); Bean draw
join (PR #192); wrapped launcher tooltips; Oddjob's hat 1.1 (PR #196/#197,
a Mods download). Headset: solo Horde, two-headset co-op with the ally at Dam,
SpaceWarp feel at 120 Hz. [Horde](docs/horde-mode.md),
[AI ally](docs/ai-ally.md), [v0.5.4](docs/releases/v0.5.4.md). v0.5.5: the ally's
settings moved to their own page, so Game rules no longer scrolls
([v0.5.5](docs/releases/v0.5.5.md)).

v0.5.3 (PR #187, #188): optional ragdolls/dismemberment (Game rules, local
to each headset, ported from Perfect Dark CE), GE-X rocket launcher decals.
[Ragdolls](docs/ragdolls.md), [v0.5.3](docs/releases/v0.5.3.md).

v0.5.0-v0.5.2 (PR #180, #183-#185): XBLA at 120 Hz on Quest 3 (early tick,
replayed eye pass), Bean parity from Dab's Mod, Wreck's blood patch, wounds on
Bean heads, the Dam speedboat on the water. [v0.5.0](docs/releases/v0.5.0.md),
[performance](docs/bean-performance-improvements.md),
[Bean backlog](docs/bean-parity-backlog.md).

v0.4.22's co-op start fixes (#164) still need two/four-headset starts, reloads,
host swaps and Jungle Natalya. [#164 evidence](docs/issue-161-164-campaign-fixes.md).

v0.4.20 merged PR #172: the hack campaigns, Project Bean, the Dam speedboat
and alarms. Headset checks covered Bean grips, gun fits and low doorways.
[PR #172](docs/pr172-handoff.md), [Bean](docs/project-bean.md),
[campaigns](docs/rom-hacks.md), [v0.4.20](docs/releases/v0.4.20.md).

Movement speed 50%-200% (solo and host settings; two-headset checks pending,
[movement speed](docs/player-movement-speed.md)); [body slots](docs/body-slots.md).

## What it is and what works

Native standalone Quest port of n64decomp/007, with Perfect Dark VR's port
layer and GEVR PC presentation. Player supplies an owned NTSC-U ROM.
Releases reach users through the headset updater and SideQuest. The site
is goldeneyevr.com, in its own repository.

- Solo stereo: headset camera, controller guns/muzzle fire, two-handed
  holds, scopes in either hand, gadgets/watch arm, patched hands, melee,
  HUD, snap/smooth turning and comfort. Flat/curved screen for menus/cinema.
- A/X cycles; grip + A goes back; hold A/X opens right/left weapon wheels.
  Solo dual guns use the game's inventory pair.
- Launcher: ROM pickers, play mode, display/comfort, gun fit, cheats,
  textures, updater, audio and microphone settings.
- Experimental multiplayer: LAN/direct IP and public/private internet
  lobbies, ICE/TURN, up to eight deathmatch players (bots fill slots) or four
  co-op players, synced combat/pickups, clocks/life IDs, late join, voting,
  host migration, names, teams and proximity/team voice (Opus/Steam Audio).
  Mute via launcher, watch or Menu + right B. See MULTIPLAYER.md.

## Outstanding issues / limits

- v0.5.4 headset checks still owed: Horde co-op (revives, late join, host
  loss), the ally in Horde and after host takeover, tracked arms on a second
  headset, Mystery Box/Wall Buys with more than one player.
- v0.5.7 checks owed: Bunker/Control/Facility Horde, swarm and boss rounds,
  two-headset Horde on this build, a co-op join on Facility. Runway autoguns
  still firing after a timed mine (report): propobj.c logs `autogun:` and
  `mine: blast` lines; get a log from the moment.

- #164: v0.4.22 fixes the logged vacant-body blocker; broader co-op mission
  starts and reloads remain to be confirmed. Keep the issue open for now.
- #161–#163: fixes released in v0.4.16; issues closed at v0.4.18 on the
  user's say. Only Natalya's solo movement/combat is headset-confirmed; the
  co-op checks in docs/issue-161-164-campaign-fixes.md are still owed.
- Co-op cutscenes on a teammate, revive, menus and debrief need continued
  two-headset testing; the invisible ending report remains open. Bond's
  cinema animation and script music stay on the host.
- #88: eight slots tested on one headset; measure 5+ headsets' frame time
  and upload before relay batching/culling. Remote hands do not animate.
- Bots: untested on two headsets (client view, mid-round replace, adoption);
  bots may stall at closed doors. MULTIPLAYER.md has the test script.
- In v0.5.14-test.14 (pre-release 2026-10-29; found by the dedicated
  server's bot runs, branch claude/dedicated-server-scope):
  kill-limit and YOLT ends no longer wait on deaths drawn on other headsets
  (a bot or remote last victim froze the match before the results); bots
  walk stairs, shoot glass when it is the only way out (Caverns' pad 6) and
  roam when nothing is reachable; 8-player extra spawns stay out of glass
  cases (Stack, Library). Headset check owed: "First to N" with bots.
- In v0.5.14-test.15 (pre-release 2026-10-29; found in headset tests of
  the dedicated server): spectators draw no hands and are not
  idle-kicked; a late joiner who takes a bot's place plays at once (its
  load made it spectate anyway); the spectator label shows in VR; bots
  step out of a swinging door that opened onto them.
- In v0.5.14-test.16 (pre-release 2026-10-29): the dedicated server merged
  in as experimental (docs/dedicated-server.md; Linux x86-64, build from
  source, no download). Fixes: bots no longer see or route through a closed
  door they lean on (test.15's door escape applied to every line test);
  bots held by an open door's leaf step off it instead of swinging it shut
  and open; game timers use CLOCK_MONOTONIC. Dedicated server before a
  self-hosting release: aarch64 build, packaging + systemd unit + operator
  guide, content hashes, Worker keyed listings (deploy needs an OK).
- In v0.5.14-test.17 (pre-release 2026-10-29, tag on ee7f9dbe; a test.16
  tester's report): the server's one-command setup (tools/gevr-server-setup.sh
  and .ps1, merged from claude/dedicated-server-scope; the server downloads
  link opus in: test.16's needed libopus0) and the ROM check on join; Bean's sky dome is drawn at infinity in stereo (at half
  the far plane its cloud cap was a disc overhead that followed the player,
  Surface 1/2; test.7's "near white band" was the same); the CCTV, whose
  crumpled mount Bean draws in its lens's list, now shows the crumpled,
  blackened mount (the only such prop of the 456). Headset: the Surface
  clouds confirmed by a tester.
- In v0.5.14-test.18 (pre-release 2026-10-29, tag on cfcfad14; servers built
  from that commit, gate 12/12 on Facility and Facility (Full)): the pause menu shows the
  match clock (time left / played); small props fade at the fog's real edge
  (FarFog over the visibility scale, Bean's own fog with Bean: Surface 2's
  cabin doors and cameras popped in); a destroyed Bean CCTV matches the
  cartridge (only the crumpled mount stays; its camera now on the body's
  list, not the lens's, which the broken glass switches off). Headset: the
  camera checked by the user (both modes, toggling). #226: Facility's
  gas-releasing tanks/barrels load as plain props in arenas (they were left
  out but their footprints stayed solid; start pad 127 is in the tanks'
  aisle); audio not fixed: the audio queue log line counts clipped dry-mix
  samples (suspect: netSpatialSample's pan up to 1.7x / HRTF over unity).
  Reporter to test test.18.
- In v0.5.14-test.19 (pre-release 2026-10-29, tag on 04d0a16a; servers from
  that commit, gate 12/12 on Facility (Full)): #226's locked door was the
  bottling room's decoder door (lock 8, which only the mission's script takes
  off). Arenas now open what a level's keys or scripts unlock
  (gevr_arena.c arenaScriptUnlocks: AI_DoorUnsetLock by tag; Facility 1,
  Control 6, Train 11 doors). Reporter asked to test on #226. #226 audio open.
- In v0.5.14-test.20 (pre-release 2026-10-30, tag on 7d4691d2; servers from
  that commit, gate 15/15): PR #227 merged as-is (roomGetProps one pass, list
  bounded) plus PTR_LIST_OBJECT_LOOKUP_INDICES_LEN = MAX_PROPS + 1; an empty
  dedicated server idles (bots still, 5 frames/s, 0.5% CPU) and wakes on a
  join. #226 audio measured off-headset (SDL_AUDIODRIVER=disk on a headless
  client with Linux Steam Audio, laser match): no clipping, cut-off clicks or
  HRTF aliasing; the user heard the recording clean; reporter asked for
  details. Prop fade (test.18) still not seen in a headset. Main untouched;
  #216/#218/#219/#226 and PRs #220-#225/#227 commented, not closed.
- In v0.5.14-test.21 (pre-release 2026-10-30, protocol 25 kept: test.20 and
  test.21 play together): Bunker II tester/user round. Bean ceiling guns
  vanish when destroyed (gevrBeanWreckHides), Bean level cast shadows
  see-through (DAB markShadows), Bean CCTV bracket stays on the base group.
  Props/guards/bodies fading at range (PropType 5/8, VR_FADE_DEPTH) draw
  depth first, then colour at LEQUAL: legs no longer vanish in front of a
  pile, the CCTV keeps its front and top. Body piles: cut Bean skins stay on
  the GPU (BEAN_CUT_KEPT 128), rdModel by address, cached pieces; Bean skins
  upload each distinct vertex once (41322 -> 8882); aux frame buffer 512 KiB
  (abort shooting a pile); Bodies stay max 24 (a saved 48 loads 24). Measured
  unattended with tools/perf/pile_drive.py (gevr_cheat.txt spawn<N>): Bunker
  II, 41 killed, 24 kept, 120 fps every second. All headset-checked by the
  user except the 24 cap (measured by script).
- In v0.5.14-test.22 (pre-release 2026-10-30, protocol 25 kept; don't mix
  with test.21 in a "(Full)" or Horde match): #228, Cradle (Full) started
  everyone on one platform. arenaPadOnFloor compared world units against
  Dab's -30..+60 setup-unit window; now scaled by level_scale. Cradle 2 ->
  122 spawn candidates, Egypt 6 -> 127; checked on the headless server.
  #228's crash (report 77652fd0, test.20): alSynAllocVoice wrote through a
  NULL __allocParam (SGI's one unchecked call) when the music stole a voice
  with the synthesizer's 128 updates spent; now no voice for that note.
  Headless stdout line-buffered (a split stats line failed the server gate).
  Tag on 039deddc; gates 15/15 on Facility (Full) and Cradle (Full); suite
  passes. #228 open. Not headset-checked.
- After test.22, unreleased: GE-X fist clench, Perfect Dark VR's FistClench
  ported (gun.c gevrGexClenchPart). The free off hand and the unarmed hand
  at rest close while their grip is held. The fingers come from GE-X's 1055,
  because GE-X's 1002 is a chop. Ini key renamed GexFistClench, default 1,
  since every old ini saved FistClench=0. Own view only, no protocol change.
  Not headset-checked.
- Two-headset checks still owed from #95 (closed): Statue/Cradle online,
  remote ledges and guns.
- #30: intermittent colored water lines; no confirmed fix. #85: Egypt
  Golden Gun room exit door can draw black; cryptdoor visibility logging.
- Hand reload, hip holster, grip to hand and game rules remain WIP;
  grip to hand/game rules need real-play and two-headset checks.
- GE-X: KF7/arms only; Dam headset play covered magazines/holds/fits/watch.
  Mid-level watch refresh and handedness changes still need headset checks.
- Debug markers: gevr_zdebug.txt, gevr_decal.txt, cryptdoor logging;
  surface probe disabled unless its marker is present.
- Solo Dam speedboat: grip at its edge (or B without grip use) to board at
  the north jetty, the move stick drives, grip at the island or jetty to step
  off. Hull is 1.75× retail. See [Dam speedboat](docs/dam-speedboat.md).
  Headset: jetty boarding, driving and the dock confirmed; a full crossing to
  the island landing still to play.
- Bean: the release's water shaders (bump, distortion, reflection) are not
  drawn; Dam's reservoir and Caverns' pools move as GoldenEye's water. Remaining
  120 Hz misses are in full-speed turns. Tower glass can pop in and out (not
  yet reproduced from the PC). Silo/Control floor
  materials are simplified;
  the grenade stays in GoldenEye's taser hand (the release has no hand
  holding one, user). Campaigns, speedboat and alarms need broader play.
- Use git worktree list for active checkouts.

## How to work on it

- Build: android/gradlew.bat assembleRelease; signing uses gitignored
  android/keystore.properties and the existing .jks. Preserve the key.
  On this Windows machine set JAVA_HOME=C:/Program Files/Java/jdk-20 each time.
  NDK 25.1.8937393, CMake 3.22.1; short junctions avoid long-path failures.
  Debug loop: tools/gevr_boot_test.ps1. Clean configure fetches SDL2/Opus.
- Release: bump versionCode/versionName, tag the build commit, release-sign
  GoldenEye-VR-vX.Y.Z.apk for the updater. Never install to a headset unasked.
- One branch per issue/feature. Commit before building for a useful build
  hash. User reviews in headset and says merge. Original game behavior stays
  unless authorized; prefer GEVR PC / Perfect Dark VR precedents.
- ROMs/model exports stay local. Hand patches contain additions/fingerprints,
  not cartridge meshes. Keep generated C and recipe/patch files consistent.
- Mixed line endings (* -text): preserve focused edits and compare diff stats
  with --ignore-cr-at-eol. Code: src/ game; port/src platform/input/audio/net;
  port/vr OpenXR/launcher; port/fast3d renderer; android/ Java; tools/ scripts.
- References beside primary repo: gevr-up, pdvr, gepc-ref, GoldenEye-007-HD,
  goldeneyevr.com. Port faults: byte order, pointer truncation, scale/frame
  mismatches. See docs/RARE-LOGO-AUDIO-HANDOFF.md and docs/gepc-port-guard-sweep.md.
