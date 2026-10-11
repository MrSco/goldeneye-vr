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

Tomorrow Never Dies (Expanded 06-22): the binder runs on it (3045 images, 112
characters, 340 props, 696 files, 31 gun stats) but its code diff (3791 words,
312 runs, same method) shows what is still to port:
- mission constants: its code moves the Cradle test 17 -> 9 (credits after
  mission 9, the retail Cuba flow), Aztec 18 -> 11, Egypt 19 -> 12 and the
  mission count 20 -> 14 (7F0168BC/D4, 7F011D5C, 7F0192C4, 7F01EA00-EAB0,
  7F01F4C8). The PR's front.c branch for it (mission select instead of the
  credits) is not what the cartridge does; use these numbers in the retail
  flow instead,
- the laser's tests set to 153 (its FAMAS on GlaserZ): an itemas row, a rifle,
- `bodyChooseHead` keeps retail's form with `& 7` (7F0235E4) and its own
  random head lists in the data segment,
- animation records (moved; read `animation_table_ptrs1/2` from its data),
- Rare logo at ROM 0x297930 (the PR reads GoldenEye's 0x29e560),
- its own held props for the silver/gold PP7 slots (230, 231; 7F09B350),
- Girl Power hooks (Dab's `tndHookConvert`); mission names in LmiscE.

Bean + GE-X: GE-X keeps the arms and guns; draw Bean's watch (suitlfhand
materials 9-11 and the three watch hands) on the GE-X wrist instead of the N64
watch, refitting the face gauges to it.

