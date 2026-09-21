# port/vr — provenance

This directory is **vendored from** [Alex-LeTux/perfect_dark_VR](https://github.com/Alex-LeTux/perfect_dark_VR)
(branch `port`), the OpenXR VR layer for the Perfect Dark PC port, and adapted
here for GoldenEye. It is not original work of this project.

Upstream is MIT licensed and its notice travels with these files: see
[LICENSE](LICENSE) in this directory. The copyright line there reads
`Copyright (c) 2022 Ryan Dwyer` — upstream is itself a fork of the Perfect Dark
decompilation and retains the decomp's notice rather than adding its own, so
that is the notice this code carries. The VR work on top of it is Alex-LeTux's,
contributed under the same licence.

The repository-root [LICENSE](../../LICENSE) covers this project's own
contributions; it does not override the notice above.

## What changed for GoldenEye

The files keep upstream's structure so changes stay diffable against it:

- Perfect Dark's weapon tables, scoped-weapon list and menu/player state reads
  are fenced off (`#if 0` blocks in `vr_input.cpp` and `vr_hub.cpp`) rather
  than deleted, each marked with what it did and what GoldenEye needs instead.
- `vr_settings_defaults.c` is **new**, not upstream. Upstream declares these
  settings in `vr_settings.h` but defines them in Perfect Dark's own game
  sources (`bondgun.c`); GoldenEye has no counterpart, so they are defined
  once here or the library does not link.
- `vr_screen.cpp` / `vr_screen.h` are **new**: GoldenEye's camera never builds
  its projection from `XrFov`/`XrAspect`, so the frame is rendered off-screen
  at the game's aspect and submitted as a world-locked quad. See HANDOFF.md.
- Android logging goes to logcat under the `GoldenEye-VR` tag; upstream wrote
  to `vr_debug.txt`, which fails on Android.
- The in-app updater and HD-texture-pack downloader (`vr_update_dl.cpp`,
  `vr_textures_pack_dl.cpp`) were **removed** — they fetched Perfect Dark
  releases and Perfect Dark textures, which is wrong for this port.

`imgui/` and `miniz/` are further third-party libraries carried along by
upstream; each keeps its own licence (`imgui/LICENSE.txt`, and the notice at
the top of `miniz/miniz.c`).
