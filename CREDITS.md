# Credits

Thank-you sheet for work this port actually leaned on. Click a name for the
project; each line says **exactly what** we used it for.

The sheet itself started as GEVR's, the PC/VR project this port grew out of,
and has been rewritten for what is actually in this tree. **No GEVR code is
in this repository** - the relationship is where the project came from, not a
code dependency; see the table near the bottom. GEVR's own player-facing docs
live in that project: https://github.com/no6969el/GEVR.

Two directories are vendored upstream source rather than our own work, and
carry their notices beside them: `port/` (Perfect Dark PC port) and
`port/vr/` (Alex-LeTux's perfect_dark_VR). `src/` is the GoldenEye
decompilation.

Start here instead: [README](README.md) · [STATUS](STATUS.md) · [LICENCE](LICENSE).

We credit only real influence or reuse. Survey-only reads and projects we did **not** copy stay off this list (or are marked "not used"). Licence for this tree: [LICENSE](LICENSE).

---

## Not ours (please do not credit us for these)

| What | Whose |
|------|--------|
| **GoldenEye 007** | Nintendo / Rareware. The game, ROM, and assets are theirs. This port does **not** ship a ROM or game assets. You bring a USA `.z64` you own. |
| **This port's binary** | Our OpenGL ES / OpenXR delta on top of the decompilation. Separate from the game data and from the upstream licences below. |

---

## Adapted or built on (specific credit)

### GoldenEye PC port - **reference only**, no code vendored

- **Repo:** https://github.com/jkdansereau/goldeneye-pc-port
- **What it is:** the same GoldenEye decompilation taken to 64-bit desktop,
  with its port-specific changes fenced behind `#ifdef PORT` and a numbered
  findings ledger (`docs/dev/findings.md`, 202 findings) plus a defect-class
  document (`docs/porting-notes.md`).
- **What we use it for:** it has already met the defect class this port keeps
  hitting - N64 assumptions that are exact with 4-byte big-endian pointers and
  wrong on LP64. We read its findings and its `src/` fixes as a reference and
  port the reasoning; **no code from it is vendored here**, and its `port/`
  host layer is not used at all (ours is Perfect Dark's).
- **Worklist:** [docs/gepc-port-worklist.md](docs/gepc-port-worklist.md) is a
  generated index of all 429 of its `#ifdef PORT` sites.


### Perfect Dark PC port (MIT) - **vendored source**, our whole host layer

- **Repo:** https://github.com/fgsfdsfgs/perfect_dark (branch `port`)
- **What we took:** `port/` - everything that stands in for the N64 so the
  game code in `src/` can run on a headset. `port/src/` matches upstream
  file-for-file (`main.c`, `system.c`, `video.c`, `audio.c`, `fs.c`,
  `input.c`, `config.c`, `mixer.c`, `romdata.c`, `mod.c`, `pdmain.c`,
  `pdsched.c`, `libultra.c`, `crash.c`, `optionsmenu.c`), with GoldenEye
  files added alongside and Perfect Dark specifics excluded from the build
  rather than deleted.
- **Licence:** MIT, `Copyright (c) 2022 Ryan Dwyer` - the port is a fork of
  the decompilation and carries its notice. Travels with the code in
  [`port/LICENSE`](port/LICENSE); provenance in
  [`port/README.md`](port/README.md).
- **What is ours in there:** the `gevr_`-prefixed files (ROM binding, runtime
  file table, byte-order passes, model/stage conversion, audio pump,
  scheduler shim) and `menuimage.c`.

### Alex-LeTux / perfect_dark_VR (MIT) - **vendored source**, our whole VR layer

- **Repo:** https://github.com/Alex-LeTux/perfect_dark_VR
- **Taken from:** branch `port` @ `67ea20c86986c6bc85687f26a27418b266af309c`
- **What we took:** the entire `port/vr/` directory - the OpenXR session and
  frame loop (`vr_openxr.cpp`, ~2.7k lines), controller input and aim
  (`vr_input.cpp`), the hub, settings, logging and Android JNI glue, plus the
  `imgui/` and `miniz/` libraries they carry. It is adapted for GoldenEye, not
  rewritten: their structure is kept so our tree stays diffable against theirs,
  and Perfect Dark specifics (weapon tables, scoped-weapon list, menu and
  player state reads) are fenced off rather than deleted.
- **Licence:** MIT. Upstream is itself a fork of the Perfect Dark
  decompilation and carries that project's notice rather than its own, so the
  line reads `Copyright (c) 2022 Ryan Dwyer`. The notice travels with the code
  in [`port/vr/LICENSE`](port/vr/LICENSE); provenance and the full list of our
  changes are in [`port/vr/README.md`](port/vr/README.md).
- **What is ours in there:** `vr_screen.cpp` (the world-locked cinema screen,
  because GoldenEye's camera never builds its projection from `XrFov`) and
  `vr_settings_defaults.c` (upstream defines these in Perfect Dark's own game
  sources, which have no GoldenEye counterpart). We removed their in-app
  updater and HD-texture-pack downloader, which fetched Perfect Dark content.

> Earlier revisions of this file described this as "design map, not vendored
> code" and said their VR tree was not copied. That was wrong, and is
> corrected here: `port/vr/` is their source.

### Perfect Dark decompilation (MIT) - the notice our port layer carries

- **Repos:** https://github.com/n64decomp/perfect_dark
- **Why here:** both `port/` and `port/vr/` descend from it, so its MIT line
  (`Copyright (c) 2022 Ryan Dwyer`) is the notice those directories carry.
- **Also:** same Rare N64 FPS family, so it doubles as an architecture and feel reference (menus, aim/sway family, Fast3D ancestry).

### Emill / n64-fast3d-engine - renderer ancestry

- **Repo:** https://github.com/Emill/n64-fast3d-engine
- **What for:** Lineage of the Fast3D / `gfx_pc` path used by the native port (and related PD/GE ports). Credit for the engine ancestry, not for inventing a new RDP from scratch.

### n64decomp / 007 - GoldenEye decompilation, our `src/`

- **Repo:** https://github.com/n64decomp/007
- **What we took:** `src/` - the decompiled C and symbols that make a
  from-source port possible. This is the game itself, not a reference.
- **Licence:** the upstream repository ships **no licence file**, which is
  usual for a decompilation and is not the same as a grant. Nothing here
  claims ownership of the decompiled code or of retail assets, and a ROM you
  own is still required to run any of it.

### Khronos OpenXR

- **What for:** The VR API this port targets. Standalone on Horizon OS via the loader vendored in `OpenXR/`; the upstream code also supports PCVR runtimes.
- **Spec / org:** https://www.khronos.org/openxr/

### SDL2

- **What for:** Windowing / input host dependency used by the native port stack.
- **Licence:** zlib (see vendored SDL `LICENSE.txt` / `CREDITS.txt` in the product tree when binaries ship).

---

## Looked at, not adapted into this port

These showed up in prior-art surveys. They are **not** sources of code or
knobs in this tree unless a later note says otherwise.

| Project | Why listed | Used here? |
|---------|------------|------------|
| StarFox64-VR | Licence unclear; rule was do not read source | **No** |
| GoldenEye64Recomp / N64ModernRuntime | GPL host stack; kept external on purpose | **Not vendored** |
| Xbox 360 GoldenEye recomps | Different game build / assets | **No** |
| MGB64 (akratch) | Sibling native-port survey / control read | **Reference only** |
| [GEVR](https://github.com/no6969el/GEVR) (no6969el) | The PC/VR project this port grew out of, and where this credit sheet came from. Its VR work targets the PC host; this port is a separate standalone Quest build. | **Inspiration only** - no GEVR code in this tree |
| Evan King / GETV, `goldeneye-native` | The from-source PC host lineage behind GEVR. Listed because earlier revisions of this sheet credited it as our platform layer. | **Not used here** - our host layer is the Perfect Dark PC port |

---

## How we keep this honest

1. **Licence first** - unclear or proprietary prior art does not influence design.
2. **Name the borrow** - constants, transforms, and clamp splits get a recorded "what for," not a vague thank-you.
3. **Map vs vendor** - say which it is, plainly. `port/` and `port/vr/` are
   **vendored**: upstream source, adapted. Their MIT notices sit beside them
   in [`port/LICENSE`](port/LICENSE) and [`port/vr/LICENSE`](port/vr/LICENSE).
   Anything listed as influence only must stay that way or move up to a
   vendored entry with its notice, as those two did on 2026-09-21.
4. **Game data stays with the player** - ROM and assets are never in the download.

If you spot a missing credit for something we really used, open an Issue titled `Credits: ...` and point at the borrow. We will add a specific line, not a blanket shout-out. Do not upload ROM files.

---

## Quick links

- [README](README.md) - what this is, how to build and run it
- [STATUS.md](STATUS.md) - what works and what does not, right now
- [HANDOFF.md](HANDOFF.md) - current state, what works, what is broken
- [docs/RARE-LOGO-AUDIO-HANDOFF.md](docs/RARE-LOGO-AUDIO-HANDOFF.md) - worked
  example of an LP64 porting-defect class
- [LICENSE](LICENSE) - MIT, for this tree
- [tools/gevr_mixer_ab/](tools/gevr_mixer_ab/README.md) - soft-mixer A/B harness
- Upstream GEVR (PC/VR), including its `PRIOR-ART.md` and `LICENSE-MAP.md`:
  https://github.com/no6969el/GEVR
