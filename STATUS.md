# Status

**Updated 2026-09-21 (evening).** Where this and [HANDOFF.md](HANDOFF.md) disagree, this
file wins — HANDOFF is a session-by-session engineering log kept for its
reasoning, not as a statement of current state.

**In one line:** the whole title sequence runs and is watchable in the headset
with sound. Dam's satellite intro was seen once, then a guard's AI script
crashed. The next two installs never reached that text: widening the ground
callback grew `Model` and died in the tank setup, and after that revert the
first tick called the same callback through a truncated address
(`sub_GAME_7F06D490`, fault `0x35f5fbe8`) before the briefing was presented.
The pointer now lives in a side table; `Model` is unchanged. That call held, and so did the patrol path: the satellite line and the caption after it both drew (build `3b3791a6`), then `textMeasure` faulted at `0xb64` because the caption font slot the renderer read was still null. Widening those slots to pointers reproduced the tank crash during the load (`proplvreset2`, fault `0x423000020`, build `6ea83226`) and skipped the intro; that widening is reverted. The caption now matches the stored low half against the real font pointers. The next test died on Start, in `matrix_4x4_set_lookat` (build `f3d43302`, fault `0xffffffffac19b680`): the view matrix from `dynAllocateMatrix` was stored in `player.field_64`, an `s32`, and sign-extended. The satellite lines are the briefing HUD. The dam view uses that matrix; the player struct was not widened. The matrix is kept in a local and copied into the existing pointer fields. That call held. The next test died while drawing the first prop, in `matrix_4x4_multiply_homogeneous` (build `87201a9d`, fault `0xb27105a0`): `sub_GAME_7F08BEEC` added the joint-matrix pointer as a `u32`. That add is now a full pointer. Not yet tested.

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
| **seen** | Dam intro: the satellite text screen draws, then the app crashes back to the Quest home. The portal-depth fix held | |
| **seen** | Dam intro: satellite text, then a second caption line, then a crash back to the Quest home. Build `3b3791a6` | |
| **open** | **Gameplay has never run.** After the second caption, `textMeasure` faulted at `0xb64` because the font slot it read was null. Widening those slots reproduced the tank crash in `proplvreset2` (fault `0x423000020`) and was reverted. The installed build leaves the slots 32-bit and matches that low half to the real font pointer. Not yet tested | |
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

**Read [HANDOFF §13](HANDOFF.md) first** - it has the nine defects, the one
defect class behind almost all of them, what is still open, and the method
notes that actually worked.

1. **Select Dam.** The satellite line and the caption after it have been
   seen. The font slots are 32-bit again; widening them crashed the load
   in the tank. The caption matches the stored low half to the real font.
   `Model` is still the size the slot pool was built for. If it dies, the
   tombstone names the frame.
2. **Do not trust the log over the headset.** `lvlStageLoad done` appeared in
   the log for several builds while the user saw nothing but a crash to the
   Quest shell. This session made that mistake in writing and had to correct
   it.
3. Two known-wrong things are waiting: the 12 bad stan pointers, and
   `bondhead.c` writing into Bond's `Model` through mislabelled `field_*`
   members. Both are in §13.4.

The working method here has been: probe, capture on device, and let the log
decide. Every time this session guessed instead, the guess was wrong - and
each wrong guess is recorded in §12 so the next reader inherits the
eliminations rather than the dead ends.

## Debug hooks still compiled in

All still present and all owed removal before any release build:

See [HANDOFF §13.5](HANDOFF.md) for the ones added on 2026-09-21 evening -
two of them change behaviour, not just logging. Earlier hooks:

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
