# Bean repair handoff — 2026-10-11

PR https://github.com/MrSco/goldeneye-vr/pull/170 remains draft. Branch
`codex/pr170-polish` pushes to `cursor/port-bean-meshes-f721`. This is ongoing
repair work, not a release-ready build. See project-bean-runtime-audit.md for
confirmed causes and the current fixes. Do not infer visual correctness from
registration or sanitizer checks.

User asks for full faithful mapping using DAB, launcher cleanup, GE-X
first-person gear priority, and a release build only after correctness. They
also requested preserving the watch/controller Bean toggle and multiplayer
hit health/armour gauges independent of the three damage comfort toggles;
these earlier changes must remain. Never install an APK without an explicit
installation request. Set JAVA_HOME=C:/Program Files/Java/jdk-20 before EVERY
Gradle invocation. The user asks to push notes for continuation when account
usage has only 2% remaining. Do not consume reset credits automatically.

## Evidence and references

- Report a2167162: old build 0bf6179 crashed in native hit-list walking. Exact
  symbols and withdrawn APK in build/releases/v0.4.20/0bf6179. Corrected in
  c220561 by retaining native collision data and using a render registry.
- Report 22ffdb80: c220561 tested without that crash but badly broken visuals.
  Decoded log: build/bean-crash/gevr-22ffdb80-decoded.log. Both supplied .txt
  reports in the user's Temp folder are base64; decode before inspecting.
- Screenshot: C:/Users/Occor/AppData/Roaming/odh/device-media/2G0YC1ZF8Q0JQ6/
  com.gevr.port-20261011-111954.jpg. Nearly absent Dam, overlapping hands,
  native-resolution UI; sky and some props visible.
- Complete pinned DAB reference in ignored build/bean-reference/upstream,
  SHA adef1dc2d4eca1a5c453a7f2bf03a3b6cdad99b8. Use this instead of the stale
  other_projects/dabs-mod checkout. MIT attribution is in port/licenses.
- Native ROM: C:/Users/Occor/Documents/other_projects/goldeneye-vr/
  007 - GoldenEye.z64. Local extracted assets: same project's added-content/
  xbla/files. These are local user files; never commit them.
- Signed diagnostic c220561 (already tested): build/diagnostics/bean-c220561/
  GoldenEye-VR-Bean-diagnostic-c220561.apk. Do not reoffer this as a new fix.

## Headset round 2026-10-11 evening (build cb33888+, Dam)

Reported: Nintendo logo translucent, fonts upside down, file-select cursor
low-res, watch see-through, guns and guards' hands set back, holes in the
map, stutter (52 fps of 120; DRAW/TICK 16 ms, GPU 2.6 ms).

- Done: fonts the right way up (1634b10); 121 HUD/menu/effect images
  repainted from files/texture with pack 2026.10.11.3 (56ecb44, b23acea);
  guns in GoldenEye's frame, glove on the hand list, pickups un-mirrored
  (2334405); props and guns two-sided (ca729c0); fast3d presentation clip
  once per vertex, 1024-triangle batches, hashed Bean lookups, new
  VTX/TRI1/CLIP/ROOM/PRES stats (cf59657). Tests: test_bean_images.py,
  test_bean_fonts.py shape check, mapping-test gun/orientation checks.
- Then: DAB's translucency and decal rules for the logo, the wrist watch
  and the water (1d02ef1, test_bean_material.py); rooms and sky as static
  GPU meshes (7f75f48; files/gevr_nomesh.txt turns them off; offline
  bit-identical to the CPU path, 10 ms -> 0.13 ms of fast3d per frame);
  rooms drawn by their Bean bounds (915f6c8). All need the headset.
  Next: phase 2, GPU skinning of characters (xblaMeshGpuPalette).
- First diagnosis (now fixed): the logo (blended alpha 255 demoted to 254, so
  drawn as glass), the watch (markDecals buckets flag 521 suitlfhand
  triangles with nothing under them), the water (material alpha 0 taken
  on level triangles). A patch for gevr_bean_load.c porting DAB's rules
  was written but its edit was refused by the permission system; it waits
  in the session's bean-material-agent scratch for the user's decision.
- Holes: Bean triangles reach far outside GoldenEye's room boxes (rooms
  3, 4, 7 entirely), and rooms are drawn only through the native portal
  walk and box test. DAB draws every room by distance. Waits for:
- Performance: the cost is fast3d's per-triangle CPU path (110K Bean
  triangles on Dam vs 12K native). Next is DAB's static GPU mesh path
  (G_MESH_EXT: VBO/IBO per room built once, a mesh vertex shader with MP,
  eye shear, fog and lighting, redraw/scope recording), then drawing Bean
  rooms by their own bounds.

