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

- **Shots against HD props** (xm `xblaMeshHitTest`, `xblaMeshObjShotTest`): shots
  stop on invisible N64 hulls or pass through HD bulk (Jungle's trees). L
- **Shot-out lamps darken HD rooms** (`gelights.c` via `roomSetLightBroken`, and
  gbs build()'s unlampLit): the lamp breaks, the room stays lit. M
- **First-person HD muzzle-flash cards** (gb `fpBuildCards`): the HD flash
  triangles are dropped, so every gun fires the N64 flash. M
- **Mirrored gun lettering** (gb `beanFixLettering`): backwards text on the rocket
  launcher, RC-P90, Klobb, sniper, AR33, Phantom and D5K; VR shows both sides. M
- **Hat seating on HD heads** (xm `xblaMeshHatSeat`): caps float or hair pokes
  through (Dam guards; Karl, Dave and Pete heads). M
- **Neck seams** (gb `beanAddNeckTube`, `beanWeldSeams`, `beanSmoothNeckWeights`,
  `beanWeldNeckWeights`; xm `xblaMeshCollarUnderHead`): cracks at the nape,
  throat gaps. M
- **Backdrop as its own pass** (gbs `takeBackdrop`, `backdropPrepare`,
  `gebeanStageRenderBackdrop`): far peaks (Surface, Dam, Runway, Cradle's ring)
  are fogged by eye depth, so the haze shifts as you turn. M
- **N64 brightness match** (gbs `matchN64Brightness`): rooms darker than the N64
  look on 15 levels (Train, Depot, Caverns worst). Dab's owner's taste, level by
  level: the user's call. L
- **Hair and lenses, solid core plus blended fringe** (xm, `G_ALPHA_CORE_EXT`):
  hair strands sort wrong or show through. M
- **Sky fill under the dome** (`gebeansky.c` gebeanSkyRender's fill): black below
  the dome's lowest ring where no mesh covers it. S
- **GoldenEye's translucent water leaves the Bean room lacks** (gbs
  `fileWaterLeaves`): Egyptian's pillared-hall pool is a dry basin. S-M
- **Hidden native rooms still stop shots** (Dab's prop.c/collision.c skip
  `gebeanStageRoomHidden` rooms): shots stop in mid-air near the dam. S
- **Faces over another room's space drawn culled** (gbs `markOverlaps`): a stone
  wall across Aztec's exhaust vent. M
- **Aztec armour closet kept out** (gbs `keepOutOfRooms`, `gebeanStageRoomByPortals`,
  `gebeanStageHidesProp`): a slab and the closet box seen through other rooms. M
- **Opaque twins the release never shows** (gbs `dropTwins`): the port's
  `levelPick` draws the later twin on top, the reverse of the release; Facility's
  hazard stripes over the concrete. Unchecked on screen. S
- **Monitor pictures** (`gemonitor.c`, 50 release textures): left out on purpose
  (`gevr_bean_images.c`); monitors show the cartridge's programmes. M
- **Shattered windowed-door pane** (xm door glass toggle): the HD pane stays
  whole over the shards. S
- **Sphere-mapped prop draws without UVs** (gb `beanDrawIsSphereMapped`,
  `beanSphereUv`): Silo satellite panels black, car windows, Yale key. S
- **Prop lettering painted on black made clear** (gb `beanKeyStencil`): black
  boxes round crate stencils and container numbers. S
- **Surfaces GoldenEye draws without fog** (gbs nofog bit): Caverns' pools fade
  to fog colour. S
- **Truck dual rear wheels** (gb `beanDoubleWheels`). S
- **KF7 front sight** (gb `fpMendVertex`): leans off the bore when aimed. S
- **Clamped cards on props** (gb `beanClampCards`): dashes under Jungle's vine
  cards. S
- **Mines stick to the HD surface** (xm `xblaMeshSurfaceAlong`): mines sink into
  fatter HD walls (Facility tank). M
- **Temple crypt door repaint and end faces** (gb `beanRepaints`, gb 8925-8943). S
- **CE HD sea picture** (`gebeansky.c` gebeanSkyWaterTile): Frigate's sea at ROM
  resolution with the Community Edition. S
- **Bunker 2 stair stan link** (Dab geconvert.c `g_RomPatches`, CE tile file): an
  invisible edge on the stairs. Patching stan changes the netplay hash. S
- **Other ROM patch-table fixes** (Dab geconvert.c `g_RomPatches`): Silo armour,
  Control blast-door clip, Surface Klobbs/railing pad, Egyptian Golden Gun case
  glass. S each
- **Always-on-top props** (xm, no depth): Cuba's flagged plants. S
- **Held-gun nudge** (gb `geHeldNudge`): Moonraker and RPG-7, only with
  DetailedGuns off. S

## Partial

- **Far plane to the mesh's extent** (gbs `gebeanStageTickFar`,
  `gebeanStageFarOwn`, `gebeanStageFogFactor`): never raised past the global far;
  HD terrain cut before the fog is whole (Surface, Runway, Temple + CE, Cradle
  2-4 player MP). M
- **Alpha-test faces that never pass** (gbs `alphaTested`): alpharef not read;
  Control's yellow stair stripes drawn solid. S
- **Community Edition colour/UV fixes on retail copies** (gb 8905-9035,
  `gebeancefixes.h`): only from the player's updater zip; retail shows the old
  colours (Bunker 2 cell doors, Complex railings). M
- **Distance LOD**: Dab always draws the near model; the port switches at 4x
  GoldenEye's range, so some distant props fall back to N64. S
- **HD casing fits** (gb cartRows): ejected casings offset, shotgun shells upside
  down. S
- **Dam fog from the camera's height** (gbs `gebeanStageFog`, cam_pos.y): the port
  uses the player's height, so intro and ending shots get low fog. S
- **Grenade or mine left in hand after a throw** (gb toggled lists): same
  mechanism as Dab's; needs a headset check. S

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
(bbd5ff40).

Not needed in the port: weldVertices, markDecalLifts and markDecalCorners and
the far-off triangle moves (Dab's s16 room vertices; the port's are floats);
the CCTV lens test (the port's shots use N64 geometry); Dab's tile and texture
table readers and native water (GoldenEye does its own).
