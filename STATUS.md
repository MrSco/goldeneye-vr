# GoldenEye VR: status

Rewrite this file in place and keep it under about 120 lines. Older session
logs are in docs/archive/; feature investigations keep their own notes.

**Current release:** v0.5.14 (versionCode 95, protocol 25), 2026-11-01.
Test builds from v0.5.14-test.22 play with it; v0.5.13 and older can't join.
Building does not publish a GitHub release. No game data is shipped.

v0.5.14 (PRs #220-#225, #227; pre-releases test.7 to test.25 from the batch
branch): all 26 maps in Horde and Deathmatch; "(Full)" campaign Deathmatch
maps (stage code LEVELID|NET_STAGE_FULL 0x40, [full maps](docs/full-deathmatch-maps.md));
arena doors per Dab's rules plus doors the level's keys or scripts unlock
(arenaScriptUnlocks), gas props as scenery, arenaPadOnFloor in setup units
(#228); bots on stairs, through glass, roaming, blind to closed doors, off
door leaves; kill-limit/YOLT ends (gevrDeathPlaysHere); late-join and
spectator fixes; pause-menu match clock; ROM hash in HELLO; wrecks never
draw whole (objDeform store 4500/120). Dedicated server, experimental
(headless build of the game, [operators](docs/dedicated-server-operators.md),
one-line setup from main, x86-64 and ARM64 tarballs per release). Bean:
release sky only on Surface/Dam/Runway, sky dome at infinity, fog in world
units, props fade at FarFog, CCTV wreck/bracket, see-through cast shadows
(black and grey <= 64), cut caps. Bodies stay max 24 with pile work on the
GPU; GE-X fist clench (GexFistClench). Crashes 0a705c81, 6acb6d38, 72eaa2c0,
77652fd0, 48253132 fixed; CLOCK_MONOTONIC timers; the watchdog logs a
`memory (...)` line each minute and at a stall (report 4616972e, cause
unproven). [v0.5.14](docs/releases/v0.5.14.md).

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
- v0.5.14 checks owed: "First to N" with bots in a headset; the fog-edge
  prop fade seen in a headset; #228 Cradle (Full) by its reporter; #226 sound
  (nothing measured, reporter asked for details); a dedicated server on real
  ARM and the Windows setup run by a player. Open: Surface I sky judder with
  Bean, Frigate engine-room judder.
- #229 (v0.5.14, Quest 3): the sound turns bit-crushed with an echo every
  second, from the start or after a while, and stays until a restart (likely
  #226's fuzz too). Suspect: the synthesizer's 128-update pool running dry
  (crash 77652fd0 found it empty; since 0f2751cf the game plays on and stops
  and fades get dropped). Branch claude/audio-update-pool-229 adds to the 2 s
  `audio queue` line: updates free (low, refused), voices, orphans (playing,
  owned by no sound) and the longest update queue on one voice. A 5-minute
  headless bot match: never below 72 free, nothing refused, no orphans.
  Reporter asked for a launcher report while it happens. Published as
  pre-release v0.5.15-test.1 (2026-11-02, vc 96, protocol 25 kept, APK only).
- Bean, Archives' bulletin board (user's screenshot, 2026-11-02): its papers
  showed through the cork in a fighting grain. The release draws a blended
  cork on the wall after three blended papers and shows the board bare; here
  both were decals pulled the same reach. A blended face wholly under later
  opaque blended faces is now left out (DAB's painted; the loader logs the
  count per level). Archives' count and the board unchecked in a headset.
  v0.5.15-test.2 (vc 97, protocol 25 kept, APK only) carries it with
  test.1's #229 logging; branch claude/trusting-mccarthy-6rlocr.
- Crash 8e53cb27 (v0.5.15-test.1, Quest 3, GE-X arms, bug just loaded):
  null def in gevrGexOffClench. The off hand's cache keeps the gun hand's
  header; a GoldenEye-only item in that buffer can reuse the root address,
  so the clench walked a rig with no GE-X definition. It now skips (as
  gevrGexOffCache does). Not reproduced in a headset. v0.5.15-test.3 (vc 98,
  protocol 25 kept, APK only), branch prerelease/v0.5.15.
- Crashes a8d242cd + 42ed2cf1 (one Statue Horde match vs Oddjobs, host on
  test.3, client on v0.5.14): Horde's markers (box, wall buys, ammo crates,
  briefcases; up to 37) took GoldenEye's 30 weapon slots, and weaponCreate
  freed the oldest when full, under Horde's pointer. They now have their own
  records (propobj.c gevrCreateItemInstanceIn); guards' guns get all 30
  slots back. v0.5.15-test.4 (vc 99, protocol 25 kept, APK only).
