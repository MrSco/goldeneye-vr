# port/ — provenance

This is the host layer: everything that stands in for the N64 so the game code
in `src/` can run on a headset. It is **vendored from the Perfect Dark PC
port** ([fgsfdsfgs/perfect_dark](https://github.com/fgsfdsfgs/perfect_dark),
branch `port`) and adapted for GoldenEye. It is not original work of this
project.

`port/src/` matches upstream's file-for-file — `main.c`, `system.c`, `video.c`,
`audio.c`, `fs.c`, `input.c`, `config.c`, `mixer.c`, `romdata.c`, `mod.c`,
`pdmain.c`, `pdsched.c`, `libultra.c`, `crash.c`, `optionsmenu.c` — with
GoldenEye-specific files added alongside.

## Notices

| Path | Upstream | Notice |
|------|----------|--------|
| `src/`, `include/`, `preprocess/` | Perfect Dark PC port | [LICENSE](LICENSE) here — MIT |
| `vr/` | Alex-LeTux / perfect_dark_VR | [vr/LICENSE](vr/LICENSE) — MIT, same notice |
| `fast3d/` | Emill / MaikelChan, n64-fast3d-engine | [fast3d/LICENSE.txt](fast3d/LICENSE.txt) — MIT |
| `vr/imgui/` | Dear ImGui | [vr/imgui/LICENSE.txt](vr/imgui/LICENSE.txt) |
| `vr/miniz/` | miniz | header of `vr/miniz/miniz.c` |

The MIT line in [LICENSE](LICENSE) reads `Copyright (c) 2022 Ryan Dwyer`. The
Perfect Dark PC port is a fork of the Perfect Dark decompilation and carries
that project's notice rather than adding its own; `port/vr/` inherits the same
line through the same chain. The repository-root [LICENSE](../LICENSE) covers
this project's own contributions and does not override these.

## What is ours

Files prefixed `gevr_` are written for this port and have no upstream
counterpart — ROM binding and the runtime file table (`gevr_romload.c`,
`gevr_rom_manifest.c`), byte-order passes over cartridge data
(`gevr_romswap.c`), model and stage conversion, the audio frame pump, and the
scheduler shim. Also ours: `menuimage.c`, and in `vr/`, `vr_screen.cpp` and
`vr_settings_defaults.c` (see [vr/README.md](vr/README.md)).

## What upstream code is disabled

Some of Perfect Dark's host layer does not apply to GoldenEye and is excluded
from the build rather than deleted, so the tree stays diffable against
upstream. See the `list(FILTER ... EXCLUDE ...)` calls and their comments in
[CMakeLists.txt](../CMakeLists.txt): `romdata.c` and `mod.c` (Perfect Dark's
ROM ids, file table and rzip codec — `gevr_romload.c` stands in), `pdmain.c`,
`pdsched.c`, `optionsmenu.c`, and the `preprocess` variants.
