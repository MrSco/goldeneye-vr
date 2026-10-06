# GoldenEye X guns and reloads

Engineering notes for the GE-X support introduced in PR #117 (2026-10-04).
For installation, ROM preparation, reload controls and troubleshooting, start
with [GoldenEye X setup](gex-setup.md). Current support covers KF7, both PP7s, DD44, Klobb, ZMG, D5K variants,
Phantom, AR33 and VR arms, read from the player's own patched GE-X 6a ROM; the weapon list below
is a format reference, not a list of implemented replacements.
Nothing from GE-X is committed or shipped; no
licence covers its assets. Credits: the GoldenEye X team (Wreck, Carnivorous
for the weapon animations, SubDrag and others) and Dab's Mod
(DabDavis/perfect-dark-dabs-mod), whose import notes this follows.

The current batch adds seven guns with separate grip, reload-grab, held-magazine,
and insertion-target fits. Both-hand support rotation is adjustable for all
supported guns. See the [roadmap](gex-weapon-roadmap.md#detachable-magazine-batch-and-saved-fits-2026-10-06)
for reviewed bindings, reload timing, fit keys and the next-model handoff.
The new batch awaits headset fitting and acceptance.

## The ROM

- GE-X 6a (`GE-X_6a_01-19-25.zip` from n64vault) is an xdelta (VCDIFF, no
  secondary compression, four 8 MiB windows with Adler-32) against Perfect
  Dark NTSC 1.1 (`pd.z64`, MD5 e03b088b6ac9e0080440efed07c1e40f).
  `python tools/gex/vcdiff_apply.py pd.z64 GE-X_6a_01-19-25.xdelta gex.z64` builds
  it (MD5 640923b68b9281044ac1b518612e979b, header "GoldenEye X", NX7E).
- It keeps PD's layout: the data segment at ROM 0x39850 (1173-compressed),
  the file table at 0x28080 inside it, 2013 files under PD's names
  (`tools/gex/pdrom.py`). Model files are 1173-compressed.
- GE-X's guns are PD model files in PD's weapon slots. The classic-gun files
  PD shipped as four-matrix props now hold full first-person rigs: GE-X
  `Gak47Z` has 41 matrices and 51 nodes (as PD's K7 Avenger), `GwppkZ` 43
  (as the Falcon 2). Node types used: 0x02 position, 0x04 gun display list,
  0x12 toggle, 0x16 star gunfire, 0x11 (`tools/gex/pdmodel.py`).

- Textures moved: PD's list (ROM 0x1ff7ca0) and data (0x1d65f40) are blank
  in GE-X. Its list is at ROM 0x1e77400 (3504 8-byte entries, the low 24 bits
  of the first word an offset into the data, the second word zero) and its
  data starts at 0x1b449a2, only 2-byte aligned (found by texture 0's bytes,
  which GE-X kept). Model texture configs name these numbers. Both of PD's
  codecs occur (first byte: bit 7 LOD data, bit 6 zlib, low 6 the LOD
  count): the KF7 has zlib and non-zlib textures, so PD's texdecompress.c is
  needed whole.

## The guns (GE-X slot: model file)

knife 2 GknifeZ; PP7 3 and silenced PP7 4 GwppkZ; DD44 5 Gtt33Z; Klobb 6
GskorpionZ; KF7 7 Gak47Z; ZMG 8 GuziZ; D5K 9 Gmp5kZ, silenced D5K 10
Gcmp150Z; Phantom 11 GcycloneZ; AR33 12 Gm16Z; RC-P90 13 Gfnp90Z; shotgun 14
GshotgunZ, automatic shotgun 15 Grcp120Z; sniper rifle 16 GsniperrifleZ;
Cougar 17 GmaianpistolZ; Golden Gun 18 Gleegun1Z; Moonraker laser 21
GdysuperdragonZ; grenade launcher 23 GdydevastatorZ; rocket launcher 24
GdyrocketZ; grenade 26; timed, proximity and remote mines 27-29 (Dab's Mod
port/src/modborrow.c).

## Reloads

PD's: a weapon's ammo row names a gun-command script (8-byte commands:
kind, flag, keyframe, word), which plays a row of PD's animation table and
shows or hides model parts, plays sounds and moves the ammo on its frames.
Addresses are PD NTSC 1.1's, which GE-X keeps: data segment at 0x80059fe0,
g_Weapons at 0x8006ff18 (0x50-byte rows: ammo at +0x1c, the ammo's script at
+0xc), GE-X's animations segment at ROM 0x157810 (moved from PD's 0x1a15c0; found
by animation 1's bytes, which GE-X kept) with its table (a count, then
12-byte rows: frames, bytes per frame, data offset, header length, frame
length, flags) at ROM 0x7cd1a0, 1207 rows. GE-X rewrote 86 of them.
`tools/gex/gexguns.py gex.z64 7` prints a gun's scripts.

The KF7 (slot 7, def 0x8006ea88, clip 30) is the first gun:

| frame | command |
|---|---|
| 0 | play animation 1018 (90 frames) |
| 3 | sound 0x53 |
| 18 | hide part 0x2a, show part 0x28 (the magazine swap) |
| 43 | sound speed 0x3b0, sound 0x47d |
| 49 | sound 0x5c5 |
| 50 | ammo moves; show part 0x2a, hide part 0x28 |

Its fire animation is 1017 (38 frames). The PP7 reloads with 1047, or 1009
with the other hand busy, and pistol-whips with 1010.

Notes from Dab's Mod: a play-animation keyframe word is signed (negative
plays backwards); sounds are GE-X's bank (bit 15 set: an index into the
sound config table at 0x8005dde4); the KF7's magazine texture 0x3f6 is IA16
with the intensity in the high byte; a GE-X 5a model gave a vertex count one
past its array; PD's animation decoder hung when frame data sat right after
its header, so keep them in separate buffers.

