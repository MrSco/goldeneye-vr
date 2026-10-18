# Optional campaigns: Goldfinger 64, Tomorrow Never Dies and Mirror Mode

GoldenEye VR can start **GoldenEye 007 (USA)**, **Goldfinger 64 1.0**, **Tomorrow Never Dies 64 Expanded 06-22**, or **Wreck's Mirror Mode (August 2017)** from the launcher. The four cartridges stay installed together. The campaign dropdown chooses which one Start loads, and that choice is remembered. You do not replace `ge.z64` to switch.

No hack ROM, patch, or converted asset is in the APK or the repository. Patch your own GoldenEye 007 (USA) dump on your computer and copy the result to the headset. Do not upload a ROM to GitHub or a bug report.

## Which release

The loader accepts these, matched on size, title, game code, and the eight CRC bytes:

| Campaign | File on the headset | Size | Patch to apply to GoldenEye 007 (USA) |
| --- | --- | --- | --- |
| GoldenEye | `data/ge.z64` | 12 MB | none |
| Goldfinger 64 | `data/goldfinger64.z64` | 24 MB | `Goldfinger64_1_0.xdelta` (from `goldfinger64.zip`) |
| Tomorrow Never Dies | `data/tnd64.z64` | 16 MB | `expanded/TND64_Expanded_06-22.xdelta` (inside n64vault's `tnd64.zip`) |
| Mirror Mode (Wreck) | `data/mirror.z64` | 12 MB | `GE_MirrorMode_patch.xdelta` (inside [`mirrormode[wreck].zip`](https://n64vault.com/ge-levels:mirror-mode)) |

Apply the patch to a big-endian `.z64` GoldenEye 007 (USA) ROM with any xdelta patcher (Delta Patcher, `xdelta3 -d -s ge.z64 patch.xdelta out.z64`, or `python tools/gex/vcdiff_apply.py ge.z64 patch.xdelta out.z64`). A file with the right size and the wrong CRC is rejected.

Tomorrow Never Dies: only **Expanded 06-22** is supported. The newer 07-23 and 11-24 patches on the n64vault page and the Original V1-V4 releases build different ROMs, and are rejected.

Mirror Mode: use Wreck's full August 2017 **Mirror Mode** patch, not the older
Mirror Multi patch. It supplies mirrored backgrounds, collision, setups and
objects. The port also runs its watch clock backwards. The port currently offers
this campaign in solo play; the N64 patch's multiplayer support does not enable
VR online play. See [Mirror Mode notes](mirror-mode.md).

## On the launcher

1. Install your GoldenEye 007 (USA) ROM the usual way, so Start can fall back to it.
2. Open the campaign dropdown to the left of **START** and choose **Add Goldfinger 64 ROM...**, **Add Tomorrow Never Dies ROM...**, or **Add Mirror Mode ROM...**, or copy the file into `data/` as named above. Each file is checked and stored under its own name. The retail ROM is left where it is.
3. Choose the campaign in the same dropdown. Start loads that cartridge. The next launch keeps the same selection.
4. A campaign whose ROM is missing is listed as not installed; if the selected one disappears, the dropdown returns to GoldenEye. If it is installed but fails to load, the game starts GoldenEye and the line under START says so next time.

## What a hack campaign leaves out

A hack campaign is solo play with the hack's own look:

- **Online play, co-op and bots** are off. Both headsets would have to run the same hack, and the network does not check that yet.
- **GoldenEye X**, **XBLA / Bean** and the **HD texture packs** are off for that run. They are made for GoldenEye's own models and textures. Your settings for them are kept for the next GoldenEye run.
- **The launcher's cheat list** is off: it names GoldenEye's guns, and the hack puts its own on those items. The game's own Cheat Options, **Unlock all missions and cheats** and the VR gun size still work.
- **Progress** is saved separately (`eeprom-gf64.bin`, `eeprom-tnd64.bin`, `eeprom-mirror.bin`), so a hack's missions never unlock GoldenEye's or the other way round.

Guns use the hack's own models and stats.
