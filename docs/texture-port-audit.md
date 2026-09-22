# Texture reference audit — 2026-09-22

Compared local `../gepc-ref/port/fast3d/gfx_pc.cpp`, `src/game/image.c`,
`docs/dev/findings.md`, and `docs/dev/TEXTURE-GLITCH-ANALYSIS.md` with our
Perfect Dark-derived renderer. The generated worklist indexes PORT guards;
it does not include every host-renderer fix. Check the renderer directly too.

| Reference | Quest disposition |
|---|---|
| D217 arena alignment | Already applied in image.c; user saw no improvement. |
| D217 palette-content cache key | Added. Existing palette addresses did not cover reuse at the same address. Hash refreshed after TLUT copies. |
| D228 IA16 channels | Applied previously; exhaustive decoder tests pass, but user reports textures remain bad. |
| D74 preserve valid loads under LOD | Applied: only missing sources use the fallback. |
| RC2 base image versus appended mip chain | Adapted through one dimension helper, shared by all nine native importers, row unpacking, cache dimensions, and UV normalization. LOD block heights exclude appended mips; sub-tile windows retain the loaded image. |
| D183 source pitch | Existing unpacker handles it; tested with synthetic strided rows. |
| D159 disable engine row swizzle | Do not copy: our renderer reverses the swizzle and compiled assets also require it. Tested that it happens exactly once. |
| D161 CI without TLUT | Applied: CI4/CI8 with G_TT_NONE use intensity decoders. |
| D229 source-format reinterpretation | Not copied: reference describes an unresolved/stale-source hazard, not a generally safe fix. |
| RC3 wrap mask approximation | Not copied: reference leaves it disabled by default and visual benefit unresolved. |

Additional local defects addressed:

- Native importers used tile width/height while UV normalization used block
  bytes/stride. A 1x1 tile over a 1024-byte, 16-byte-row CI8 load uploaded one
  texel while the draw normalized against 16x64. The shared dimension helper
  restores the full loaded image for this non-LOD sub-tile case.
- CI4 rectangle import recomputed dimensions after native row unpacking.
  It could read beyond the packed base image into stale scratch bytes/mips.
  It now follows the same dimensions as triangle imports.
- Cache equality now includes upload dimensions, source pitch/swizzle,
  palette format and content hash. TLUT mode changes mark texture bindings
  dirty even when no new palette or image load occurs.

Validation: `tools/gevr_texture_probe.py` compiles production import code
against a capture-only renderer and exercises CI4 rectangles/mips, sub-tile
images, pitched rows, swizzle, disabled TLUT and cache-key variants.
`tools/gevr_palette_probe.py` checks all IA16 and RGBA16 entries. Both pass.
Android assembleDebug passes. Hardware visual acceptance remains pending;
these tests do not prove all PP7/HUD/effect corruption is resolved.