- Friends, presence and invites ([design](docs/friends-and-invites.md)):
  device identity and friend codes on the lobby Worker (goldeneyevr.com
  branch claude/friends-invites, src/social.ts, deployed 2026-11-04), a
  Friends sidebar right of the launcher, a Friends lobby visibility, and
  holding Menu now opens an in-game panel (back to launcher, HD textures,
  the VR settings moved off the watch, Friends). Protocol 25 kept: no game
  network change. v0.5.15-test.5 (vc 100, APK only).
- One layout for the launcher, the in-game panel and the online pause: a
  1280 surface with the Friends side panel (open or folded) to its right;
  the in-game panel takes the online pause's header (health and armour, solo
  too) and its settings tabs no longer scroll. v0.5.15-test.6 (vc 101).
- Link Discord (phase 3): Social SDK 1.10 in builds that have its library
  (build/discord/, not in the repo), sign-in exchanged by the Worker with the
  secret, Discord friends who both linked, Rich Presence. v0.5.15-test.7
  (vc 102); the headset sign-in is the open question.
- Friends: search by name (display names stay non-unique; Show me in search,
  on by default), your name and status at the top of the panel, ten renames
  a day; the in-game panel's text boxes get the headset keyboard.
  v0.5.15-test.8 (vc 103).
- Discord invites both ways (Rich Presence join secret, Invite from Discord,
  invites received, Join from Discord's app) and goldeneyevr.com/join links
  (goldeneyevr://join/CODE). Link Discord confirmed on the user's Quest once
  its Discord app was current. v0.5.15-test.9 (vc 104).
- #229 bit-crushed echo until a restart: Perfect Dark VR has it too (its #139;
  same audio.c); there Quest's recording captured clean sound and ending a
  recording cured it, so Android's resampling of the 22050 Hz stream is the
  suspect. Device now opened at 48000 Hz with the resampling in the game,
  plus Reset sound in the hold-Menu panel. v0.5.15-test.10 (vc 105).
- Force pull (docs/force-pull.md), single player, merged from its worktree
  (8d5111b0); online sync waits for the headset feel check. v0.5.15-test.11
  (vc 106, installed only, not published).
- v0.5.15-test.12 (vc 107, NETWORK PROTOCOL 26: test.12 joins only test.12,
  not v0.5.14 or the public servers): force pull online (NET_MSG_FORCE_PULL,
  host claims, pool props by item and place), the watch glow, disarm aim on
  the guard's hand; co-op guards' grenades and rockets on the clients
  (NET_MSG_COOP_SHOT, catchable); empty gun hand (clench, no sight, watch
  item counts empty); the rifle butt held two-handed, double damage;
  third person flat (#230 step 1); Streets jeep/lorry/Zil, posters, tank
  shake; Jungle Bean UVs at 1/512 (ladder); guards' detailed guns and
  ragdolls fade with the body; solo texture pool doubled; a decoded texture
  drops its address's stale upload (flat crosshair grid). Two-headset play
  of the force pull sync and the guard shots is still owed. Library (512?)
  and Caves (2048?) Bean UV scales measured, not checked against the N64.
- Crash reports land as issues in the private MrSco/goldeneye-vr-reports,
  decoded by its Digest workflow; each build's symbols zip goes to its
  "symbols" release.
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
