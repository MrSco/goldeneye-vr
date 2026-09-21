# Status

**Updated 2026-09-21.** Where this and [HANDOFF.md](HANDOFF.md) disagree, this
file wins — HANDOFF is a session-by-session engineering log kept for its
reasoning, not as a statement of current state.

**In one line:** the whole title sequence runs and is watchable in the headset
with sound; gameplay has never run.

Evidence is marked, because the difference has bitten this port before — a
screen can be "working" in the logs while the headset shows black:

- **seen** — a person looked at it in the headset and accepted it
- **logged** — the code path runs clean, nobody has looked
- **open** — known broken or unfinished

---

## Works

| | What | |
|---|---|---|
| **seen** | Boots on Quest standalone, no PC. Loads your `ge.z64` (706 of 727 files bound; the other 21 are PAL-only) | |
| **seen** | Legal page, Nintendo logo, Rareware logo, gun-barrel intro, GoldenEye logo, cast screens, title reload | |
| **seen** | Presentation: the game's frame on a world-locked cinema screen, black surround, no flicker | [HANDOFF §7.0](HANDOFF.md) |
| **seen** | Textures — the halftone/"glitch" grid was a TMEM odd-row swizzle | HANDOFF item 32 |
| **seen** | Gun-barrel stripes gone; blood drip animates | HANDOFF §9 |
| **seen** | File select opens on START; START advances through stage and difficulty | HANDOFF §10 |
| **seen** | Audio — music and SFX through the soft mixer at 22050 Hz | §11 |
| **seen** | Quitting to the Quest home is clean and immediate | §11 |
| **seen** | App shows as "GoldenEye VR" with an icon in the Quest library | §12 |
| **seen** | Mission-select folder background restored; user confirmed after missing-return fix | HANDOFF 12.6 |
| **logged** | Left thumbstick drives the file-select crosshair; A / trigger / B | HANDOFF §10 |

## Open

| | What | |
|---|---|---|
| **open** | **Gameplay has never run.** Root cause found and fixed, **not yet seen on device**: `StandTile` did not match the 8-byte cartridge tile layout `gevrConvertStan` produces, so every tile field read two bytes late. Explains both the hang and the wild-pointer fault | [HANDOFF §12.7](HANDOFF.md) |
| **open** | Mission-complete missing-return fix and other menu screens need verification; mission select is now **seen** fixed | HANDOFF 12.6 |
| **open** | Rest of the level loader unported: stage setups (`U...Z`) and `bg.c`'s segment pointer arithmetic | [HANDOFF §5 step 3](HANDOFF.md) |
| **open** | True-stereo gameplay camera not started. `gevrVrScreenMode = 0` switches back to the direct path when it is | HANDOFF item 33, §7.2.7 |
| **open** | Briefing crash fix and the front-end artwork/portrait fixes shipped in the 20:39 build but were never accepted in the headset — treat as unverified | HANDOFF §10 |
| **open** | Lighting looks dark on the Nintendo logo and on characters. Unexplored; start at `calculate_normal_dir` / lookat | HANDOFF §7.2.8 |
| **open** | Attract demos unbound. A guard in `src/game/ramromreplay.c` calls `bossRunTitleStage()` instead. Needs the `ramrom_*` segments in the manifest and a byte-swap of `ramromfilestructure` | HANDOFF item 11 |
| **open** | System recenter leaves the cinema screen left of view. Fix built and installed, **not yet seen on device**: stop recreating the play space on `REFERENCE_SPACE_CHANGE_PENDING` and re-place the screen once frames pass `changeTime`. App recenter is hold-left-stick-click | [HANDOFF §12.7](HANDOFF.md) |

## Closed, but not by us

The universal menu's quit dialog says **"App name unavailable"** and shows no
icon. This is a Quest limitation for sideloaded apps, not a defect here:
VirtualBoyGo, installed from the same Unknown Sources list, shows the same
text. The library list name and icon are correct. Nothing further to do.

## If you are picking this up

Both open items have a fix built and installed that **nobody has looked at
yet**. The next session's first job is to watch them, not to write code:

1. **Select Dam.** If it loads, the `StandTile` layout fix (HANDOFF 12.7) is
   confirmed and gameplay has run for the first time. If it hangs or faults,
   the `dam-pad:` probes still log the pad and stan pointers.
2. **Hold the right-controller menu button to recenter.** The cinema screen
   should land in front of you rather than to the left.

Note that the tile fix also corrects `tile->room`, which portals, AI and
explosions all read - so expect changes beyond Dam loading, and be ready for
the next defect to be a different one rather than a regression.

The working method here has been: probe, capture on device, and let the log
decide. Every time this session guessed instead, the guess was wrong - and
each wrong guess is recorded in §12 so the next reader inherits the
eliminations rather than the dead ends.

## Debug hooks still compiled in

All still present and all owed removal before any release build:

`menubg:` and the bound-pad / PadID bounds checks (added 2026-09-21) ·
`gfx:` and `input: pad0` once-a-second stats · `badvtx:` in `gfx_sp_vertex` ·
four `stage:` logs in `boss.c` · `menu-pump:` / `gevrPumpStage` counters ·
the stall watchdog (`gevr_watchdog_kill.txt` marker) · the display-list dump
(`gevr_dumpdl.txt`) · PC input injection (`gevr_input.txt`) · the demo guard.

## How this gets tested

The user tests in the headset and reports; the agent builds, installs and
reads logs. **Do not run blind launch-and-capture loops from the PC** — log
evidence cannot tell a rendering frame from a black one, and that mistake cost
this port several sessions.

```bash
powershell -File tools\gevr_boot_test.ps1 -Seconds 24
```

Logs come out under the `GoldenEye`, `GoldenEye-VR` and `GEVR` tags. Guardian
gates launches when the headset is off-head, so anything past init needs it
worn. Headset screenshots and PC-driven input exist as fallbacks — see
[HANDOFF §7.3](HANDOFF.md).

## Reading further

| | |
|---|---|
| [HANDOFF.md](HANDOFF.md) | The full log. §11 is newest; §3 is the defect-class table, which is the most reusable thing in it |
| [docs/RARE-LOGO-AUDIO-HANDOFF.md](docs/RARE-LOGO-AUDIO-HANDOFF.md) | A worked example of one defect class, start to finish |
| [CREDITS.md](CREDITS.md) | What is vendored from where |
| [port/README.md](port/README.md), [port/vr/README.md](port/vr/README.md) | Provenance of the host and VR layers |
