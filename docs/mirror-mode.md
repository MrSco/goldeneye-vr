# Wreck's Mirror Mode

Issue: [#200](https://github.com/MrSco/goldeneye-vr/issues/200).
Patch and author: [Wreck, Mirror Mode](https://n64vault.com/ge-levels:mirror-mode),
August 12, 2017 (the full solo/multiplayer patch, not the older Mirror Multi).

Apply `GE_MirrorMode_patch.xdelta` from `mirrormode[wreck].zip` to a clean,
big-endian GoldenEye 007 (USA) dump that you own. For example:

```sh
python tools/gex/vcdiff_apply.py ge.z64 GE_MirrorMode_patch.xdelta mirror.z64
```

Copy the result into the headset's Download folder. In the launcher, open the
campaign dropdown beside **START**, choose **Add Mirror Mode ROM...**, and pick
the patched ROM. It is stored as `data/mirror.z64`; the original `data/ge.z64`
stays installed. Select **Mirror Mode (Wreck)** in the same dropdown to play.
The selection is remembered (`Campaign=3`), with progress in `eeprom-mirror.bin`.
Missing or rejected campaign data falls back to GoldenEye.

This integration uses the patch's own mirrored backgrounds, collision, setups,
object models, textures and animations. It also reproduces its backwards watch
clock. Like the other hack campaigns, this runs solo with online/co-op/bots,
GE-X and the launcher's cheat list disabled. Existing preferences are retained.

Bean (XBLA) and HD packs stay available, because Mirror Mode keeps GoldenEye's
own models and pictures under their names. Wreck flipped each level about
x = 0 and 140 object models in their own x; characters, hats and guns are
left alone, and doors mirror at run time (`DOORFLAG_FLIP`). Bean follows him
(`port/src/gevr_mirror.c`): its rooms and sky dome are mirrored before they are
matched to the cartridge's room triangles, a flipped model's fit takes its x
negated (and its triangles wound the other way), its Rare logo is flipped, and
the pictures he turned over (the stage pictures) are turned over too. A flipped
model with no fit keeps Wreck's own. HD packs match pictures by content, so the
ones he changed keep his. `tools/gevr_mirror_lists.py` lists what he changed
(`port/src/gevr_mirror_lists.inc.h`, names and numbers only) from the retail and
Mirror Mode cartridges. Wreck's N64 multiplayer changes are not part of the VR network
protocol; the game's own cheat menu remains available.

## Layout measurements

The uploaded patch was inspected without a base ROM. VCDIFF COPY instructions
were traced to their original USA offsets, and ADD/RUN bytes were treated as
known new bytes. Unknown source bytes were never treated as recovered ROM data.
No ROM, patch, model, texture or animation is committed or included in the APK.

The xdelta patch is 3,825,115 bytes, SHA-256
`08e8c42d43474487041b56afa1279d35eb9957579cb3ffb0af8ea8c801744d63`.
Its two target windows produce 12,582,912 bytes. The title (`GOLDENEYE`), game
code (`NGEE`) and magic are copied unchanged from the USA source; the eight new
header CRC bytes are `94 BD 9A 93 4F 82 1A C8`. All four header properties must
match before the campaign can bind. Byte-swapped `.v64`/`.n64` imports are
normalised by the existing decoder.

| Bank | Mirror ROM offset | Evidence from the patch |
| --- | --- | --- |
| Animation records | `0x28E980` | Original segment start; edited records occupy 16 fewer bytes |
| Global image table | `0x29D150` | Original `0x29D160` copied 16 bytes earlier; 2,760 bytes |
| Rare logo | `0x29E550` | Original `0x29E560` start copied 16 bytes earlier; edited model data |
| Gunbarrel | `0x2A4D40` | Original `0x2A4D50` copied 16 bytes earlier |
| Gothic font | `0x2E63E0` | Original `0x2E63F0` copied 16 bytes earlier |
| Zurich font | `0x2E8890` | Original `0x2E88A0` copied 16 bytes earlier |
| SFX control / samples | `0x2EBDD0` / `0x2F1990` | Original audio banks copied 16 bytes earlier |
| Instrument control / samples | `0x3B4440` / `0x3B87E0` | Original audio banks copied 16 bytes earlier |
| Music | `0x419780` | Original `0x419790` copied 16 bytes earlier |
| Images | `0x8EDA20` | Image-loader address instructions at ROM `0x1007EC`; COPY of original bank start |

The patch changes `image_entries_load` and `check_if_imageID_is_light` to four
bytes per image row with bit 23 marking lamps (code at ROM `0x35708` and
`0xF0160`). The existing variant image decoder already handles that format. The
remaining engine tables use the USA layout: 80 character rows, 340 props and
2,698 images. The campaign binder reads its resource table, sizes, model
headers, level records and animation offsets from the player's patched ROM;
retail file offsets and animation offsets are not reused for these assets.
The mirrored Rare logo uses its relocated segment, including on retained draws.

The watch change at ROM `0xBBF14` changes addition to subtraction. The native
port subtracts elapsed game ticks from the watch clock for this campaign; both
the screen watch and controller watch use that clock. Mission timing is separate.
Animation pointer relocations are handled by the existing variant rebuild path.

## Validation

Automated checks cover strict header matching, all three byte orders, rejecting
a retail ROM without replacing an installed Mirror ROM, selecting only the
Mirror filename, missing/failed/wrong-ROM fallback, GE-X preference preservation,
segment locations, save isolation, campaign persistence, mission progression and
watch-hand angles with either handedness. The campaign selector is rendered at
the Quest launcher size and font scale. The Android debug APK and JVM tests are
built through the project's installed Android toolchain.

```sh
python3 port/tests/test_mirror_mode.py
python3 port/tests/test_romlayout.py
python3 port/tests/test_launcher_start_row.py
python3 port/tests/test_campaign_progression.py
python3 port/tests/test_room_heap.py
python3 port/tests/test_vr_display.py
GEVR_MIRROR_ROM=/path/to/mirror.z64 python3 port/tests/test_romcampaign.py
```

The last check requires the player's complete big-endian patched ROM. It runs
the production binder and independently checks resource offsets/sizes, model
headers, level records and relocated animation records. It is skipped here:
only the patch was supplied. Quest gameplay and a complete mission have not
been tested. Before release, verify startup logos, Dam and Facility collision,
doors, monitor images, watch direction, intro/outro animations, mission completion,
save isolation, and returning to the normal GoldenEye campaign.

On Linux/glibc hosts the existing Windows random stub in `test_vr_display.py`
needs a compiler wrapper that includes `<cstdlib>` before mapping only that
fixture's `arc4random_uniform` to a private test symbol. This is a host test
compatibility adjustment; production settings and test assertions are unchanged.

Credits from Wreck's release: SubDrag (tools, Bunker eye, logo work), Pavarini
(plane/chopper and Cradle dive animations), and Zoinkity (backwards watch clock).