## What still needs work

1. Validate the next candidate's Dam placement, full limbs, guard neck seams,
   single GE-X arms/watch, menu material orientation and frame times. The root
   scale correction has actual cartridge geometry checks; live portal/scissor,
   fog/water/overlap/outside-camera fidelity are still unfinished.
2. Reuse more DAB code: head fitting, seam pinning, collar/hood/tube routing,
   per-bone fitting and palette handling, full shader/sampler/mask/UV2 logic.
   Current emitHead is an approximation. Do not add arbitrary centroid cuts
   and call them a faithful mapping.
3. Menu textures (folder done 2026-10-11): `port/src/gevr_bean_folder.c` ports
   DAB's gefolder.c repaint of PwalletbondZ's 84 textures, bound by native
   address (gevr_bean_tex.c repaint registry, gfx_pc.cpp import_texture).
   `port/tests/test_bean_folder.py ROM FILES` checks it against the ROM's own
   pictures. Still open below: fonts, portraits, stage pictures.
   Packaged Bean PNGs number only 122. DAB's gefolder.c repaints
   native walletbond textures using new/prop/walletbond (already packaged),
   using folderRows plus briefPicture/slidePicture, crop/padding, masks and
   paper/grey matching. PwalletbondZ was excluded from full mesh mapping,
   which explains much native-resolution folder UI. Repaint its native
   textures independently of replacing its mesh. Font assets are misc/alps3
   and misc/doc0; DAB gebeanFontOpen and gefolder.c parse/crop glyphs. Current
   archive/extractor do NOT include misc or texture/*; extending fonts/HUD
   needs packaging and downloader changes too. Do not borrow HD/AI textures
   while Bean is active. The CAFF decode only handles the first animated frame.
4. Nintendo chrome: new/prop/nintendologo contains a real magenta placeholder,
   currently painted grey. DAB's intro deliberately skips Nintendo/Rare boot
   logos, so there is no DAB chrome implementation to reuse for that screen.
   Restore the material/reflection behavior; grey fill alone is not complete.
5. Performance: c220561 report has ~53–61 fps at 120 Hz. Secondary-pass CPU
   posing has been removed, but no headset performance improvement is proven.
   Room importer scans all triangles twice per room; emitter expands vertices
   and draws five triangles per vertex batch; swap lookup is a linear scan.
   DAB uses richer indexed/GPU mesh rendering and HD room caching. Profile
   before claiming a performance fix.
6. GE-X texture namespace: user's other agent is handling a hack-ROM PR.
   GE-X reserves 3100–4095, overlaps Goldfinger's table through 3914;
   converter fields mask 0xfff so simply raising IDs wraps them. Do not
   duplicate that agent's namespace changes or message it without user
   authorization. This is separate from Bean CAFF pointer tiles.

## Alternative engine question

User linked Alex-LeTux/perfect_dark_VR and asked whether using Perfect Dark
would be better. It already has standalone Quest/PCVR and 6DoF, latest checked
release v2.0. Source snapshot b39b12bbea8bf428a9cf71c08e4a522625451a7a saved in
build/bean-reference/pdvr. It has no DAB converter/Bean renderer. Combining its
VR with DAB's GE Plus conversion and renderer is a credible alternative, but
requires integration and campaign/feature acceptance. Earlier claim that all
VR would need rebuilding was overstated and was corrected to the user. User
has NOT requested replacing this PR's engine or creating a new task.

## Verification and build workflow

- WSL bash: BEAN_SANITIZE=1 python3 port/tests/test_bean.py
- WSL bash: BEAN_SANITIZE=1 python3 port/tests/test_bean_mapping.py ROM ASSETS
  (WSL paths /mnt/c/...). Checks 457 registrations and all 26 native stages,
  actual original mesh placement and two real native character joint layouts.
- WSL bash: python3 port/tests/test_launcher_mods.py (real production ImGui).
- Windows Python: port/tests/test_gex_weapons.py; WSL plain build for that
  test has unrelated header setup problems.
- Gradle :app:assembleRelease :app:testReleaseUnitTest, JAVA_HOME as above.
- After a clean commit, rebuild for accurate embedded commit/dirty metadata.
  tools/perf/archive_apk.py NAME signs and preserves exact packaged ELF symbols
  (NAME alphanumerics/hyphens/underscore only). It does not build or install.
- Keep artifacts diagnostic until headset acceptance. No APK installed or
  published by this task during these repair passes. Never run perf boot or
  profile helpers casually: they may install APKs and clear logs.

Engine files have mixed line endings and * -text attributes. Preserve untouched
lines; do not normalize entire files via read_text/write_text. Most Bean files
also contain mixed endings. Use byte-preserving edits and inspect diff sizes.