## Hands

PD draws a separate hand model (per outfit, `Ghand_jowetsuitZ` for GE-X's
Bond) with the gun model's matrices and animation: every first-person gun
carries the same hand skeleton. Placement comes from the weapon's position
fields (KF7 13, -23, -27; PP7 8, -19, -26).

## In this port (2026-10-06)

- tools/gex reads the ROM (file table, models, scripts, animation rows).
- port/src/gevr_gex.c loads data/gex.z64; gevr_gexmodel.c rebuilds a PD
  gun model as a GoldenEye file; gevr_pdanim.c is PD's animation reader.
  Supported GE-X weapon textures are paired with GoldenEye's own ids where the
  pixels match, so the HD packs apply (gevr_gexweapon.c).
- Supported guns (launcher MODS "Its guns (WIP)", ini GexGuns, off by
  default; also the watch's VR settings, from the next weapon drawn). On
  the screen it fires and reloads with GE-X's animations; its hands are
  the hand model on the gun's skeleton, as PD draws them.
- In the headset there is no reload animation (hand reload): the off hand
  pulls the magazine (or B/Y drops it; it falls), takes one at the belt
  and pushes it into the well. A magazine keeps its rounds while out;
  dropped, they go back to the reserve; one from the belt is full. Either
  gun can take one at the belt by itself. Left GE-X guns are mirrored.
- Gun fit keeps GE-X's own gun, grip and scope fits, and adds modes (X):
  reload (where the magazine is taken, the belt), off hand (its palm;
  holding the right grip, the watch) and, holding with both hands, where
  GE-X's left hand holds the gun (ini GexForeHold), which is also where the
  two-handed hold is taken; the nearer of it and the magazine wins for KF7.
  PP7 shares separate gun/support/grab fits across its two variants. A fresh
  underside grab selects its magazine; ambiguous overlap selects support,
  and grip ownership persists until release.
  The PP7 support hand stays fixed relative to the pistol instead of rotating
  with the off controller. X also offers a held-magazine fit: sticks move only
  the magazine within the fingers, with separate PP7/KF7 values and the fitted
  top used for physical seating.
  Held-magazine fit previews the magazine without requiring physical reload.
  PP7's support fit includes pitch/yaw/roll about the palm, adjusted with the
  gun-hand grip held. Reload grab-point fitting recognizes the GE-X pistol
  magazine too.
  Magazine-well fit moves only the insertion entrance, independently of the
  grab/held points. PP7's default entrance is its handle bottom; KF7 keeps its
  existing target. The off trigger sets the entrance at the preview magazine's
  tip. An already rendered GE-X magazine hand consumes the off-hand render slot
  so the legacy arm is not added over it.
- Arms (MODS "Its arms, wearing the watch (VR, WIP)", ini GexArms):
  GE-X's own arms are every arm in the headset, as GE-X made them, and
  GoldenEye's watch is drawn at its own size or larger (ini GexWatch) at
  the end of the left sleeve - never smaller, for the status on its face.
  The pause's own watch arm (bondview2.c bondviewRenderWatch) keeps its
  watch and GE-X's arm is drawn under it. The rejected alternative, the
  watch arm's sleeve on GE-X's hands, is parked on claude/gex-watch-sleeves.
- `GexWeaponDef` centralizes models, parts, animations, render matrix indices,
  texture mappings, muzzle origins and magazine alignment. The PP7 installed
  magazine is matrix 38 / animation joint 41, held is matrix 42 / joint 40;
  its held mesh needs a separate rigid transform and seating point.
- tools/gex/gex_extract.py reports rigs and scripts; gex_texmatch.py compares
  decoded pixels with both compression paths. Neither exports ROM assets.
- Next stages and acceptance gates: [weapon roadmap](gex-weapon-roadmap.md).
