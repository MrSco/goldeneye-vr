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

## Step 2: its code, in C (done)

Most of what Mario Characters does is in its level scripts, not its code:
Carnivorous' Footsteps patch makes AI command 0x2B ("2B FE", "2B FD"...) run
the PRINT after it as a memory poke. The cartridge's interpreter (0x7005C250,
called from the 0x2B case at 7F0360A0; TND64 carries the same code, see DAB
geconvert.c tndHookConvert) is ported whole in `src/game/gevr_footsteps.c`:

| Form | What it does |
| --- | --- |
| `2B 00` | music volume back to what level start saved |
| `2B nn` | the stage's X track is nn |
| `2B FE "AAAAAAAA[+Poooooooo]+Xvvvvvvvv"` | pointer steps, then C store, A/S add/subtract (F floats, T per tick), E/H/L go to `+Rxx` |
| `2B FD "SSSSSSSS++DDDDDDDD+C[n]"` | copy n bytes (4); `+A`/`+S` (F) puts S +/- D in D |
| `2B FC "ii"` | take item ii (FF: 32 down to 2) |
| `2B FB` | kill Bond |

Its 12,789 commands (all setups) name: gun and character file names (Play as
Luigi/Peach/Daisy, random pilot heads), character scales (Tiny Mode,
Mushroom Mode), the POV height constant, Cheat Options' toggles, Bond's
health/armour/outfit, objectives, the mission folder (Temple writes page 2
into it), the cast table, fog and music rows, and its own variables. The
addresses are the binder's copy of the cartridge (`gevrRomWindowBytes`); a
write into a table the port copied out of it reads that table again
(`gevrRomWindowChanged`). Bond's fields (g_CurrentPlayer + N64 offset),
g_CheatActivated (0x800696A0), g_CheatPlayerTextRelated (0x80079E30), the
objectives' words (0x80075D58) and mTrack2Vol (0x80040B58) map to the port's
own variables. `port/tests/test_footsteps.py` runs every one of them on the
real cartridge: none names memory the port lacks.

Also ported from its code:

- AI 0x36 (IFICanHearAlarm in the port) shows or hides objective n: its
  difficulty flips by 4 and a flag in the high half of its status word; the
  briefing, the pause list and the mission-complete check follow it (an
  objective at difficulty 4 is hidden until shown).
- AI 0xEC toggles; 0xFB fades the fog without gas (Egypt's 120 ticks, no
  harm); 0xC4 channel -2 plays at the guard; 0xBF arms spawned guards with
  rocket launchers when Enemy Rockets is on in Cheat Options; 0xE9 swaps the
  two environments so the next switches back.
- Global AI list 0x000D runs from the cartridge (the XBLA fix).
- bheadUpdatePos uses its constant at 0x800552FC (POV height).
- Cheats: its rows' names and masks; 1/5/6 run 2x Health, 2x Armour and 10x
  Health (button codes), 8/9 (Play as Luigi/Peach) and 13 (Mushroom Mode) are
  its scripts', 16 is Extra Weapons; 7/8, 9 and 13/16 unlock as 17, 18 and 19;
  Cheat Options has fourteen rows a column from y 0x28.
- Cradle, Aztec and Egypt count as completed (its secret missions and those
  cheats are open from the start).
- A guard's grenade pin (sound 0x117); a quarter of the guards' cries half a
  semitone down; its attract cast's Mario outfits; level start keeps the
  music volume and silences it where the level row's last float is 0.

Left out on purpose:

- Switches no Mario list sets (mailbox 0x807000A0-0x807000F0): a frame
  buffer effect, a camera offset, four extra save bits, widescreen field of
  view and viewport; the AI preamble's store to 0x8070011C (never read).
- Dead bodies drawn after ACT_DEAD and not faded (chrRenderProp, chrlvTick-
  Dead): the port keeps bodies its own way (body retention, ragdolls).
- Multiplayer's random respawns (multiplayer is off in a hack campaign);
  its anti-aliasing pause option and intro skip.
- AI 0x27 and 0xF2's new code: no setup or global list uses them.

Not seen in a headset yet: Play as Luigi/Peach on the hands, Tiny Mode's
view, the Cheat Options page. On the PC (2026-11-16, `--boot-stage`, 25 s
each), all 20 missions of its first page, Temple (38) and the credits (54)
load and run their scripts: no crash, no address the port lacks.

## Online co-op (2026-11-17)

