# GoldenEye VR

A standalone VR port of **GoldenEye 007** for Meta Quest, built from the
GoldenEye decompilation. Renders through OpenGL ES with OpenXR for headset and
controller tracking, and runs natively on the headset — no PC, no streaming.

> **No game data is included, and none ever will be.** GoldenEye 007 and its
> assets belong to Nintendo / Rareware. You supply a USA `.z64` cartridge dump
> that you own. The repository ignores every ROM extension; see
> [.gitignore](.gitignore).

**Status:** in active bring-up. The whole title sequence runs and is watchable
in the headset with sound; gameplay has never run. See **[STATUS.md](STATUS.md)**
for what works, what is broken and what is next — and
[HANDOFF.md](HANDOFF.md) for the session-by-session engineering log behind it.

---

## Build

**Requirements**

| | |
|---|---|
| Host | Windows |
| Target | Android / Horizon OS, `arm64-v8a` |
| NDK | `25.1.8937393` (Clang) |
| CMake | 3.22.1 |
| Device | Quest 2 / Pro / 3 / 3S with developer mode enabled |

The root `CMakeLists.txt` builds `libgevr.so`; the Gradle project in `android/`
packages it. The first configure fetches **SDL2** (`release-2.32.8`) and
**zlib** (`v1.3.1`) via CMake `FetchContent` (see [cmake/android.cmake](cmake/android.cmake)),
so a fresh clone needs network access for that first build. The OpenXR loader
is vendored under `OpenXR/` and is not fetched.

Point `android/local.properties` at your SDK (it is gitignored —
Android Studio will write it on first open, or create it by hand):

```
sdk.dir=C\:\\Users\\<you>\\AppData\\Local\\Android\\Sdk
```

Then:

```bash
cd android && ./gradlew.bat assembleDebug
```

The APK lands at `android/app/build/outputs/apk/debug/app-debug.apk`.

> **Clone somewhere shallow on Windows.** The NDK build nests object files
> deeply under `android/app/.cxx/`, and a long clone path pushes them past
> `MAX_PATH`. CMake emits `CMAKE_OBJECT_PATH_MAX` warnings and the build then
> fails with `ninja: error: manifest 'build.ninja' still dirty after 100
> tries`, which looks like a CMake bug and is not one.

## Run

Push your ROM to the app's data directory as `ge.z64`:

```bash
adb push "007 - GoldenEye.z64" /storage/emulated/0/Android/data/com.gevr.port/files/data/ge.z64
```

Build, install, launch and capture a filtered boot log in one step:

```bash
powershell -File tools\gevr_boot_test.ps1 -Seconds 24
```

That script also broadcasts `com.oculus.vrpowermanager.prox_close`, because the
headset suspends immersive apps when nobody is wearing it and the app would
otherwise pause a few frames in. Guardian still gates launches off-head, so
wear the headset for anything past init.

Full log afterwards:

```bash
powershell -Command "Get-Content $env:TEMP\gevr_boot.log -Tail 100"
```

## Layout

| Path | What |
|------|------|
| `src/` | GoldenEye engine and game code from the decomp |
| `src/libultra/`, `src/libultrare/` | N64 SDK audio/graphics libraries |
| `port/src/`, `port/include/` | Host layer — platform, ROM loading, soft audio mixer |
| `port/fast3d/` | RDP display-list translation |
| `port/vr/` | OpenXR session, stereo submission, controller input |
| `include/` | Shared headers (`include/PR/` is the N64 SDK's) |
| `assets/` | Converted asset tables compiled into the binary |
| `android/` | Gradle project, `MainActivity`, SDL Java layer |
| `OpenXR/` | Vendored OpenXR loader (headers + prebuilt libs) |
| `cmake/` | Toolchain and helper modules |
| `tools/` | Build, probe and analysis scripts — see below |
| `docs/` | Deep-dive notes on specific subsystems |
| [STATUS.md](STATUS.md) | Current state — start here |
| [HANDOFF.md](HANDOFF.md) | The engineering log behind it |

## Tools

Host-side Python and PowerShell helpers, all under `tools/`:

- `gevr_boot_test.ps1` — build, install, launch, capture a filtered boot log
- `gevr_rom_probe.py`, `gevr_model_probe.py`, `gevr_stage_probe.py`,
  `gevr_menu_probe.py`, `gevr_frontend_probe.py`, `gevr_blood_probe.py` —
  inspect ROM structures without running the game
- `gevr_gen_rom_manifest.py`, `gevr_gen_anim_offsets.py`,
  `gevr_make_runtime_file_table.py` — regenerate generated tables
- `gevr_implicit_decls.py` — maintain `src/gevr_implicit_protos.h` from a build
  log (see the porting-defect notes in `HANDOFF.md`)
- `gevr_mixer_ab/` — compiles the soft mixer twice into one arm64 binary and
  A/Bs scalar against NEON on real bank data, on the headset
  ([README](tools/gevr_mixer_ab/README.md))

Most ROM probes take the cartridge path as their first argument:

```bash
python tools/gevr_model_probe.py "007 - GoldenEye.z64" <FileName> <numSwitches> <numTextures>
```

## Porting notes

This is a 32-bit big-endian console port running on a 64-bit little-endian
device, and the recurring defect classes are documented rather than
rediscovered. `HANDOFF.md` covers byte-order bugs and truncated pointer slots;
[docs/RARE-LOGO-AUDIO-HANDOFF.md](docs/RARE-LOGO-AUDIO-HANDOFF.md) is a worked
example of a third class — an index bias the original encoded as a *struct
offset*, which LP64 alignment padding silently erased.

## Credits and licence

[CREDITS.md](CREDITS.md) records what this work actually leaned on and what is
owed to whom — the GoldenEye decompilation, the GETV / `goldeneye-native`
platform layer, the Perfect Dark decomp and PC port, and others.

This tree is MIT licensed; see [LICENSE](LICENSE). That covers this project's
own contributions — vendored components keep their own notices:

| Path | Upstream | Notice |
|------|----------|--------|
| `port/` | [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark) — the whole host layer | [`port/LICENSE`](port/LICENSE), [provenance](port/README.md) |
| `port/vr/` | [Alex-LeTux/perfect_dark_VR](https://github.com/Alex-LeTux/perfect_dark_VR) — the whole VR layer | [`port/vr/LICENSE`](port/vr/LICENSE), [provenance](port/vr/README.md) |
| `src/` | [n64decomp/007](https://github.com/n64decomp/007) — the GoldenEye decompilation | upstream ships no licence file |
| `port/vr/imgui/` | Dear ImGui | `port/vr/imgui/LICENSE.txt` |
| `port/vr/miniz/` | miniz | header of `port/vr/miniz/miniz.c` |
| `port/fast3d/` | Emill / n64-fast3d-engine | `port/fast3d/LICENSE.txt` |
| `OpenXR/` | Khronos OpenXR loader | Apache-2.0, see the headers |

The game itself is not covered by any of that and is not distributed here.
