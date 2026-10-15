# Bean parity backlog

2026-10-14. Two audits against GoldenEye's own renderer and Dab's Mod
(`other_projects/dabs-mod`, `port/src/gebean*.c`, `xblamesh.c`,
`CLAUDE-notes/ge-bean.md`, `xbla.md`). One line an item; delete a line when it
ships. References are Dab's unless marked GE (the decomp).

## Being ported now (PR 180)

- Room culling for GoldenEye's own cameras outside the level
  (`gebeanStageTickCamera`, `bgRenderRoomOpaque`): opening shots, the swirl and
  cutscene cameras stand inside rock and under floors, and two-sided HD rooms
  block the view (user, 2026-10-14, Facility's opening shots).

## Done in PR 180

- Corpse fade (77bf3eb), destroyed-prop scorch and crumple (772b6db, 415e8a0).
- Node mode (113e7c1, 51a8c7b): Bean model lists draw under the game's node
  state - room shade, prop darkening and pickup fade, first-person tint, fades.
  `files/gevr_bean_nodemode.txt` 0 turns it off; then a chr fading at range
  (PropType 8) skins on the CPU (77bf3eb's G_GEVR_BEAN_FADE path).
- Boot sequence: the release's Rare logo (7aa5b3e) and its line in place of
  the Nintendo logo screen (879cbaa). The legal page was already the release's;
  the GoldenEye logo stays the cartridge's, as in Dab's Mod.
- Bean HD fill (Mods, `BeanFillTextures`): the pictures Bean leaves at the
  cartridge's, the 68x44 stage pictures among them, from the HD + AI or HD pack.

## Characters, props and guns

- Tinted glass and window-door opacity by distance (GE `glassCalculateOpacity`,
  PropType 9 prim alpha; DAB fade span) - the game shuts the portal behind glass
  that still draws clear. DAB also keeps windows clear: `gebeanGlassClear`.
- Window-door glass shattering (GE `sub_GAME_7F04DD68` Switches[1]); CCTV lens
  break (GE `objBreakCCTVGlass`): the switched lists are folded into one mesh.
- Hair under peaked hats (GE `chrTick` head Switches[1]); hat seating on HD heads
  (`xblaMeshHatSeat`).
- Hair and lenses: solid core plus blended fringe (`G_ALPHA_CORE_EXT`, xbla.md
  "A character's hair and lenses").
- Rocket launcher sight screen and taser arc (GE gun Switches[16] monitor
  microcode; `beanLiveScreen` only honours props' 0-3).
- Grenade/timed mine left in hand after a throw (GE `field_87E` toggles 14/15):
  unverified.
- Distance LOD: native switch runs at 4x range and non-primary lists are blanked;
  DAB always draws the near model.
- Switch-controlled body parts drawn by a sibling list (`sharedG`).
- Third-person muzzle flash: HD painted flash dropped; first-person flash cards
  (`fpBuildCards`), mirrored gun lettering (`beanFixLettering`), KF7 sight
  (`fpMendVertex`).
- Always-on-top props (`PROPFLAG2_00010000`, zbuffer off).
- Shots and surfaces against HD triangles (`xblaMeshHitTest`, `ObjShotTest`).
- Neck seams (`beanAddNeckTube`, `beanWeldSeams`, `beanSmoothNeckWeights`).
- Held gun nudge for the Moonraker and rocket launcher (`geHeldNudge`).
- Truck dual rear wheels (`beanDoubleWheels`).
- Community Edition vertex-colour/UV fixes on retail copies (`gebeancefixes.h`,
  `beanVertexColourFixes`, `beanVertexUvFixes`, `beanRepaints`); CE fog distances
  always (`beanFogsCe`).
- Sphere-mapped prop draws without UVs (`beanDrawIsSphereMapped`, `beanSphereUv`).
- Lettering painted on black made clear (`beanKeyStencil`).
- Cut-out edge bleed and clamped cards (`beanBleedCutout`, `beanClampCards`).
- Tree UVs on instanced pines (`beanTreeUvs`).
- Monitor pictures (50 `texture/monitors`).

## Levels (DAB `gebeanstage.c` build passes)

- Release sky only on Surface, Dam and Runway (`gebeansky.c` `releaseSkyNames`);
  we put the sunset dome on Caverns, Egyptian, Frigate, Temple, Complex, the
  Bunkers and Surface 2. Sky cap fade and fill (`skyCapFade`).
- Hidden native rooms (c94a8ff: Dam's intro boulder and its N64 water) still
  stop shots; Dab's `prop.c` and `collision.c` skip `gebeanStageRoomHidden` rooms.
- Keep GoldenEye's translucent room leaves the Bean room lacks, water first
  (`fileWaterLeaves`): `gevr_bean_world.c` sets replaced before checking.
- Far plane to the mesh's extent (`gebeanStageTickFar`); backdrop handling
  (`takeBackdrop`, `gebeanStageRenderBackdrop`).
- Level mask/cut/alpha-test pictures (`beanLevelMaskTexture`,
  `beanLevelCutTexture`, `alphaTested`).
- Coincident faces (`dropTwins`, `markBacked`, `markFights`, `markOverlaps`).
- Doorway gaps, reveals and slots (`closeDoorGaps`, `fillDoorReveals`,
  `fillDoorSlots`).
- Shot-out lamps darken HD rooms (`gelights.c` `roomSetLightBroken`).
- Unlit black faces relit (`paintUnlit`); cast-shadow decals see-through
  (`markShadows`); hull reflection under the sea (`markUnderSea`); surfaces
  GoldenEye draws without fog (`nofog`); N64 brightness match
  (`matchN64Brightness`, Dab's owner's taste).
