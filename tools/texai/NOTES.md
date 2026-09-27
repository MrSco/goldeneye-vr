# AI-generated HD textures: research and pilot (2026-09-26)

Branch `claude/ai-generated-hd-textures-477235`. Question: can image AI fill
the gaps in the texture pack we offer (evilgames.eu "GoldenEye 007 HD",
intermissionfb and GhostlyDark)?

## The gap

- The installed pack (headset, `files/texture-packs/ge007-hd`) has 1763 PNGs:
  508 in-game textures, 651 explosion/smoke/death frames, 299 gun barrel frames,
  about 200 font glyphs, and a few ammo, crosshair and watch textures.
  On the Dam, 502 of 665 distinct textures matched (HANDOFF 101).
- It has no guards or characters and covers only 7 weapons. Its level
  textures cover 15 missions, each only partly: Dam 106, Facility 86,
  Runway 63, Surface 45, Archives 40, Streets 30, Aztec 27, Silo 25,
  Bunker 20, Jungle 15, Statue 12, Frigate 7, Train 2, Caverns 2, Depot 1.
  These counts are folder sizes in the authors' repo, which restructured the
  same textures after the release.
- The authors' repo (github.com/GhostlyDark/GoldenEye-007-HD, forked to
  MrSco/GoldenEye-007-HD on 2026-09-26) has `GOLDENEYE/ge007.tdb`, a
  database of every texture the game draws: 35,517 GLideN64 names with their
  native sizes.
  - That is 4,801 distinct textures (checksum, format, size); the pack
    covers 1,780.
  - The gap is 3,021 textures. 511 of them are 8 texels or less on a side
    (flat colours, ramps), so about 2,500 are worth generating.
  - The database has names only, no texels; the texels come from the dump.
- The ROM's texture table holds 2698 textures (`assets/images.def`).
  - The pilot decoder reads 1744 of them: the zlib ones, via
    `tools/gevr_tex_decode.py` on `feature/9-hand-shells`.
  - The other 954 use Rare's own compression.
- The pack keys each image by GLideN64's Rice checksum of the texture *as
  drawn*. A texture number from the ROM can't be matched to a pack name, so
  the texels come from the running game (the dump below).

## Dump and batch (for the fork)

- **Build.** Build `63e1628` onward writes a dump while `files/gevr_packdump`
  exists.
  - It writes every texture the active pack lacks to `files/texture-dump/`.
  - Each file is named as GLideN64 dumps it (the name the pack loader looks
    up), holding the RGBA texels the checksum covers.
  - `index.tsv` gets a line per texture: size, format, cms/cmt, mask bits,
    and the level (`g_StageNum`).
- **Collect.** Play each level, then
  `adb pull /sdcard/Android/data/com.gevr.port/files/texture-dump`.
- **Generate.** `tools/texai/batch.py <dump> <fork> [--tool seedvr2]`:
  - It skips names the fork already has, and textures under 16 texels.
  - Each texture is upscaled through ComfyUI, padded by its own wrap flags.
  - The results are colour-locked and written to
    `<fork>/GOLDENEYE/AI/<Mission NN - Name>/<name>`.
  - It resumes if stopped, and logs drift scores to `build/texai-batch/log.tsv`.
- **Speed.** About 12 s a texture with SeedVR2 at 16x (256-1024 px), so about
  8 hours for 2,500.
- **No headset needed for most textures (`romkeys.py`).**
  - It names ROM textures offline the way the pack does. It rebuilds each
    zlib texture's pool bytes (texInflateZlib: rows padded to 8 bytes, odd
    rows word-swapped per the header's LOD bits, the palette hashed from the
    image's own first bytes), then takes GLideN64's checksum.
  - Every name is kept only if the authors' `ge007.tdb` lists it.
  - 2026-09-26: 1,744 decoded, 1,225 exact matches, 21 on the texture
    checksum only, 498 unmatched (drawn from a sub-tile, or never drawn).
    258 of the matches are already in the pack, so 967 go to batch.py
    (`build/texai-rom`, level column -1, written to `GOLDENEYE/AI/From ROM/`).
  - Wrap flags are guessed from the size: power-of-two sides repeat.
  - 2026-09-27: `rareimg.py` ports texInflateNonZlib (huffman, RLE, lookup,
    blur; RGBA/IA/I formats). All 2,698 table textures now decode.
    - 2,007 are names the tdb lists; 782 of those are from the 954 non-zlib.
    - The 54 non-zlib textures that are also in the headset dump decode to
      identical texels.
    - Where the port and the N64 differ (a u16 lookup read by byte), the
      N64's reading is used.
    - 518 new gap names, 469 of them 16 texels or more (`build/texai-rom2`).
  - Still only from a dump: textures outside the texture table (fonts,
    effects, model-embedded art such as the Rare logo).
