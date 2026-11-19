# Bean parity backlog

What Dab's Mod (`other_projects/dabs-mod`) does for Project Bean that the port
does not yet. Rewritten 2026-11-19 from a code audit of every Dab Bean pass and
of the 2026-10-14 list (the October list had gone stale: shipped items were never
deleted and some never-done ones looked handled). Each line was checked by Dab's
name and by behaviour. Nothing here was checked on a headset. One line an item;
delete a line when it ships. Sorted by what a player sees, most first.

References: gb = `port/src/gebean.c`, gbs = `port/src/gebeanstage.c`,
xm = `port/src/xblamesh.c`, all Dab's. Size S/M/L.

## Missing

- **Shots against HD bodies** (xm `xblaMeshHitTest` from Dab's chr hit walk): guards
  are still shot on the N64 body, so HD hair, shoulders and coats are hit only
  where the N64 body is. Props are done; bodies need the skinned pose. M
- **Throat gaps** (gb `beanAddNeckTube`; xm `xblaMeshCollarUnderHead`,
  `xblaMeshHeadTubeOn`): a head cut off a high collar on a body cut lower
  (Bond's tuxedo head on the Russian guard) leaves the throat open. M
- **Backdrop as its own pass** (gbs `takeBackdrop`, `backdropPrepare`,
  `gebeanStageRenderBackdrop`): far peaks (Surface, Dam, Runway, Cradle's ring)
  are fogged by eye depth, so the haze shifts as you turn. M
- **Hair and lenses, solid core plus blended fringe** (xm, `G_ALPHA_CORE_EXT`):
  hair strands sort wrong or show through. M
- **Faces over another room's space drawn culled** (gbs `markOverlaps`): a stone
  wall across Aztec's exhaust vent. M
- **Aztec armour closet kept out** (gbs `keepOutOfRooms`, `gebeanStageRoomByPortals`,
  `gebeanStageHidesProp`): a slab and the closet box seen through other rooms. M
- **Mines stick to the HD surface** (xm `xblaMeshSurfaceAlong`): mines sink into
  fatter HD walls (Facility tank). M
- **CE HD sea picture** (`gebeansky.c` gebeanSkyWaterTile): Frigate's sea at ROM
  resolution with the Community Edition. S
- **Bunker 2 stair stan link** (Dab geconvert.c `g_RomPatches`, CE tile file): an
  invisible edge on the stairs. Patching stan changes the netplay hash. S
- **Other ROM patch-table fixes** (Dab geconvert.c `g_RomPatches`): Silo armour,
  Control blast-door clip, Surface Klobbs/railing pad, Egyptian Golden Gun case
  glass. S each
- **Always-on-top props** (xm, no depth): Cuba's flagged plants. Conflicts with the
  hands' rule in `gfx_bean_node_state()` (a node without depth keeps the list's). S
- **Held-gun nudge** (gb `geHeldNudge`): Moonraker and RPG-7, only with
  DetailedGuns off. S

## Partial

- **Far plane to the mesh's extent** (gbs `gebeanStageTickFar`,
  `gebeanStageFarOwn`, `gebeanStageFogFactor`): never raised past the global far;
  HD terrain cut before the fog is whole (Surface, Runway, Temple + CE, Cradle
  2-4 player MP). M
- **Community Edition colour/UV fixes on retail copies** (gb 8905-9035,
  `gebeancefixes.h`): only from the player's updater zip; retail shows the old
  colours (Complex railings; Bunker 2's cell doors are mended built in). M
- **HD casing fits** (gb cartRows): ejected casings offset, shotgun shells upside
  down. S
- **Dam fog from the camera's height** (gbs `gebeanStageFog`, cam_pos.y): the port
  uses the player's height, so intro and ending shots get low fog. The intro camera is
  the player's `pos` while `cameramode` is 1 (1160 down to 68 on Dam's), but it ends far
  from the player's 757; check the space on screen before using it. S
- **Grenade or mine left in hand after a throw** (gb toggled lists): same
  mechanism as Dab's; needs a headset check. S

## Declined (the user's call)

- N64 brightness match (gbs `matchN64Brightness`): the user sees no brightness problem (2026-11-19).

## Not Dab fixes (port-only gaps, no Dab code to port)

- Rocket-launcher sight screen and taser arc: Dab too shows live screens on
  parts 0-3 only.

## Shipped

Room culling for GoldenEye's own cameras; corpse fade, scorch and crumple; node
mode; boot sequence; Bean HD fill; paintUnlit; plain white decals and faces;
level cut-out clamps; doorway gaps, reveals and slots; markBacked and markFights
(f9ad4446); markDecals; markShadows; markWaterPictures and the water slide; room
dealing; hidden rooms not drawn; release sky table and cap fade; release and CE
fog; scrolling pictures; tinted glass; third-person muzzle flash; cut-out edge
bleed; tree UVs; vertex drops; markUnderSea, Frigate's hull reflection
(bbd5ff40); the monitors' pictures (DAB gemonitor.c); shots against HD props (DAB xblaMeshObjShotTest, 31754136); shot-out lamps darken HD rooms (DAB gelights.c, not yet seen on screen); first-person HD muzzle-flash cards (DAB fpBuildCards); mirrored gun lettering (DAB beanFixLettering); hat seating on HD heads (DAB xblaMeshHatSeat); seam weld and neck weights on HD bodies and heads; the sky colour under the dome (DAB gebeanSkyRender's fill); hidden native rooms stop no shot (DAB shotGeRooms); GoldenEye's water leaves in rooms Bean leaves dry (DAB fileWaterLeaves; Egyptian's pool); lettering painted on black made clear (DAB beanKeyStencil); sphere-mapped prop draws without UVs get the map's lookup (DAB beanSphereUv; not yet seen on screen); opaque twins the release never shows left out (DAB dropTwins); a shot-out door pane goes with GoldenEye's glass (DAB xblamesh fade toggle; not yet seen on screen); surfaces GoldenEye draws without fog drawn without it (DAB nofog); the truck's dual rear wheels (DAB beanDoubleWheels); the KF7's front sight on the bore (DAB fpMendVertex); props' cut-out cards sampled clamped (DAB beanClampCards); the crypt block's repaint and end faces, the cell doors' and table's colours (DAB beanRepaints, beanVertexColourFixes); alpha-tested level faces that never pass left out (DAB alphaTested); a model Bean draws takes its near detail level (DAB modelDistanceIsFullDetail).

Not needed in the port: weldVertices, markDecalLifts and markDecalCorners and
the far-off triangle moves (Dab's s16 room vertices; the port's are floats);
the CCTV lens test (the port's shots use N64 geometry); Dab's tile and texture
table readers and native water (GoldenEye does its own).