Only the host runs the level scripts; a teammate's guards are puppets. What
Mario's pokes write to the hack's memory (gun and character files, scales,
the mission folder, fog, music, its own variables) is sent as the bytes
written: the poke engine logs each written range (`gevrFootstepsTakeWrites`,
since the last send; `gevrFootstepsTouchedWrites`, since the bind), the host
sends them each tick as `NET_MSG_COOP_POKES` and with each roster (a
drop-in's snapshot), and a teammate copies them in and calls
`gevrRomWindowChanged`, so its tables follow as the host's did. The same
message carries the objectives' show/hide flags and difficulties (AI 0x36).
If nothing took the log for a while (solo play) it stops logging and the
host sends everything once.

Checked: `test_footsteps.py` copies every range written by all 12,789
commands into a fresh bind and gets the host's bytes (153 ranges, 1,295
bytes); two PC copies in a co-op Dam log the same hash of the written
memory on host and teammate.

Not shared, by design or not yet:

- The player's own fields (health, armour, outfit: 410 pokes, its power-up
  loop) act on the host's player only; a teammate's health is GoldenEye's.
- Play as, Tiny and Mushroom Mode follow the host's Cheat Options: the
  host's script decides, and its renames reach everyone.
- A teammate's body comes from GoldenEye's multiplayer character list, which
  in a hack names its own characters by number (its multiplayer character
  table, 0x80028490, is not bound).
- Not checked: whether a teammate's guns and guards that load before the
  first message arrives reload with the renamed files.

## Step 3: its missions (done)

Page 2 is not code: Temple (stage 38, mission 19 of page 1) is a hub, and
its console list (`UsetupdishZ` list 0x1001) is a two-state switch. Its first
half (flag 0x80700124 = 0) writes page 2 into the mission folder (0x8002ABE4)
with Footsteps stores, swaps the menu's `Pmainmenu1Z`/`Pmainmenu2Z` files and
sets the flag; the half after its label 02 writes page 1 back. The rows point
at its extended mission numbers (0x8004F310), chapter numbers (0x8005BA00)
and briefing names (0x80055A70), all in the binder's window, so step 2's
`gevrRomWindowChanged` -> `installFolder` already builds page 2: Aztec (28),
Egypt (32), Bob-omb Battlefield (42), Big Boo's Haunt (44) and Peach's Castle (40) as
missions 0-4, then page 1's rows from mission 5. They share save slots 0-4
with page 1, as on the cartridge. `test_footsteps.py` runs that half on the
real cartridge and checks the folder it leaves.

What was missing was its text banks. language.c's switch
(langGetLangBankIndexFromStagenum, 7F0C15E0) is widened in the cartridge
(`sltiu 0x2e` -> `0x30`, stages 9-56) and its added cases are stubs in the
data segment just before the jump table (0x8005BB00-0x8005BB50): 40 Lcat,
42 Lsho, 44 Leld, 47 Llue, 49 Lrit, 51 Lear, 52 Llee, 53 Llip, 55 Lwax,
56 Lpam; stage 21 returns the null bank. Its default no longer hangs. The
binder decodes the cartridge's own switch for every campaign
(`gevrRomLangBank`; Goldfinger and TND decode to GoldenEye's) and language.c
asks it first. `test_romcampaign.py` compares it with an independent decode
on all three hack ROMs. A message whose bank is not loaded is now skipped by
hudmsgBottomShow/hudmsgTopShow instead of crashing (langGet logs it).

Seen running (2026-11-17, Windows flat build, `--boot-stage`): 40, 42 and 44
load, show their own HUD text ("Big Koopa: ..." on Bob-omb Battlefield) and
run, no error. 45-47 are their multiplayer forms: their scripts name the solo
banks, which those stages do not load; the cartridge would read garbage, the
port logs and skips the line (multiplayer is off in a hack campaign anyway).

Left as GoldenEye's, multiplayer only (off in a hack campaign):

- Level row 37: Mario gives it stage 21 (Usetupark3); the only writes of
  stage 21 are to multiplayer tables (Temple's "Multiplayer Map 0A", Cuba's
  slot table at 0x8002B808). The binder logs it and keeps GoldenEye's row.
- Its menu code changes besides the cheats (7F0100D0-7F010588, 7F011D50):
  multiplayer stages always open, 0x50 characters instead of 0x40, weapon
  sets 14 -> 20 (mp_weapon_set, 0x80049160, table 0x80049080), its
  multiplayer character table (0x80028490).

Not checked: which of its lists play the 3 added animations (183-185; they
are bound since step 1).

Still not seen in a headset: anything of this campaign (the desk-parked
Quest ends the VR session two seconds after launch).
