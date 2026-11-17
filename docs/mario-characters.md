# GoldenEye with Mario Characters V3.17

Branch notes (2026-11-16). Hack: "GoldenEye with Mario Characters V3.17",
[n64squid](https://n64squid.com/homebrew/roms/goldeneye-mario-characters/),
`MarioCharactersV3.17.xdelta` (2022-09-26) from `GE-with-Mario-Characters-V3.17.zip`.
The zip's `CHANGELOG - v3.1 Modding.txt` is the hack's own memory map and is
the first thing to read.

Apply the patch to a big-endian GoldenEye 007 (USA) `.z64`
(`python tools/gex/vcdiff_apply.py ge.z64 MarioCharactersV3.17.xdelta mario.z64`).
The result is 16 MB, title `GOLDENEYE`, code `NGEE` (the same as retail and
TND64), CRC bytes `59 BE 5A 72 BC 7E 5C 17`. It is stored as `data/mario.z64`
and runs as campaign 4 (`GEVR_CAMPAIGN_MARIO`, layout tag `mario`, save
`eeprom-mario.bin`). Like the other hacks it is solo play only: online play,
co-op, bots, GoldenEye X, Bean, HD packs and the launcher's cheat list are off.

No ROM, patch or asset is in the repository: only offsets and counts.

## What it changes

The hack is built on Carnivorous' Footsteps patch, so unlike Mirror Mode it
changes code as well as data. Word diff of the game code (unpacked at ROM
0x34B30 in all four cartridges) against retail, Goldfinger 64 and TND64:

| Words changed | Shared with | Meaning |
| --- | --- | --- |
| 1091 | GF64 and TND64 | the editor's Expansion Pak changes; already bound |
| 127 | GF64 only | Goldfinger's random-head system (bodyChooseHead) |
| 126 | TND64 only | moved animation records |
| 330 (163 runs) | none | Mario's own; plus 276 boot-segment words |

Its own boot-segment words are mostly new MIPS routines written over dead
debug printers (0x80004758, 182 words; 0x80004A90, 39; 0x80004E98, 17),
reached by jumps from chrlvTickDead, chrlvTickThrowGrenade and the AI
interpreter. They keep state in a mailbox at 0x807000A4-0x807000DC.

## Step 1: bind the cartridge (done on this branch)

`gevr_romlayout.c` row `mario`, read off its code with a relocation map
(every changed lui/lo16 pair, retail address to Mario's):

- Expansion Pak pieces from its notes: characters 0x80702000 <- ROM
  0xFFD000 (0x3000), hat fits 0x80705000 <- 0xFF8800 (0x4800), props
  0x8070A000 <- 0xFF0000 (0x8800). Characters at 0x80702FC0 (126), props at
  0x8070CA80 (340), explosions 0x8070EE60, hats 0-125.
- Images: 3194 four-byte rows at 0x80049300, bank at ROM 0xC0A734 (7F0CBCC0).
- Banks (all moved): animation 0x299450, global images 0x2A84E0 (0x17A0),
  Rare logo 0x2A9C80, gunbarrel 0x2B0470, fonts 0x2F1B10/0x2F3FC0, sfx
  0x2F7500/0x2FD0C0 (samples 0x2C5528: two sounds added), instruments
  0x5C25E8/0x5C6988, music 0x627928 (63 tracks, 0x229F6 bytes). Sample
  extents measured from each bank's ALBank sounds.
- Solo outfits (7F079D94-7F079F3C, jump tables 0x80054FE4/0x80055008):
  every cuff picks one of its solo slots 122-125 (Mario, Luigi, Peach,
  Daisy) with the invisible head 78. Its scripts change those slots' files.
- Intro Bond 41/78 (7F009088). Held props: retail, with 230/231 for the
  silver and gold PP7 (7F09B350, as TND).
- Head pools (7F023668): Goldfinger's code with four sets for heads -1 to -4
  (16, 12, 8, 4 heads, men and women), seed bits 16-19 (`& 15`, 7F0235F4) and
  the offset for women too (7F0235F8). Layout fields `headmask`, `headwomen`.
- Moved data-segment tables (new layout fields, 0 = GoldenEye's address):
  animation_table_ptrs1/2 0x80023718/0x80023A04, memallocstringtable
  0x8002B19C (37 rows), random_tracks 0x8004F410 (54 tracks).
- Its music table has 35 rows (GoldenEye 23): music_0D2720.c keeps 24 spare
  rows and 20 spare tracks under GEVR (`gevrMusicRowsMax`, `gevrRandomTracksMax`).
- Its file table has 735 files (GoldenEye 726): the binder widens
  `file_entry_max` up to ob.c's OBJ_INDEX_MAX (736, `gevrFileSlots`).
- It renamed 14 hand items (fist `Garmr1Z`, PP7 `Gwppk1Z`, left hand
  `Garmlw6Z`, ...). An item whose row names none of GoldenEye's files now
  loads the hack's file by its own number, with its header, as the
  cartridge does. Goldfinger and TND have none (checked).
- Animations: indices 0-182 keep GoldenEye's meaning (11 have new content,
  the rebuild takes the hack's); it adds 183-185. initanitable.c has 8 spare
  slots and `gevrRomRebuildAnimationData` appends them (`GEVR_ANIM_APPEND`).
- Its added setups have no trailing Z (`Usetupark3`, `Usetupdam2`); the
  setup-name check accepts them.
- Its collision files are `Tbg<stage>Z` (GoldenEye's `Tbg_<stage>_all_p_stanZ`):
  ob.c converts any file starting `Tbg` (only collision files do, in both).
  Before that, Dam crashed in stanDetermineEOF on the unconverted file.
- Unchanged from retail (its code there is GoldenEye's): portal-culling
  level cases, gadget items, autogun rate, glass, timed mine, weapon case,
  rifle-butt melee, cars, lamps (bit 23, as the other hacks), mission
  progression.

Seen running (2026-11-16, Windows flat build, `tools/build_pc.py`, mario.z64
and `Campaign=4` beside the exe): the launcher offers Mario Characters, its
Nintendo logo and coin intro play, the file select shows Mario's portraits,
and `--boot-stage 33` plays Dam with Mario's glove on its PP7. The Quest
build compiles; the headset run was stopped by a Guardian dialog (desk).

Check: `GEVR_MARIO_ROM=<mario.z64> python port/tests/test_romcampaign.py`
runs the production binder on the real ROM and compares it with an
independent decode (734 files, 3194 images, 126 characters, 340 props, music,
random tracks, memory splits, setups, four head pools, hat fits, renamed
items and their stats, 176 animation records incl. the 3 added). The
Goldfinger and TND checks still pass with the same changes.

## Step 2: its code, in C (to do)

Port each own run behind `gevrRomCampaign() == GEVR_CAMPAIGN_MARIO`:

- AI interpreter: repurposed commands at 7F0362D8 (toggles a flag byte in
  the table at 0x80075D30), 7F038F38, 7F039DA8 (xor-toggles 0x800364B4),
  7F03A104. Every solo level's scripts use them for Play as Luigi / Peach /
  Daisy (renames the solo slots' and hands' files), Tiny Mode and Mushroom
  Mode, so the levels need them.
- The boot-segment routines and the mailbox (model scale, play-as state).
- chrRenderProp 7F022668: a dead guard is skipped only when byte 0xC is 0xFF.
- Footsteps patch gameplay: random guard sizes, pitched pain voices, the
  grenade-draw sound (sfx 0x117?), health/armour display on pickup, the
  XBLA fix of action block 000D, enemy rockets for spawned guards. Its
  anti-aliasing pause option and multiplayer spawn shuffle do not apply.
- Its attract cast (7F01881C: body 0x7A picks one of 0x16-0x19).

## Step 3: its missions (to do)

- Mission select page 2 (constructor_menu07_missionsel 7F01014C-7F010588,
  0x40 -> 0x50 and 0xD -> 0x13/0x1B): Aztec, Egypt, Castle, Bob-omb
  Battlefield, Big Boo's Haunt. Page 1's folder (0x8002ABE4) already binds:
  20 missions, Credits as 18 (stage 54) and Temple as 19 (stage 38).
- Level row 37: Mario gives it stage 21 (Usetupark3) instead of 57; the
  binder keeps GoldenEye's and logs it.
- Its extended mission numbers (0x8004F310), chapter numbers (0x8005BA00),
  briefing names (0x80055A70) and per-level allocations (0x80055CC0).
- The 3 added animations' users, its MP character table (0x80028490; MP is
  off).