- **What the ROM can't give (checked 2026-09-27).**
  - After ai-2026.09.27, 1,070 real textures are still missing (1,580
    counting 510 flat ones of 8 texels or less).
  - Offline variants were tried against every table texture: swap on/off,
    sub-tiles, stride. They recover none of them.
    - 33x33 CI: 630 tdb entries; only 8 of the ROM's 101 match in any
      variant.
    - Most of the rest are 95x32 IA, 32x32 CI and 16x16 RGBA.
  - These are built or changed at run time, or read past the loaded texture
    into whatever follows it in memory, as GoldenEye's global explosion and
    smoke DLs do (56x56 IA8 with 64 masks). The port's lookup skips those
    reads (s_tpSkipSize).
  - Palette-only mismatches (21 textures) are odd colour counts: GLideN64's
    palette checksum reads 2 bytes past into memory that varies (one 16x1
    ramp has 47 tdb names). All are tiny ramps and flat colours.
  - 31 small textures (128x9 strips, 14x14 fire) that `--min 16` skipped were
    added with `--min 9`.
  - The rest needs the in-game dump.
- **Pack scale.** The authors' 4K sources are 16-64x native. Their HD
  release is 25-50% of that, so 4-32x (median 8-16x). Ours is 8x (256 px
  for 32 px), to keep the download small; raise `texai.SCALE` if it looks
  soft next to theirs.
- **Release (user decision 2026-09-26: no permission ask).** The public repo
  has a fork button and they're credited: `CREDITS.md`, the fork README, the
  zip's readme.
  1. Package:
     `python tools/texai/package.py <fork> build/pack/ge007-hd-v2025.12.30-gliden64-png-hd.zip <out.zip>`.
     The zip holds the authors' latest master, resized to their HD release's
     per-texture sizes (Hacks/ left out), plus `GOLDENEYE/AI/`.
  2. Commit `GOLDENEYE/AI` and the README in the fork, push, and attach the
     zip to a release on MrSco/GoldenEye-007-HD.
  3. Add a `ModManager.PACKS` entry with the asset URL and its exact byte
     size (the download is checked against it).
- **Review.** `python tools/texai/review.py build/texai-rom <fork>` writes
  pages to `build/texai-batch/review_NN.png`, worst drift first.
  - SeedVR2 does well on materials, props, signs and decals.
  - It reinvents ornate pixel art (e.g. 7A7EF21F). Those are candidates for
    ChatGPT, or for leaving out.

## Headset tour (2026-09-27, release ai-2026.09.27.2)

The ROM pass can't reach textures the game builds or reads at run time, so
`tools/texai/tour.py` drives the headset through all 20 missions with the
dump on. It warps to every pad and spins where something new shows up.
Running unattended, it took about 3.5 hours and found ~380 real gaps. The
script's docstring lists the traps: exit-zone pads, intro timing, sleep.

What the tour taught:
- **Measure gaps with the dump, not the tdb.** Most tdb names the pack lacks
  are never looked up by our port. The dump shows exactly what the
  installed pack misses.
- **Palette checksums vary for one image.** The checksum takes in bytes the
  texture doesn't use, so a Silo console dumps under 5-8 names with
  identical pixels.
  - batch.py makes one `#$` (any-palette) file for these. The port looks it
    up after the exact name misses, and it also covers variants nobody has
    dumped yet.
- **SeedVR2 invents structure on grainy art.** On stone, bark and the Statue
  Park statue's 33x33 tiles it drew chrome ornaments, a different one on
  each tile.
  - Grainy textures (flat share < 0.15) get both upscalers. Real-ESRGAN wins
    only if it is 1.5 dB more faithful at native resolution.
  - regrain.py applies the same test to textures already in the fork (129
    swapped).
