# Boot menu texture preload — headset acceptance, 2026-10-02

The first legal/disclaimer screen, file-select folders and common menu art
previously appeared at native resolution while replacement PNGs decoded.
The file-select background also changed to HD after the menu appeared.

The existing single low-priority texture worker now warms a selected set of
menu art while the game boots. ROM binding queues the exact glyph checksums
for the two legal/menu fonts, regardless of their location in the pack.
It also reads the static gunbarrel background's RLE image and queues the
299 scanline checksums used by the folder-menu renderer. Those scanlines
match all 299 replacements in the local HD + AI pack and occupy about
2 MiB after the loader's existing resizing.

Visible texture requests take priority over speculative warming. Separate
preload allowances are 8 MiB for fonts, 8 MiB for menu art and 4 MiB for
background rows. Oversized or over-budget speculative images remain
available for normal on-demand loading. The 160 MiB decoded cache and
existing GPU upload limits are unchanged; no stage-wide preload or extra
decoding threads were added.

## Validation

- `python tools/gevr_texpack_preload_probe.py` passed. It exercises production
  checksums and worker logic against deterministic fixtures: font descriptors,
  all background-row checksums, malformed RLE, deduplication, visible-texture
  priority, in-flight requests, separate budgets, late background-only worker
  wake-up and on-demand fallback.
- Android debug and signed release builds passed. The final release build
  includes the background preload.
- The release APK signature verified and matches the existing release key.
  Application version remains 0.3.10, version code 50.
- The user tested the signed build on the headset and reported: "no more
  menu popin and background loaded instantly too".

Headset acceptance covers the observed menu appearance. Frame-time and
peak-memory measurements were not collected in this playtest.
