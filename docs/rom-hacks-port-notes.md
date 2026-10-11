# Hack campaigns: branch notes (2026-10-11)

Branch `cursor/rom-hack-layouts-026b` (PR #172), reworked in place, plus PR #170's
Project Bean branch merged in (2d32be9, with the Codex session's unfinished repair
pass as 25ab820). Player docs: [rom-hacks.md](rom-hacks.md). Bean notes:
[project-bean-handoff.md](project-bean-handoff.md).

## How a hack runs

GoldenEye's own C code runs the hack's data. `gevr_romcampaign.c` binds the hack
at `romdataInit`: its data segment and Expansion Pak pieces, then every table
GoldenEye compiles in that the hack edits. Numbers come from Dab's Mod
(`g_Layouts`) and from the hack's own code, read with a word-for-word diff of the
two cartridges' game code (both keep it unpacked at ROM 0x34B30).

Bound from the hack: file table (names too; kept across `obInit`), images,
characters with their model headers, the 416-row prop table and explosions, gun
headers and stats (each hack item on the US item with the same gun file), AI item
arguments, level scale/visibility/BG names, fog and fogless skies, music, target
times, mission folder, random-head pools (Goldfinger's own `bodyChooseHead`), hat
fits, the intro Bond, special items played as plain guns (`GEVR_ITEM_AS`).

Held out of a hack run: online/co-op/bots, GoldenEye X, Bean, HD packs. Separate
save per campaign.

## Verified

Offline only; nothing has run on a headset yet.

- `GEVR_GF64_ROM=<goldfinger64.z64> python port/tests/test_romcampaign.py` runs
  the production binder on the real Goldfinger 64 1.0 ROM and compares every
  table it writes with an independent decode (716 files, 3915 images, 126
  characters, 416 props, 31 guns' stats, head picks, 756 hat fits, levels, fog,
  music, times). It says SKIP without the ROM.
- All `port/tests/test_*.py` pass except `test_romcheck` (fails the same way on
  main: Windows ctypes cannot find zlib1.dll). Bean tests run under WSL
  (`port/tests/test_bean*.py`, `test_launcher_mods.py`); see the Bean handoff.

## Open

Goldfinger:
- The rest of its code diff (2125 changed instructions, 347 runs) is being
  classified; any behaviour patch found there still needs porting.
- Headset: boot, folder, briefing, a level each, guards' heads and hats, guns.

Tomorrow Never Dies (Expanded 06-22), not started past the PR's numbers:
- animation records (moved; read `animation_table_ptrs1/2` from its data),
- Rare logo at ROM 0x297930 (the PR reads GoldenEye's 0x29e560),
- `bodyChooseHead` with `& 7` and its own head lists,
- Girl Power hooks (Dab's `tndHookConvert`), `nolaser` (its FAMAS on GlaserZ),
- mission names in LmiscE as well as LtitleE, credits after mission 9.

Bean + GE-X: GE-X keeps the arms and guns; draw Bean's watch (suitlfhand
materials 9-11 and the three watch hands) on the GE-X wrist instead of the N64
watch, refitting the face gauges to it.

