# Bean: the XBLA Community Edition

The GoldenEye XBLA Community Edition is the community's set of fixes to the
leaked XBLA build: new skydomes, Frigate's water, props, doors, icons and
text. It is only distributed as patches against the release
(`CommunityEditionUpdaterV6.zip`, HDiffPatch files), and the port keeps to
that: nothing of it ships with the APK or is downloaded. The player applies
their own copy of the updater to their own imported copy of the release.

This is Dab's Mod's design and code (`gebeance.c` and its HDiffPatch glue,
dabs-mod aae28c8, MIT), ported.

## For the player

1. Import GoldenEye XBLA on the Mods page (**Choose copy...**). A copy imported
   before this change has to be imported again, because the import now keeps
   the files the update patches.
2. Press **CE...** in the XBLA / Bean row and pick
   `CommunityEditionUpdaterV6.zip`. The row shows "Applying CE..." for a few
   seconds.
3. The **CE** box appears in the same place, ticked. Unticking it goes back to
   the release's files from the next level.

Importing the copy again removes the update; apply it again after.

## How it works

- **Import** (`port/src/gevr_bean_import.c`): besides Bean's own files, the
  import keeps the 145 files the update reads from (about 140 MB, listed in
  `port/src/gevr_bean_ce_refs.inc.h`). The copy itself is still not kept.
- **Apply** (`port/src/gevr_bean_ce.c`, `ModManager.applyCe`): the zip's
  `CEUpdate/files.diff` is read into `texture-packs/ge007-bean/ce/`. Every
  file it reads from is checked to be present, then `hdpApplyOverlay()` writes
  what it adds, changes or renames to `ce/files.new/`. The patch checks its own
  checksums of everything it reads, so it applies only to an unmodified
  "Fyodorovna" release; anything else is refused and nothing is kept. The
  levels' collision meshes (`*_hits`) are dropped, as the import drops them.
  Then the overlay is moved to `ce/files/` and `ce/applied` (the zip's size)
  is written last. The diff is deleted.
- **Read** (`gevrBeanCeRoot()`): with `ce/applied` present and
  `BeanCommunityEdition=1` in the ini (the default), the Bean loaders try
  `ce/files/` before the release's files: `gevr_bean.c` and
  `gevr_bean_world.c` `findFile()`, the fonts (`gevr_bean_font.c`) and the
  pictures (`gevr_bean_images.c`). The world's existing CE sky choice
  (`communityEdition()`, `skyCe`) now follows the overlay.
- **HDiffPatch**: the patch half, vendored whole from Dab's Mod in
  `port/external/hdiffpatch` (see its README), built over the port's own LZMA
  SDK.

On the PC, against the release in `added-content/xbla/files`: 148 files
written in under 5 seconds, 125 kept (118 MB) after the collision meshes. The
same 125 files come out when only the 145 reference files are present, as on
the headset. A copy with four bytes changed in one prop is refused (step -6).

On Quest 3 (2026-10-19): the retail zip imported again, the updater applied
from the Mods page, and the Community Edition's content showed in game.

## Differences from Dab's Mod

- Dab's Mod reads only the overlay's art (`ceArtFiles`), because it rebuilt
  the Community Edition's fixes into its own source. The port has not, so it
  reads the whole overlay.
- The executable's patch (`xex.diff`) is not applied; the import keeps no
  `default.xex`. Its only use in Dab's Mod is the levels' fog, and that is
  built in instead (`gevr_bean_fog.h`, the values Dab's Mod read off the
  patched table), used while the overlay is read since `communityEdition()`
  follows it: distances Statue 5000, Archives, Train and Jungle 4500, Temple
  22500, Surface 2 6500; colours Statue 0x090708, Jungle 0x797c79, Temple
  0x103060, Cradle 0x6e8196, and Surface 2 its sf2 dome's horizon 0x575f5d.
  The player's `default.xex` here (7.3 MB) is not the 8.9 MB one the patch
  expects, so these could not be re-read from it. Bean's fog is a colour
  blend only and culls nothing, so none of this changes the frame rate.
- Dab's Mod finds the zip in `added-content/`. Here the player picks it on the
  Mods page.
