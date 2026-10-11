# Visual runtime repair — 2026-10-11

Reports `80d7c14b` (Bean) and `51adccf0` (Goldfinger), Quest 3,
v0.4.20 build `8245847`. Ramirez Cartel still lacked walls with GE-X weapons
off and on the virtual screen. This excludes a stereo-only failure.

Cartel's 128 rooms load through the native room loader, texture expansion and
4 MiB room heap in a local harness. All 349 compressed room streams also match
an independent inflater. The report has 7,361,744 stage-pool bytes free after
load; Bean Dam has about 8.3 MiB. These are solo observations, not measured
eight-player or GE-X stage headroom.

The shared renderer used combiner alpha whenever the blender selected memory
and inverse alpha. It ignored `ALPHA_CVG_SEL`, which selects coverage instead
of combiner alpha. Cartel's opaque room lists use mode `0x0c182078` and
combiner `fc26a004 1ffc93ff`, whose second-cycle alpha is zero. They therefore
became fully transparent on the host. Coverage-selected surfaces now ignore
combiner alpha unless `CVG_X_ALPHA` requires it for cutouts. Glass, cutouts,
fog and the custom depth-writing translucent mode have regression checks.

Bean changes:

- Attach rigid first-person weapon replacements to the normal gun matrix,
  excluding switch 1's muzzle-flash subtree. Previously an AK47 replacement
  appeared only when the flash switch was active and its normal lists were blank.
- Preserve the opaque base underneath two-picture terrain blends and decals,
  use the second UV set for bases and masks, and preserve blend identity for
  partial-range draws sharing the vertex shader and material. Material flags,
  rather than texture alpha alone, select level cutouts. Explicitly restore
  depth testing when emitting room geometry after sky lists.
- Reject absurd imported positions above magnitude 100,000,000.
- Replace linear model-node replacement searches with a bounded hash table;
  full-table, forget, rebind and storage-invalidation checks cover its lifecycle.
- Read `misc/alps3/default.dt` and `misc/doc0/default.dt` font atlases. Repaint
  the native ASCII glyph tiles with full-resolution Bean crops, preserving
  native layout, padding, clipping and wrapping. The launcher remains ImGui;
  this change addresses in-game text, not every menu background or graphic.
- Package and safely extract the two font files. Pack `2026.10.11.1` lacks
  them; local pack `2026.10.11.2` includes 499 mesh files, two fonts and 122 PNGs.

Comparison for material and font layouts: the MIT-licensed DAB source pinned
at `adef1dc2d4eca1a5c453a7f2bf03a3b6cdad99b8`, as recorded in `port/licenses`.

Validation: 57 port scripts passed with retail and Goldfinger ROMs supplied.
The Bean sweep decoded 497 meshes; two TV assets without GPU payloads retain
native geometry. Model registration checks cover 457 catalogue rows. Both real
font atlases pass all 94 printable non-space ASCII glyph checks. Android release
unit tests cover font extraction, allowlisting, malformed CAFF data and CRCs.

Headset acceptance remains pending. Check Cartel in stereo and virtual-screen
mode, Bean Dam terrain/guard guns/menu text, and compare frame timing. Also
check retail Dam's tunnel, Statue and Depot for room popping or sky holes;
eight-player multiplayer and GE-X need measured stage-pool headroom before
the earlier room-heap change is considered fully accepted. Asset decoding and
registration do not establish visual parity for all Bean meshes or materials.
No APK was installed or PR merged during this repair.
