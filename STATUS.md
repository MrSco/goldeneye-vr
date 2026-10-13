# GoldenEye VR: status

Rewrite this file in place and keep it under about 120 lines. Older session
logs are in docs/archive/; feature investigations keep their own notes.

**Current release:** v0.4.21 (versionCode 77, protocol 21), 2026-10-12.
v0.4.16 through v0.4.20 can join; v0.4.15 and older can't. Building does not
publish a GitHub release. No game data is shipped.

v0.4.21: Tomorrow Never Dies 64 starts again (v0.4.20 crashed launching it,
then after the Rare logo): its moved animation records and Rare logo are read
where it put them; it loaded into its first mission in the headset. Co-op
starts (#164, bb8acfe): every player's start, the first included, is checked
against loaded scenery; two-headset starts still owed (Jungle, Train,
Control, Caverns, Cradle; Natalya following in Jungle). The reporter still
gets stuck in v0.4.21, including after reloads. A follow-up preserves the
checked gameplay position through the co-op intro and reconnects the retained
body at its end; native regressions pass, two-headset confirmation is pending.
See [#164 follow-up](docs/issue-161-164-campaign-fixes.md). Guards' and other
players' muzzle flashes (DetailedGuns) compare depth and no longer show
through trunks and walls; confirmed in the headset. Notes:
[v0.4.21](docs/releases/v0.4.21.md).

v0.4.20 merged PR #172: the Goldfinger 64 / Tomorrow Never Dies campaigns
(solo, the player's own patched ROMs), Project Bean (#170), the Dam
speedboat (#171), Dam wall alarms (#174) and Bean's GPU skinning and world
batching. Bean is imported from the player's own XBLA copy (Mods, "Choose
copy..."; nothing is downloaded; the old Bean releases are deleted and that
repo is private). The last round, played in the headset: Bean's arm holds the
gun with both hands, Gun fit per gun and placed by hand (the off hand's
trigger), the user's Bean fits as defaults, low-doorway clearance for tall
characters (online and on stairs), and the host's stage dropdown. Start with
[PR #172 handoff](docs/pr172-handoff.md); setup:
[Project Bean](docs/project-bean.md), [hack campaigns](docs/rom-hacks.md).
Notes: [v0.4.20](docs/releases/v0.4.20.md).

Movement speed: 50%-200%, 100% default, separate solo and host settings;
host changes apply to the whole session, bots included. Room-scale walking
is unchanged. Native checks pass; two-headset checks pending. Details:
[movement speed](docs/player-movement-speed.md). Launcher Match and Comfort
use two columns, and the eight-player lobby fits without scrolling. Chest
and Belt defaults use saved Quest fits at 170 cm; custom fits stay intact.
See [body slots](docs/body-slots.md).

## Recent releases

**v0.4.21:** TND64 starts again, co-op starts clear of scenery (#164), guards'
muzzle flashes hidden behind the world. Notes:
[v0.4.21](docs/releases/v0.4.21.md).
**v0.4.20:** Project Bean visuals from the player's XBLA copy, Goldfinger 64
and TND campaigns, Dam speedboat and alarms, per-gun Gun fit placed by hand,
low doorways. Notes: [v0.4.20](docs/releases/v0.4.20.md).
**v0.4.19:** hand/scope tracking, mission gadgets, watch laser, ROM validation
and startup crash fix. Notes: [v0.4.19](docs/releases/v0.4.19.md).
**v0.4.18:** Touch controllers on the watch's Control page, off-hand trigger
no longer aims (PR #166). Notes: [v0.4.18](docs/releases/v0.4.18.md).
**Earlier:** v0.4.17 crash fix fb1c9718, off-hand GE-X scope lens; v0.4.16
live movement speed (protocol 21), two-column launcher, co-op fixes #161-#164
(PR #165); v0.4.15 body slots, hand reloads, two-handed grips; v0.4.14 bot
doors, soft collision, hit immunity (#153); v0.4.13 crash fixes (#151);
v0.4.12 detonator watch fit; v0.4.11 bots, 3D audio and GE-X for every weapon
(#133-#149); v0.4.10 wall movement, co-op gadgets (#127, #132); v0.4.9 smooth
locomotion, GE-X muzzles, belt reloads (#124-#126); v0.4.6-v0.4.8 settings
persistence, GE-X KF7 and hand reload, watch settings (#110-#121);
v0.4.0-v0.4.5 four-player co-op, eight-player deathmatch, weapon wheels,
gadgets, passthrough. Notes for each are in docs/releases/; older evidence in
docs/playtest-* and Git history.

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

- #161–#164: fixes released in v0.4.16; issues closed at v0.4.18 on the
  user's say. Only Natalya's solo movement/combat is headset-confirmed; the
  co-op checks in docs/issue-161-164-campaign-fixes.md are still owed.
- Co-op cutscenes on a teammate, revive, menus and debrief need continued
  two-headset testing; the invisible ending report remains open. Bond's
  cinema animation and script music stay on the host.
- #88: eight slots tested on one headset; measure 5+ headsets' frame time
  and upload before relay batching/culling. Remote hands do not animate.
- Bots: untested on two headsets (client view, mid-round replace, adoption);
  bots may stall at closed doors. MULTIPLAYER.md has the test script.
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
- Solo Dam speedboat: board at the north jetty (B, or a hand on the hull),
  ride to the island, B to step off. Hull is 1.75× retail. See
  [Dam speedboat](docs/dam-speedboat.md). Headset crossing still to confirm.
- Bean: Dam/Complex water and Silo/Control floor materials are simplified;
  the grenade stays in GoldenEye's taser hand (the release has no hand
  holding one, user). Campaigns, speedboat and alarms need broader play.
- Use git worktree list for checkouts. Unmerged: feature/xbla-hd,
  fix/30-water-sky; claude/gex-watch-sleeves is parked, not for merge.

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
