# Crash a16b05b5: co-op Bunker I Statistics

Report: v0.4.6, commit `9fb7be1`, Quest 3, player GKnightPR. The attachment
`gevr-a16b05b5.txt.gz` contains base64-encoded gzip data. Its contents were
treated as diagnostic evidence. The decoded log remains outside the repository.

## Confirmed crash

The separate tombstone is unavailable, but the attached app logcat contains
the native crash backtrace. At October 4, 2026, 20:34:41 EDT
(`2026-10-05T00:34:41Z`), SDLThread receives SIGSEGV / SEGV_MAPERR:

```
fault address: 0x31; x0: 0
fileGetSaveStageDifficultyTime + 164
constructor_menu0D_missioncomplete + 552
lvlRender + 304
bossMainloop + 1708
```

Native build ID: `33f87a26378a6058ad7a6b2fe8aec8fb034eae30`.

This headset is the co-op client in slot 1. It starts the party's title
stage at 20:28:39, then loads Bunker I (level 9), Secret Agent at 20:28:54
while still on File Select. That screen uses `selected_folder_num = -1`
until the player chooses a folder; the host can start the mission first.

The mission completes at 20:34:34. At 20:34:38 the party returns to the
debrief; the background logs `folder=-1` through 20:34:41. Then the client
follows the host to screen 13, Statistics, and crashes.

`fileGetSaveForFoldernum(-1)` returns NULL. Statistics passes that pointer
to `fileGetSaveStageDifficultyTime`, which dereferences the packed time
bytes without checking it. Bunker I's save index is 4; Secret Agent is 1.
The time begins at bit `(1 * 20 + 4) * 10 = 240`, byte 30 of `times`.
With `times` at offset 18, its second byte is at offset 49 (`0x31`),
matching the fault address. An invalid folder also indexes outside
`folderpositions` in the debrief background and prevents completion from
being saved to a valid local folder.

## Local changes

- Before applying any co-op stage configuration, preserve a chosen local
  folder in the range 0–3. If the picker was bypassed or the folder is
  invalid, use this headset's first folder. The host supplies no save-folder
  index. This covers starting before the client chooses, joining a mission
  directly, and returning to the menus.
- A NULL save returns zero (no recorded best time) from
  `fileGetSaveStageDifficultyTime`, including 007 mode. Statistics already
  hides the best-time field for zero. Existing save times keep their
  original decoding.
- Folder preparation does no save I/O: configuration also runs in the
  launcher before game initialization. Existing boot and folder-screen
  validation still reads this headset's saves.

Protocol and release version are unchanged. Implementation branch:
`codex/fix-a16b05b5-coop-debrief`. No headset installation was performed.

## Ending cutscene

The same log records the host's no-control state at 20:34:26, its cutscene
camera at 20:34:27 (pad 103), and `stereo: off` at 20:34:27. Thus the
cutscene state reached the client and it left stereo before the mission
ended. The crash occurs afterward in Statistics.

These messages do not prove that the cutscene rendered visibly. The
attachment contains no camera-coordinate/fade trace or headset capture
that establishes why it was invisible. No speculative cutscene change was
made, and the save fix is not claimed to repair that symptom.

## Validation

`python port/tests/test_coop_debrief.py --verify-baseline` passes against
production save queries, folder preparation and co-op stage configuration.
It covers NULL and missing saves, every mission's packed best times at
the three regular difficulties, 007 unlock handling, the reported Bunker
start/return, preserving each local folder, and invalid/drop-in selections.
It also confirms that the v0.4.6 query crashes on the report's NULL-save
call and that v0.4.6 stage configuration leaves the missing folder invalid.
The harness stubs EEPROM I/O and unrelated world functions; it does not
verify persistence on a headset.

The existing multiplayer native suite passes all 60 tests. All three
modified C translation units compile with the Android ARM64 release flags
and NDK 25.1.8937393, using existing cached dependency headers. This is a
compile check, not a complete APK build or headset playtest.

Headset acceptance: start Bunker I / Secret Agent while a joiner is still
choosing a folder, complete it, and open debrief and Statistics on both
headsets. Repeat with a chosen non-default client folder and a client that
joins mid-mission; verify completion and times in each headset's own save.
Inspect the ending cutscene on the joiner separately, especially whether
the virtual screen is black, misplaced, or displaying the wrong camera.
