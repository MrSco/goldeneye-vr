# Status

**Current state (2026-09-23, first public release v0.1.0):** the Dam plays
end to end in true stereo VR (controller-aimed guns firing from the muzzle,
3D sight, ammo panel on the gun, health HUD, left arm, watch gesture, head
translation, smooth/snap turning, comfort vignette) and on the virtual screen
(flat or curved, grab to move, laser-pointer menus), launched from an in-VR
launcher. The app ships no game content: everything comes from the player's
ROM. HANDOFF §46-56 has the detail. Known issues for the next release:
the launcher cursor hides behind a curved screen and the pointer has no beam;
bullet-hole sprites glitch at some angles; props such as the Dam gate button
appear late (draw distance). Other levels are less tested.

The tables below are the older ledger (last fully revised 2026-09-22) and are
kept for their evidence; the paragraph above and HANDOFF supersede them.

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
| **seen** | **Dam is playable.** Move, shoot, kill guards, take damage, pick up an AK; music and SFX both work | [HANDOFF §18](HANDOFF.md) |
| **open** | Screen-mode controls replaced with native 1.2 mapping: left move, right look, trigger fire, grip aim, B/X use/reload, A/Y cycle, Menu watch. Grip + left down/up crouches/stands. Built/installed, headset check pending | [HANDOFF §21](HANDOFF.md) |
| **open** | Aim/watch crash fixes D137/D140/D191 applied from gepc-ref and installed; synthetic aim test passes, headset verification pending | [HANDOFF §21](HANDOFF.md) |
| **seen/open** | User confirms HUD bullet-ammo texture improved. PP7, smoke, initial bullet impacts and other textures still wrong | [Texture audit](docs/texture-port-audit.md) |
| **open** | Bullets pass through the guard tower glass; level brightness too high | [HANDOFF §18.4](HANDOFF.md) |
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

**Read [HANDOFF §14](HANDOFF.md) first, then §13.** §14 has the reference port
that should have been used from the start, what it does and does not cover,
and the next fix already written out. §13 has the defect class behind most of
this.

The single most useful fact: `../gepc-ref`
(github.com/jkdansereau/goldeneye-pc-port) is the same decompilation taken to
64-bit, with 429 `#ifdef PORT` sites indexed in
[docs/gepc-port-worklist.md](docs/gepc-port-worklist.md). Work that ledger
rather than the next tombstone - but read §14.3 first, because this port has a
superset of its defects and its silence about a site is not a clean bill of
health.

1. **Textures.** User reports D228 alone did not resolve corruption. The
   [texture audit](docs/texture-port-audit.md) adds D74/RC2/D161/D217 and
   reconciles upload dimensions with UV normalization, including CI4 HUD
   rectangles. Synthetic tests and Android build pass; visual check pending.
   The earlier constant-palette diagnosis was a probe error, not the cause.
2. **Controls** (§18.3) - no door-switch button means the level cannot be
   finished. Port GEVR's scheme rather than inventing one.
3. **Pin the intro camera before comparing two crashes.** Dam picks one of
   six at random; write an index to
   `/sdcard/Android/data/com.gevr.port/files/gevr_introcam.txt`. It is set
   to 0 on the device. Several earlier "it crashed again" rounds were
   different crashes because of this.
4. **Do not trust the log over the headset.** `lvlStageLoad done` appeared in
   the log for several builds while the user saw nothing but a crash to the
   Quest shell. This session made that mistake in writing and had to correct
   it.
5. Known-wrong and waiting: the 12 bad stan pointers, and
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