- **ComfyUI's VRAM creeps up over a run** until jobs spill and crawl.
  comfy.py frees memory every 15 jobs and interrupts any job past 240 s.
  One hung SeedVR2 job ignored the interrupt and needed a ComfyUI restart.
- **package.py sizes AI textures to the authors' scale** beside them: about
  256 px on the long side, 4x for a 64-px wall. The fork keeps the 8x
  masters. The zip went from 334 MB to 262 MB.
- **Review failures** to watch for: invented text, "eyes" on small dark
  screens, contour swirls on flat grey dials, stringy coastlines on map
  tiles. They are all on rejected.txt.

## Pilot

Six ROM textures went through ChatGPT (its image model, Plus) and Gemini
(3.1 Pro = Nano Banana Pro), both driven in the user's Chrome. The prompts
are the templates in `texai.py`.

`drift4` is the PSNR of the answer averaged over 4x4 texels, against the
original. Higher means the layout is kept.

| texture | ChatGPT | Gemini |
|---|---|---|
| 00c3 brick wall | 24.7: bricks half size (pattern redrawn) | 24.5: bricks slightly larger, close |
| 0160 Facility panel | 20.5: excellent | 22.3: excellent |
| 0955 "58" sign | 14.6: Rare's digits replaced by a stencil font | 12.5: same |
| 081d face | 26.9: same person, excellent | 16.7: a different person |
| 0914 frond (cut-out) | 15.0: new leaf shape, fine inside the original's silhouette | 12.9: a different plant |
| 094e jungle foliage | 24.5: realistic, pattern loose | 31.5: follows the original's pattern |

Findings:

- **Don't describe identity.** The first face prompt said "a man". ChatGPT
  followed the words over the picture and drew a man on a woman's texture.
  With "the same person" it kept her.
- **Never ship raw answers.** Colour and brightness drift. Checked on the
  sheet, not measured:
  - Locking the result back to the original texel by texel (`strict`)
    ghosts wherever the model moved something.
  - A 4x4-texel colour lock (`soft`) keeps the model's detail and the
    original's colours. Use `soft`.
