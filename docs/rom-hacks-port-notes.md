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
fits, the intro Bond, special items played as plain guns (`GEVR_ITEM_AS`), each
stage's memory split (`memallocstringtable`; a hack run keeps at least GEVR PC's
4 MB room heap).

Held out of a hack run: online/co-op/bots, GoldenEye X, Bean, HD packs, the
launcher's cheat list (the game's own Cheat Options stay). Separate save per
campaign.

## Verified

- `GEVR_GF64_ROM=<goldfinger64.z64> python port/tests/test_romcampaign.py` runs
  the production binder on the real Goldfinger 64 1.0 ROM and compares every
  table it writes with an independent decode (716 files, 3915 images, 126
  characters, 416 props, 31 guns' stats, head picks, 756 hat fits, levels, fog,
  music, times, memory splits). It says SKIP without the ROM.
- All `port/tests/test_*.py` pass except `test_romcheck` (fails the same way on
  main: Windows ctypes cannot find zlib1.dll). Bean tests run under WSL
  (`port/tests/test_bean*.py`, `test_launcher_mods.py`); see the Bean handoff.

## Open

Goldfinger: its whole code diff (2125 changed instructions, 347 runs) is
classified (session scratch `gfdiff/classification.md`; rebuild with the same
word diff). Done from it: prop pool 1600 (its init at 7F0014E8 chains 1000 in
Expansion Pak RAM; the Cartel places 1158), Cradle/Dam level cases off,
gadget-use items 49/57, each stage's memory split, lamps by image-row bit 23
(7F0BB630; Tomorrow Never Dies has the same code), and as layout values: autoguns
`& 3`, glass pierced by its Ruger/Spectre-file guns, no timer on item 33 (its
Knockout Gas), weapon-case grenades, no rifle-butt melee; the drivable Aston
Martin (a tank record not on PtankZ: no shells, sounds 0/63/0, speed 20,
acceleration offset 0.75; no digital step-back reverse in any vehicle;
7F082ADC, code at 0x8005C550). Still to port, by impact:
- knife/golf club fist reach 100 (7F03BED8; intent inferred, first candidate only),
- hat types for props 212-223, default 4 (`get_hat_model` 7F05268C),
- monitor programmes (7F049CDC),
  yellow watch/HUD (`watchtint`), the attract cast table (0x8002B600,
  7F01881C), no-armour cheat, explosion retry (optional).
- Headset: the Cartel loaded through models before the prop pool fix
  (2026-10-11 13:02); with it, it played with walls missing and textures
  flickering (13:27): GoldenEye's Dam memory split (room heap 275 KB for
  rooms that unpack to 1.6 MB of vertices; texture pool 625 KB, the hack's is
  800). Fixed by binding the hack's split; next: retest, then a mission end
  to end.

Tomorrow Never Dies (Expanded 06-22): the binder runs on it (3045 images, 112
characters, 340 props, 696 files, 31 gun stats) but its code diff (3791 words,
312 runs, same method) identified the following campaign changes.
Mission progression now uses its Cradle test 17 -> 9 (credits after
mission 9, the retail Cuba flow), Aztec 18 -> 11, Egypt 19 -> 12 and mission
count 20 -> 14 (7F0168BC/D4, 7F011D5C, 7F0192C4, 7F01EA00-EAB0,
7F01F4C8), including bonus difficulty gates, completion flags and the return
from credits. Mission 13 retains its no-advance boundary. Remaining code changes:

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

