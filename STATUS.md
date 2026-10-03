# GoldenEye VR: status

Rewrite this file in place and keep it under about 120 lines. Older session
logs are in docs/archive/; feature investigations keep their own notes.

**Updated / latest release:** 2026-10-02, v0.3.10; versionCode 50, protocol 15.
The release tag identifies the signed build commit. No game data is shipped.

**v0.3.10:** regular watch arm stays visible throughout gripping; only the
original right palm and animated pressing finger draw from the private
combined-model copy. Its old left hand, all six small sleeves/undersides,
and embedded watch face/band are hidden. Snap entry/retention are 16/22 cm.
Headset candidate `57f00e5` accepted; #60 and #75 closed at the maintainer's
request. No separate close-wall crosshair/auto-crouch adjustment was made.
See docs/issue-60-watch-grip-hand.md. Water preserves projected positions
and UVs as floats, and stereo reflections transform the body axes into
view space; water and Dam truck glass accepted on `942e897`. See
docs/issue-30-surface-capture.md. HD + AI pack catalog: 2026.09.29.2.

**Recent releases:** v0.3.9 added scopes in either/both hands, snap-turn
vignette, watch arm in the left weapon panel, solid panel hands, corrected
room decals (#72), HD texture cache/import fixes, closer bullet sparks and
smoke, late-join spectator and watch-panel crash fixes. v0.3.8 corrected
crash reporting to use APK install time and exported Android exit history.
v0.3.7 added clock/life-ID combat synchronization (protocol 15), independent
hand cycling, launcher changes, TURN fallback and shots from eye depth.
Earlier evidence remains in docs/playtest-* and Git history.

## What it is and what works

Native standalone Quest port of n64decomp/007, with Perfect Dark VR's port
layer and GEVR PC presentation. Player supplies an owned NTSC-U ROM.
Releases reach users through the headset updater and SideQuest. The site
is goldeneyevr.com, in its own repository.

- Solo missions in true stereo: headset camera, controller guns/muzzle
  fire, two-handed holds, real scopes in either hand, gadgets/watch arm,
  patched hand shells, melee, HUD, snap/smooth turning and comfort at 90 Hz.
  Menus/cutscenes can use the flat or curved virtual screen.
- Weapon controls: A/X cycles, grip + A goes back, hold A opens the weapon
  panel, hold X the left panel. Solo dual guns use the game's inventory pair.
- Launcher: ROM picker, play mode, turning/comfort, gun fit, unlock-all
  (RAM only), textures, updater, music/SFX/voice and microphone settings.
- Multiplayer is experimental: LAN/direct IP and public/private internet
  lobbies, ICE with TURN fallback, four-player match setup, synced weapons,
  damage/deaths/respawns/pickups, clocks/life IDs, late-join spectators,
  ballots, host migration, names/tags, teams and proximity/team voice.
  Opus uses its own ENet channel; Steam Audio supplies binaural direction.
  Lobby voice is full-volume; match voice varies by distance/visibility.
  Mute via launcher, watch/pause menu, or hold Menu + right B.
  Details: MULTIPLAYER.md and docs/multiplayer-voice-teams-hud.md.

## Outstanding issues / limits

- VR crosshair depth, headset-accepted 2026-10-02, unreleased: trace in
  unscaled collision coordinates and scale the hit once for stereo. Enemy
  sights use the front surface, and the sight shares bullets' eye-depth
  origin so a muzzle through a wall cannot aim beyond it. Native aim probe,
  debug and signed release builds passed; release certificate verified.
- #89 Cradle layering, merged to main 2026-10-02, unreleased: blended room
  surfaces (railings, truss sides) write depth where opaque, and portal-less
  levels draw the blended pass far to near (smoke). Headset-accepted: Cradle
  truss view (DRAWS 207), Dam glass, explosions fine. Issue left open.
- #74 ammo crates, merged to main 2026-10-02, unreleased: the setup converter
  copied a multi-ammo crate's {u16 model, u16 quantity} slots as words,
  swapping each pair, so every solo crate gave every ammo type (mines,
  knives, grenades). Slots now swap as halfwords; tools/gevr_setup_probe.py
  covers it. The v0.3.4 multiplayer slot clearing stays. Headset-accepted on
  the signed `bce26d2` build: Runway's start crates give only their own ammo.
  The issue is closed on GitHub; the reporter's reopen request was not acted on.
- #30: intermittent colored water lines cleared on launcher restart and
  did not recur in final capture; still unresolved. Animation/glass fixed.
- #85: Egypt Golden Gun room exit door can draw black. The cryptdoor log
  targets room/portal visibility; no confirmed fix yet.
- #9 hand shells, merged to main 2026-10-02, unreleased: modelled in Blender
  (tools/blender/gevr_hands_author.py; tools/handpatch/NOTES.md). The PPK
  family: missing finger joints, trigger-finger underside, a heel pad filling
  the hollow under the little finger, the forearm's cut end in skin. The
  watch arm: ring and little finger palm sides, the fingers curled into a
  fist, index creases, a palm in the heel's skin. The taser/grenade hand:
  back of the hand and forearm in the skin beside them, finger cracks zipped,
  the index knuckle's notch rounded; the grenade's see-through bottom drawn
  solid. All headset-accepted ("looks good"). Next: the other pistol hands
  (golden gun, Cougar, DD44, knives). Issue left open.
- #23: multiplayer stays open/experimental. Remote hands do not animate;
  held guns are the original low-detail third-person models. Host migration
  and mixed-network/four-player cases need broader headset coverage.
- #32 room-box shot pretest, #73 NPC ground callback table, #64 watch-arm
  haptics, #24 mirrored fist white face, #81 AimNoLean and #72 decals were
  corrected before this release. Laser/AR33 and other penetrating weapons
  retain the original game's object penetration rules.
- Debug controls remain: files/gevr_zdebug.txt and cryptdoor logging; the
  optional surface probe is disabled unless its marker is present.
- Use `git worktree list` for active checkouts; #30 and #60 branches are
  merged, and are not pending work.

## How to work on it

- Build: android/gradlew.bat assembleRelease; signing uses gitignored
  android/keystore.properties and the existing release .jks. Preserve the
  key: changing it forces uninstall. Local Java is JDK 20; NDK 25.1.8937393
  and CMake 3.22.1. A short junction can avoid Windows long-path failures.
  Debug loop: tools/gevr_boot_test.ps1. A clean configure fetches SDL2/Opus.
- Release: bump versionCode and versionName; tag equals the build commit;
  asset name must be GoldenEye-VR-vX.Y.Z.apk for the updater. Release-signed.
- One branch per issue/feature. Commit before building so the launcher hash
  identifies the code; the user reviews in the headset and says merge.
  Never install to a headset unasked. Original game behavior remains unless
  the user authorizes a change; prefer GEVR PC / Perfect Dark VR precedents.
- The ROM and model exports are local only and never committed. Hand patches
  store additions/fingerprints, not cartridge meshes. Keep generated C and
  patch/recipe files consistent.
- Files use mixed line endings (`* -text`); preserve them for focused edits.
  Compare diff --stat with diff --stat --ignore-cr-at-eol before committing.
- Code map: src/ decomp; port/src platform/input/audio/net; port/vr OpenXR,
  launcher/settings; port/fast3d renderer; android/ app/Java bridges;
  services/lobbies Worker; tools/ development scripts.
- References beside the primary repo: gevr-up, pdvr, gepc-ref, GoldenEye-007-HD
  and goldeneyevr.com. Common port faults: N64 byte order, truncated pointer
  slots and scale/frame mismatches. See docs/RARE-LOGO-AUDIO-HANDOFF.md and
  docs/gepc-port-guard-sweep.md.