- **drift4 separates good answers from wrong ones:**
  - At 20 or above, the answer is usable.
  - At 17 or below, it is always visibly wrong.
  - It misses a redrawn pattern at the same colours (ChatGPT's bricks), so
    tiling materials need an eyeball check, or a pattern-scale check to be
    written.
- **Wrap flags matter.** 094e only repeats horizontally: seam-fixing it
  vertically smeared its dark top band. A dump has to record the tile's
  cms/cmt.
- **Lettering is out.** Both models replace Rare's glyphs. Text, fonts and
  HUD stay with the pack or hand work.
- **Output sizes and watermarks:**
  - ChatGPT answers at 1254x1254; Gemini's page shows 1024x1024.
  - No visible watermark was found in these Gemini downloads. They carry
    SynthID, which is invisible.
- **Throughput by hand is about 1 minute per image per service.** The apps
  have per-hour limits. Fine for a few dozen showcase textures, not
  for 1,500+.

## Local pass (ComfyUI, same six textures)

The user's ComfyUI Desktop (0.37.4) is at `E:\AI\ComfyUI-Installs\ComfyUI`, with
its API on :8188. It had no image models. Added from the Comfy-Org Hugging
Face repos:
- `RealESRGAN_x4plus` (67 MB, BSD-3)
- SeedVR2 7B int8 (8.3 GB) plus its VAE (0.5 GB), Apache-2.0

`comfy.py` sends each texture at its own size, padded by 8 texels: wrapped on
axes that repeat, edge-repeated on clamped ones. It then frames the answer
for `texai.py post`.

Run times on the RTX 3080 Ti:

| method | time per texture |
|---|---|
| Real-ESRGAN | about 0.5 s |
| SeedVR2 (two 4x passes) | 8-20 s |
| Real-ESRGAN, then SeedVR2 | about 35 s |

A single 32x SeedVR2 pass does nothing useful: it restores roughly 4x at a
time and keeps a 32x lanczos blur as content.

drift4 (dB) for the final soft textures, next to the chat models:

| texture | ESRGAN | SeedVR2 | ESRGAN+SeedVR2 | ChatGPT | Gemini |
|---|---|---|---|---|---|
| brick | 37.3 | 33.5 | 38.4 | 24.7 | 24.5 |
| panel | 38.8 | 38.8 | 38.7 | 20.5 | 22.3 |
| "58" sign | 34.9 | 30.7 | 34.4 | 14.6 | 12.5 |
| face | 33.3 | 38.6 | 32.8 | 26.9 | 16.7 |
| frond | 17.7 | 17.6 | 17.7 | 15.0 | 12.9 |
| foliage | 26.1 | 39.5 | 25.8 | 24.5 | 31.5 |

What the pilot shows, in `compare_final.png`:
- Local upscalers keep the texture. The chat models remaster it. Neither is
  better everywhere.
- Best per kind of texture:
  - **Tiling materials (brick, rock, foliage):** SeedVR2. Faithful layout
    with real material grain. Gemini is also good on foliage.
  - **Lettering and signs:** Real-ESRGAN, or ESRGAN then SeedVR2. The only
    methods that keep Rare's glyphs.
  - **Faces:** ChatGPT, with the neutral prompt. SeedVR2 is the best local
    one. Real-ESRGAN looks plastic.
  - **Machinery, panels, posters:** ChatGPT or Gemini. The local
    upscalers keep them flat and painterly, and SeedVR2 turns pixel noise
    into dots and blocks.
  - **Cut-out foliage:** ChatGPT or Gemini. Locally the leaf goes to smooth
    blobs.
- Key-colour spill on cut-out edges is cleaned in `post` (purple pixels
  within 6 px of the edge are refilled). Dark purple traces remain on the
  local frond results.

A workable split for the real gap fill:
- Everything goes through SeedVR2 locally, overnight: about 5-8 hours for
  1,500-2,000 textures, and free.
- Text and signs use Real-ESRGAN.
- A reviewed list of showcase textures (faces, panels, posters, weapons)
  goes to ChatGPT.
- The drift score plus the sheet decide which result each texture keeps.

## Next steps (not started)

1. **Port: dump misses.** Behind a marker file, `gevr_texpack_lookup` writes
   each texture the pack doesn't match as `GOLDENEYE#CRC#F#S[#PAL]_all.png` /
   `_ciByRGBA.png`. It already computes the key. Add a sidecar line per
   texture with fmt/siz/size, cms/cmt and mask, and the level. The user plays
   each level once, with unlock all and the level select, then `adb pull`.
2. **Port: a fill pack.** A second pack scanned after the main one (first
   match wins), so AI textures fill gaps and never replace the authors' work.
3. **Batch through the APIs, not the apps.**
   - Needs an OpenAI and/or Gemini API key.
   - Research quotes (not checked here) put 2000 images at about $40 (Gemini
     Flash image, batch) to $270 (Nano Banana Pro). gpt-image-2 medium is
     about $0.05 each, half that in batch.
   - Run both models and keep the better score, then have a person review
     the sheet.
4. **Local option.** The RTX 3080 Ti runs GAN upscalers in milliseconds per
   texture. Candidates:
   - 4xTextureDAT2_otf (CC-BY-4.0)
   - 4x-GameAI 2.0 (WTFPL)
   - Real-ESRGAN (BSD-3)
   They keep the layout exactly, which suits tiling materials. Not tried:
   nothing was downloaded.

## Legal line

- The originals, the answers and the finished PNGs are all ROM-derived. Keep
  them under `build/`.
- An AI pack is a separate download, like the evilgames one.
- The evilgames repo has no license, so ask its authors before merging into
  or redistributing their pack.
- The guard faces are photos of real people (Rare staff), a likeness question
  on top of copyright.

## Running the pilot again

```
python tools/texai/texai.py prep  <orig_dir> build/texai-pilot    # in/<tex>.png + in/<tex>.txt
#   send each in/<tex>.png with its prompt; save answers to build/texai-pilot/out/<tool>/<tex>.png
python tools/texai/texai.py post  <orig_dir> build/texai-pilot
python tools/texai/texai.py sheet <orig_dir> build/texai-pilot
```

`<orig_dir>` holds the originals as `<tex>.png`, e.g. decoded with
`python tools/gevr_tex_decode.py "<rom>" <orig_dir> 0x0c3 0x160 ...` on the #9
branch (it writes `0x0c3.png`; the samples file names them `00c3`).
