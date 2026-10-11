# Goldfinger 64 and Tomorrow Never Dies

GoldenEye VR can start **GoldenEye 007 (USA)**, **Goldfinger 64 1.0**, or **Tomorrow Never Dies 64 Expanded 06-22** from the launcher. The three cartridges stay installed together. The campaign radio chooses which one Start loads, and that choice is remembered. You do not replace `ge.z64` to switch.

No hack ROM, patch, or converted asset is in the APK or the repository. Patch your own GoldenEye 007 (USA) dump on your computer and copy the result to the headset. Do not upload a ROM to GitHub or a bug report.

## Which release

The loader accepts these, matched on size, title, game code, and the eight CRC bytes:

| Campaign | File on the headset | Size | Patch to apply to GoldenEye 007 (USA) |
| --- | --- | --- | --- |
| GoldenEye | `data/ge.z64` | 12 MB | none |
| Goldfinger 64 | `data/goldfinger64.z64` | 24 MB | `Goldfinger64_1_0.xdelta` (from `goldfinger64.zip`) |
| Tomorrow Never Dies | `data/tnd64.z64` | 16 MB | `expanded/TND64_Expanded_06-22.xdelta` (inside n64vault's `tnd64.zip`) |

Apply the patch to a big-endian `.z64` GoldenEye 007 (USA) ROM with any xdelta patcher (Delta Patcher, `xdelta3 -d -s ge.z64 patch.xdelta out.z64`, or `python tools/gex/vcdiff_apply.py ge.z64 patch.xdelta out.z64`). A file with the right size and the wrong CRC is rejected.

Tomorrow Never Dies: only **Expanded 06-22** is supported. The newer 07-23 and 11-24 patches on the n64vault page and the Original V1-V4 releases build different ROMs, and are rejected.

## On the launcher

1. Install your GoldenEye 007 (USA) ROM the usual way, so Start can fall back to it.
2. Under **CAMPAIGN**, use **Choose ROM** on Goldfinger 64, Tomorrow Never Dies, or both. Each file is checked and stored under its own name. The retail ROM is left where it is.
3. Select the campaign. Start loads that cartridge. The next launch keeps the same selection.
4. If the selected hack is missing, the radio returns to GoldenEye and says so. If it is installed but fails to load, the game starts GoldenEye and the launcher says so next time.

## What a hack campaign leaves out

A hack campaign is solo play with the hack's own look:

- **Online play, co-op and bots** are off. Both headsets would have to run the same hack, and the network does not check that yet.
- **GoldenEye X**, **XBLA / Bean** and the **HD texture packs** are off for that run. They are made for GoldenEye's own models and textures. Your settings for them are kept for the next GoldenEye run.
- **Progress** is saved separately (`eeprom-gf64.bin`, `eeprom-tnd64.bin`), so a hack's missions never unlock GoldenEye's or the other way round.

Guns use the hack's own models and stats.
