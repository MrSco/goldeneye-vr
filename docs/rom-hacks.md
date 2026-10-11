# Goldfinger 64 and Tomorrow Never Dies

GoldenEye VR can start **GoldenEye 007 (USA)**, **Goldfinger 64 1.0**, or **Tomorrow Never Dies 64 Expanded 06-22** from the launcher. The three cartridges stay installed together. The campaign radio chooses which one Start loads, and that choice is remembered. You do not replace `ge.z64` to switch.

No hack ROM, patch, or converted asset is in the APK or the repository. Supply dumps of games you own. Do not upload a ROM to GitHub or a bug report.

## Which release

The loader accepts two hack headers, matched on size, title, game code, and the eight CRC bytes:

| Campaign | File | Size | Title | Code |
| --- | --- | --- | --- | --- |
| GoldenEye | `data/ge.z64` | 12 MB | `GOLDENEYE` | `NGEE` |
| Goldfinger 64 | `data/goldfinger64.z64` | 24 MB | `GOLDFINGER` | `NGFE` |
| Tomorrow Never Dies | `data/tnd64.z64` | 16 MB | `GOLDENEYE` | `NGEE` |

Goldfinger 64 is version **1.0** (2017). The patch that produces it is distributed as `goldfinger64.zip`, containing `Goldfinger64_1_0.xdelta`, applied to a GoldenEye 007 (USA) ROM in big-endian `.z64` order. A file with the right size and the wrong CRC is rejected.

Tomorrow Never Dies is the **Expanded 06-22** release (`TND64_Expanded_06-22.z64`). It still says `GOLDENEYE` / `NGEE` in the header; the size and CRC are what identify it. **Original V4 is not this layout** and will not be offered as a campaign.

Retail checks are unchanged. A hack cannot be adopted into the `ge.z64` slot.

## On the launcher

1. Install your GoldenEye 007 (USA) ROM the usual way, so Start can fall back to it.
2. Under **CAMPAIGN**, use **Choose ROM** on Goldfinger 64, Tomorrow Never Dies, or both. Each file is checked and stored under its own name. The retail ROM is left where it is.
3. Select the campaign. Start loads that cartridge. The next launch keeps the same selection.
4. If the selected hack is missing, the radio returns to GoldenEye and says so.

Switching campaigns is the radio plus Start. Leaving a mission already returns to the launcher, so the other campaign is one choice away.

Guns in a hack use the GoldenEye weapon whose gun file they took, so the hack's mesh shows and that weapon's stats apply. Distinct stats for a hack's extra pistols are not in this version. Tomorrow Never Dies Girl Power script hooks are dropped on load rather than run.
