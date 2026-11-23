# iCloud sync (Apple builds)

User idea, 2026-11-22: load the ROM (and packs) on one Apple device and have
the controls, settings and everything else follow on the others. Decided scope
(user): settings, saves and the ROM by default; HD, AI, Bean and other heavy
packs only when the player opts in.

iPhone/iPad, Apple TV and Vision Pro are one App Store Connect app
(`com.mrsco.goldeneyevr`), so one iCloud store reaches all three. Android, PC
and the server are untouched (the header's calls are empty there).

Code: [port/ios/ios_icloud.mm](../port/ios/ios_icloud.mm), interface
[port/include/gevr_icloud.h](../port/include/gevr_icloud.h).

## What moves, and how

| What | Where in iCloud | When |
| --- | --- | --- |
| `goldeneye-vr.ini` | key-value store `ini` (shared) + `ini@ios/tv/vision` (device kind's own) | merged at launch; each save pushes its changed keys |
| `goldeneye.ini` (binds, crosshair...) | `cfg` + `cfg@<family>` | the same |
| `eeprom*.bin` (game saves) | `save:<file>` (bytes + time) | merged at launch; each save pushes |
| ROMs (`*.z64/v64/n64` in the save folder and `data/`) | CloudKit private database, zone `GevrFiles` | at launch, on return to the foreground, after a ROM is chosen, every 15 min |
| Texture packs (`texture-packs/<pack>/`: HD, AI, Bean, mods) | the same, only with **Big packs too** on | the same, Wi-Fi only |

tvOS has no iCloud Drive (Apple QA1935), so files go through CloudKit on every
platform. The key-value store holds 1 MB and 1024 keys: the settings are a few
dictionaries, not one key per setting.

**Per device kind** (not shared): frame rate, display Hz, SpaceWarp, MSAA,
CPU level, stats, the flat screen's size/distance/shape/curve/passthrough,
touch controls and layout, play mode, seated mode, height, world scale, HUD
distance, stereo crosshair, pad-default and look-upright markers, mic mute,
LiveSplit, and the two iCloud rows themselves; in `goldeneye.ini` the `Video.`,
`Audio.`, mouse and input-backend keys. Everything else (controls, look speed,
vibration, gameplay, cheats, gun fits, multiplayer choices, the player name,
the active pack, Bean mode) is shared.

**Joining.** A device pushes nothing to the key-value store until it joins:
when the store already has the other devices' settings (they win; only keys
the cloud lacks go up), or when CloudKit has no `GevrJoined` record (it is the
first device; its settings go up and it leaves the record). Otherwise it waits;
settings that arrive late are used from the next launch. (First device test,
2026-11-22: a new iPad whose store had not come down in 5 s pushed its own
settings over the iPhone's. Store names are `ini2`/`cfg2`/`save2:` since.)

**Settings** merge key by key against `.gevr-icloud/last-*` (the copy last
synced): a key changed here goes up, otherwise the cloud's comes down. Settings
changed on another device while this one runs apply at its next launch.

**Saves are never merged** (user: choose the right one). The save the game
wrote last wins, by the file's own time carried in the store and put back on
the file when it comes down. A save with no progress (no level time, cheat or
007 mode in a used slot of GoldenEye's EEPROM layout) never replaces one with
progress. A replaced save is kept as `eeprom.bin.before-icloud-<date>`, the
last five.

**Files.** An item is a `GevrItem` record (path, stamp, size, chunk count,
generation, content hash for ROMs) and `GevrChunk` records of 32 MB, each one
asset, holding a small archive (one entry for a ROM, every file for a pack).
Chunks are written before the item, so a reader never sees half an upload; a
re-upload is a new generation and the old chunks are deleted after. The item
record is read before and after an upload: if another device put the same
content there meanwhile, ours is dropped. Chunks no item names and a day old
are deleted on each pass. A
download whose target is already there (the game may be using it) waits in
`.gevr-icloud/ready/` and is put in place at the next launch. Same content on
both sides (ROM hash, pack file count and size) is recorded as synced without
moving. Nothing is deleted from iCloud when a file is removed on a device.

Launcher: **Play** tab, under Show stats: **iCloud sync** (on) and
**Big packs too** (off), shown only in a build signed for iCloud. The ROM box
shows the status line ("iCloud: downloading ge.z64 40%") and a one-time
message when a ROM arrives; the launcher's ROM scan picks it up.

## Turning it on (Apple developer account, once)

Builds are unchanged until a build script runs with `GEVR_ICLOUD=1` on a
signed build (device dev install or archive); the simulator never gets it.

1. App ID `com.mrsco.goldeneyevr` (team 449Y259APN only): enable **iCloud**
   with **CloudKit**, and create/assign container `iCloud.com.mrsco.goldeneyevr`.
   (Xcode's automatic signing with `-allowProvisioningUpdates` can do both for a
   development build.)
2. Make the App Store profile "GoldenEye VR App Store" again so it carries the
   iCloud entitlements; development profiles likewise.
3. CloudKit Console: after the first development build has synced once (that
   creates the `GevrItem` and `GevrChunk` record types), **Deploy Schema
   Changes** to Production. TestFlight builds use Production; development
   builds use Development, so the two never see each other's files (the
   key-value store has no such split).
4. Build with `GEVR_ICLOUD=1` (`tools/build_ios.sh`, `build_tvos.sh`,
   `build_visionos_native.sh`).

## State

- 2026-11-22, branch `t3/icloud-settings-sync`: App ID iCloud capability (API)
  and container (user, developer site) done; development profiles "GEVR iCloud
  Dev iOS/tvOS" made through the API (Xcode cannot log in with the team key;
  `~/gevr-perf/asc.py` on the Mac). Signed dev builds on the iPhone, iPad and
  Living Room Apple TV (`~/src/gevr-icloud-dev-ios.command`, `-tvos.command`).
- Device test: iPhone joined first (settings + save up, 6 ROMs, Bean, HD-AI,
  Oddjob hat up); iPad joined and took the iPhone's settings, matched the ROMs
  and packs without downloading; identical saves left alone. Apple TV pending.
- `bash port/tests/test_icloud.sh` (Mac, iOS simulator) passes.
- v0.5.26 (2026-11-23): the App Store profiles "GoldenEye VR App Store" and
  "GoldenEye VR tvOS App Store" had gone INVALID when the App ID gained iCloud;
  remade through the API with iCloud (uuids 52cdc231..., ab61ee6e...) and the
  TestFlight archives built with `GEVR_ICLOUD=1`. The CloudKit schema (GevrItem,
  GevrChunk, GevrJoined) was deployed to Production the same day, for TestFlight's
  ROM and pack sync (the key-value store needs none).
  TestFlight iOS 148 and visionOS 23 have iCloud. tvOS 9 went up without it: App
  Store validation wants `aps-environment` beside iCloud on tvOS (error 90575), so
  Apple TV needs the Push Notifications capability, a remade profile and that
  entitlement in its build.
- Open: deleting from iCloud; speedrun ghosts (`ghosts/`) are not carried.
