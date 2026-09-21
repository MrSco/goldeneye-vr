# Credits

Thank-you sheet for work GEVR actually leaned on. Click a name for the project; each line says **exactly what** we used it for.

Current play zip is **vr440**. [README Play](README.md#play-vr440) - [vr440 release](https://github.com/no6969el/GEVR/releases/tag/vr440). How to file bugs: [CONTRIBUTING.md](CONTRIBUTING.md). Whose license is whose: [LICENSE-MAP.md](LICENSE-MAP.md).

Player door: [README](README.md) · [Beta notes](docs/BETA.md) · [Features](FEATURES.md) · [Roadmap](docs/ROADMAP.md).

We credit only real influence or reuse. Survey-only reads and projects we did **not** copy stay off this list (or are marked "not used"). Details and licence notes: [PRIOR-ART.md](PRIOR-ART.md). Licence for this public tree: [LICENSE](LICENSE).

---

## Not ours (please do not credit us for these)

| What | Whose |
|------|--------|
| **GoldenEye 007** | Nintendo / Rareware. The game, ROM, and assets are theirs. GEVR does **not** ship a ROM or game assets. You bring a USA `.z64` you own. |
| **GEVR workshop binary** | Our OpenXR / VR delta on top of the native port. Separate from the game data and from upstream licences below. |

---

## Adapted or built on (specific credit)

### Evan King / GETV platform layer (MIT)

- **What:** The from-source PC host / platform layer GEVR's VR work sits on (`goldeneye-native` lineage).
- **Credit for:** Host, port scaffolding, Fast3D wiring, and the native bring-up path we extend for OpenXR.
- **Not claiming:** Their MIT work as closed or as "GEVR-only." Their notice travels with derived portions.
- **Licence in tree:** see the GETV / `goldeneye-native` `LICENSE` (Copyright (c) 2026 Evan King).

### Alex-LeTux / perfect_dark_VR (MIT) - design map, not vendored code

- **Repo:** https://github.com/Alex-LeTux/perfect_dark_VR  
- **Surveyed:** branch `port` @ `67ea20c86986c6bc85687f26a27418b266af309c`
- **What we took (recorded influence only - their VR tree is not copied into GEVR):**
  - Controller quaternion basis of the form `{w, -x, y, -z}` (hand-axis knobs)
  - Pistol grip offset `(0, 16, -4)` scaled into our gun-offset knobs
  - `x/(1-damp)` integrator pre-load idea
  - Drawn-vs-shot clamp split (aim draw path vs fire path)
- **Also:** Perfect Dark VR's *waiting-room / hub feel* informed our cinema-hub direction (procedural room + world-locked board). We map the idea; we do **not** vendor their hub sources.
- **Upstream notices that travel with that lineage:** Perfect Dark decomp (Ryan Dwyer et al.) and the Perfect Dark PC port (MIT).

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

## Looked at, not adapted into GEVR

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

- [CONTRIBUTING.md](CONTRIBUTING.md) - how to play vr440 and file bugs
- [LICENSE-MAP.md](LICENSE-MAP.md) - whose license is whose
- [README Play](README.md#play) | [Releases/latest](https://github.com/no6969el/GEVR/releases/latest)
- [README](README.md) · [Beta notes](docs/BETA.md) · [Controls](docs/CONTROLS.md)
- [PRIOR-ART.md](PRIOR-ART.md) - Perfect Dark VR influence detail
- [docs/ORIGIN.md](docs/ORIGIN.md) - ours vs upstream tags
- [LICENSE](LICENSE) - this public docs/tools tree
- [Roadmap](docs/ROADMAP.md) · [Coming soon](docs/COMING-SOON.md)
