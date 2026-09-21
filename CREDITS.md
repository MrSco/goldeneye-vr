# Credits

Thank-you sheet for work this port actually leaned on. Click a name for the
project; each line says **exactly what** we used it for.

This sheet is inherited from GEVR, the PC/VR project this port grew out of, and
trimmed to what applies here. GEVR's own player-facing docs (release zips,
controls, roadmap, `PRIOR-ART.md`, `LICENSE-MAP.md`) live in that project:
https://github.com/no6969el/GEVR - they are not duplicated in this repository.

Start here instead: [README](README.md) · [HANDOFF](HANDOFF.md) · [LICENCE](LICENSE).

We credit only real influence or reuse. Survey-only reads and projects we did **not** copy stay off this list (or are marked "not used"). Licence for this tree: [LICENSE](LICENSE).

---

## Not ours (please do not credit us for these)

| What | Whose |
|------|--------|
| **GoldenEye 007** | Nintendo / Rareware. The game, ROM, and assets are theirs. This port does **not** ship a ROM or game assets. You bring a USA `.z64` you own. |
| **This port's binary** | Our OpenGL ES / OpenXR delta on top of the decompilation. Separate from the game data and from the upstream licences below. |

---

## Adapted or built on (specific credit)

### Evan King / GETV platform layer (MIT)

- **What:** The from-source PC host / platform layer GEVR's VR work sits on (`goldeneye-native` lineage).
- **Credit for:** Host, port scaffolding, Fast3D wiring, and the native bring-up path we extend for OpenXR.
- **Not claiming:** Their MIT work as closed or as "GEVR-only." Their notice travels with derived portions.
- **Licence in tree:** see the GETV / `goldeneye-native` `LICENSE` (Copyright (c) 2026 Evan King).

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

### Perfect Dark PC port / decomp (MIT) - sibling engine reference

- **Repos:** https://github.com/perfect-dark-pc-port/perfect_dark (and `n64decomp/perfect_dark`)
- **What for:** Same Rare N64 FPS family. Used as architecture and feel reference (menus, aim/sway family, Fast3D ancestry notes). Not a wholesale copy into GEVR.

### Emill / n64-fast3d-engine - renderer ancestry

- **Repo:** https://github.com/Emill/n64-fast3d-engine
- **What for:** Lineage of the Fast3D / `gfx_pc` path used by the native port (and related PD/GE ports). Credit for the engine ancestry, not for inventing a new RDP from scratch.

### n64decomp / 007 - GoldenEye decompilation upstream

- **Repo:** https://github.com/n64decomp/007
- **What for:** Decompiled C and symbols that make a from-source port possible. Upstream posture is **not** a blanket open licence for the game; GEVR does not claim ownership of decomp or retail assets. ROM still required.

### Khronos OpenXR

- **What for:** The VR API GEVR targets (PCVR runtimes: SteamVR-class, Pimax, Quest via PC link, etc.).
- **Spec / org:** https://www.khronos.org/openxr/

### SDL2

- **What for:** Windowing / input host dependency used by the native port stack.
- **Licence:** zlib (see vendored SDL `LICENSE.txt` / `CREDITS.txt` in the product tree when binaries ship).

---

## Looked at, not adapted into this port

These showed up in prior-art surveys. They are **not** credited as sources of GEVR code or knobs unless a later note says otherwise.

| Project | Why listed | Used in GEVR? |
|---------|------------|---------------|
| StarFox64-VR | Licence unclear; rule was do not read source | **No** |
| GoldenEye64Recomp / N64ModernRuntime | GPL host stack; kept external on purpose | **Not vendored** |
| Xbox 360 GoldenEye recomps | Different game build / assets | **No** |
| MGB64 (akratch) | Sibling native-port survey / control read | **Reference only** |

---

## How we keep this honest

1. **Licence first** - unclear or proprietary prior art does not influence design (see `docs/55-prior-art-licence-check.md`).
2. **Name the borrow** - constants, transforms, and clamp splits get a recorded "what for," not a vague thank-you.
3. **Map vs vendor** - Perfect Dark VR is **prior-art map** unless a future commit says code was brought in (then MIT notice + this sheet update).
4. **Game data stays with the player** - ROM and assets are never in the download.

If you spot a missing credit for something we really used, open an Issue titled `Credits: ...` and point at the borrow. We will add a specific line, not a blanket shout-out. Do not upload ROM files.

---

## Quick links

- [README](README.md) - what this is, how to build and run it
- [HANDOFF.md](HANDOFF.md) - current state, what works, what is broken
- [docs/RARE-LOGO-AUDIO-HANDOFF.md](docs/RARE-LOGO-AUDIO-HANDOFF.md) - worked
  example of an LP64 porting-defect class
- [LICENSE](LICENSE) - MIT, for this tree
- [tools/gevr_mixer_ab/](tools/gevr_mixer_ab/README.md) - soft-mixer A/B harness
- Upstream GEVR (PC/VR), including its `PRIOR-ART.md` and `LICENSE-MAP.md`:
  https://github.com/no6969el/GEVR
