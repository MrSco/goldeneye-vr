# GoldenEye VR — Quest Port Handoff

> **Note on `artifacts/`:** build and boot logs referenced throughout this document live in the original working folder `GEVR-OpenGLES/`, not in this repository. They are debug evidence, not source.

**Project:** Native standalone Meta Quest (Horizon OS / Android `arm64-v8a`) port of GoldenEye 007 VR (`MrSco/goldeneye-vr`), powered by OpenGL ES 3.2, Meta OpenXR Mobile SDK, and SDL2.

---

**For current state, read [STATUS.md](STATUS.md), not this file.** This is the
engineering log: what was tried, what it turned out to be, and why. Read §11
(most recent) first, then §10/§9/§8/§7.

> This document grew session by session and is written newest-last.
> Sections §1-§10 are a working log from 2026-09-19/20, kept for the
> reasoning rather than as a statement of current state: several of their
> open items have since been closed, and they cite `docs/` and `packaging/`
> files that belong to the GEVR PC project and are not in this repository.
> Where §11 contradicts an earlier section, §11 wins.

## 1. Executive Status (End of Session 3: 2026-09-19)

The engine boots natively on standalone Meta Quest hardware without a PC, loads the user's cartridge (`ge.z64`), swaps animation tables/fonts/text banks, pumps frames lazily via the frame pump, converts models dynamically into host memory, and executes the game loop.

### Hardware Boot Milestones Confirmed on Device
1. **Full Boot Pipeline:** `sysInit` → `fsInit` → `configInit` → `videoInit` (GLES 3.2 / `GL_OVR_multiview2` single-pass stereo) → `audioInit` → `romdataInit`.
2. **Cartridge ROM Binding:** 706 of 727 cartridge files bound from `ge.z64` (remaining 21 are PAL-only assets not present in US ROM).
3. **Animation Table Swap:** `gevrRomSwapAnimationData` successfully byte-swaps 173 animation headers and 173 descriptor blocks (59,360 bytes, 0 bad).
4. **Text & Fonts:** 7 text banks (`LgunE`, `LtitleE`, `LmpmenuE`, `LpropobjE`, `LmpweaponsE`, `LoptionsE`, `LmiscE`) and fonts (Bank Gothic, Zurich Bold) decoded, byte-swapped, and relocated.
5. **Frame Pump:** `port/src/gevr_engine_shim.c` lazily drives `bossMainloop` without busy-spinning, synchronizing with OpenXR `xrWaitFrame`.
6. **Legal Screen:** `model PlegalpageZ: 9232 -> 9840 bytes, 14 blocks (3 nodes, 1 display list, 5 data blocks)` converts cleanly and renders text to screen.
7. **Nintendo 3D Logo:** `model PnintendologoZ: 31632 -> 39808 bytes, 132 blocks (42 nodes, 23 display lists, 1 data block)` loads through the model converter and **renders in 3D with continuous rotation in stereoscopic VR for ~5 seconds on the headset**.

### Current Build State
- **Clean Build:** Gradle assembleDebug builds successfully in 5 seconds (`BUILD SUCCESSFUL in 5s`).
- **Artifact:** `android/app/build/outputs/apk/debug/app-debug.apk` is compiled and ready for immediate deployment and hardware verification.

---

## 2. Diagnoses & Fixes Applied in Session 3

Following the Nintendo 3D logo screen, the engine crashed during the transition to the Rareware logo. All root causes have been diagnosed, repaired, and compiled.

### A. Intro Matrix Buffer Corruption (Rareware Logo Crash)
- **Crash Site:** `src/game/title.c:350` (`load_display_rare_logo+648` → `guRotate+84` → `guMtxF2L+200`). Logcat tombstone: `SIGSEGV SEGV_MAPERR fault addr 0x0000000000000001`.
- **Root Cause:** `alloc_intro_matrices()` allocated matrix pointers (`matrixBufferRareLogo0..2`, `matrixBufferGunbarrel0..1`, `matrixBufferIntroBackdrop`, `matrixBufferIntroBond`) via `mempAlloc(..., MEMPOOL_STAGE)`. During screen transitions between the Nintendo logo and Rareware logo, the stage memory pool is reset/re-initialized, leaving these pointers dangling or overwritten with `0x1`.
- **Fix:** Backed all intro matrix buffers with permanent static arrays:
  ```c
  /* src/game/title.c */
  Mtx s_matrixBufferRareLogo0[2];
  Mtx s_matrixBufferGunbarrel0[1];
  Mtx s_matrixBufferRareLogo1[2];
  Mtx s_matrixBufferRareLogo2[2];
  Mtx s_matrixBufferGunbarrel1[2];
  Mtx s_matrixBufferIntroBackdrop[2];
  Mtx s_matrixBufferIntroBond[2];
  ```
  Updated `src/game/initintromatrices.c` to assign these static buffers directly rather than allocating from `MEMPOOL_STAGE`.

### B. Gun-Barrel Segment (`GEVR_SEG_GUNBARREL`) Missing & Stub Dereference
- **Root Cause:** The gun-barrel intro decompresses backdrop data via `sub_GAME_7F008DE4`, which copies from cartridge linker symbol `unknown2` to `unknown2_end`. In the initial port layer, `unknown2` was a stub initialized to `NULL` in `port/src/gevr_engine_shim.c`, which would cause an immediate null-pointer dereference upon reaching the gun-barrel sequence.
- **Cartridge Analysis:** Analysis of `assets/romfiles2.s` revealed `unknown2` incbins `assets/ge007.u.2A4D50.usedby7F008DE4.bin`.
  - ROM Offset: `0x002A4D50` (immediately following Rareware logo at `0x29E560` + `0x67F0`).
  - Size: `107,904` bytes (`0x1A580`).
- **Fix:**
  1. Added `GEVR_SEG_GUNBARREL` to `enum` in `port/include/gevr_rom_segments.h`.
  2. Registered `{ 0x002A4D50, 107904, 0 }` in `port/src/gevr_rom_manifest.c`.
  3. Bound `unknown2 = gevrSegPtr(GEVR_SEG_GUNBARREL);` and `unknown2_end = (u8 *)unknown2 + gevrSegSize(GEVR_SEG_GUNBARREL);` in `port/src/gevr_romload.c`.
  4. Removed the null stubs from `gevr_engine_shim.c`.

### C. 64-Bit Pointer Width Integrity & KSEG0 Bias Removal
- **Root Cause:** In `src/game/title.h` and `title.c`, pointers were declared as 32-bit types:
  - `barrelDisplayListPtr`, `dword_CODE_bss_80069588`, and `dword_CODE_bss_8006958C` were declared as `s32`, causing 64-bit truncation on ARM64.
  - `title.c:498` had `barrelDisplayListPtr = gfxBuffer + 0x80000000;`, which baked in N64 physical-to-virtual KSEG0 offset arithmetic onto host 64-bit heap pointers.
- **Fix:**
  1. Changed declarations in `src/game/title.h` and `title.c` to `u8 *`.
  2. Removed `+ 0x80000000` from `barrelDisplayListPtr = gfxBuffer;`.
  3. Cast `barrelDisplayListPtr` to `(Vtx *)` in `sub_GAME_7F01BFF8`.

### D. `OS_K0_TO_PHYSICAL` Macro Sanitization
- **Root Cause:** `include/PR/os.h` defined `OS_K0_TO_PHYSICAL(x)` as `(u32)(((char *)(x)-0x80000000))`, which subtracted `0x80000000` and truncated host pointers to 32-bit `u32` when embedding display list matrix/vertex pointers.
- **Fix:** Redefined in `include/PR/os.h` to pass directly through `osVirtualToPhysical((void *)(x))`:
  ```c
  #define OS_K0_TO_PHYSICAL(x)  osVirtualToPhysical((void *)(x))
  #define OS_K1_TO_PHYSICAL(x)  osVirtualToPhysical((void *)(x))
  #define OS_PHYSICAL_TO_K0(x)  ((void *)(uintptr_t)(x))
  #define OS_PHYSICAL_TO_K1(x)  ((void *)(uintptr_t)(x))
  ```

---

## 2b. Session 4 — verification of session 3, and the gun-barrel intro

Session 3's changes were built but never run on the headset (it was
disconnected). Session 4 audited them against the code and ran them:

- **Verified on device:** the Rareware logo screen now runs to completion
  (about three seconds) and the game moves on to the gun-barrel intro. The
  matrix-buffer probe in `load_display_rare_logo` printed sane static
  addresses. The static buffers are a safe change; the "stage pool reset"
  explanation in §2A above was never confirmed, and `MEMPOOL_STAGE` is not
  reset between title screens, so treat the cause of the original `0x1`
  fault as unknown rather than as a pool-lifecycle class.
- **Segment binding audited:** `GEVR_SEG_GUNBARREL` (`0x2A4D50`, 107904
  bytes) is bound correctly and `sub_GAME_7F008DE4` now copies from the
  bound pointer rather than the address of the variable. The manifest
  generator (`tools/gevr_gen_rom_manifest.py`) had not been taught the new
  segment; it has been, and regenerating the manifest now reproduces the
  file byte for byte.
- **`OS_K0_TO_PHYSICAL` change:** correct, but it returns `uintptr_t` and
  `titleRenderFolderMenuBackgroundLines` takes `u8 *`; it compiles through an
  implicit conversion. Harmless, noted.

Fixed in session 4 on the gun-barrel path:

1. **`src/game/rle.c`** — all three RLE decoders read their width/height
   header as native `u16`; the cartridge header is big-endian, so the run
   count came out enormous and `rle_expand_8bit` ran off the buffer
   (`SEGV_ACCERR` on a page boundary). They read the bytes in cartridge
   order now. Only the 8-bit decoder has callers.
2. **Animated model slots** — `initializeGunBarrelIntro` runs after
   `lvlStageLoad` has cleared the level-resetting flag, so
   `modelmgrInstantiateModelWithAnim` may only use the spare slots, whose
   runtime-data capacity (`ANIM_MODEL_SPARE_RWDATALEN`, 140 words) was tuned
   for cartridge record sizes. Host records with pointers are twice as big,
   no slot qualified, and `setup_chr_instance` returned NULL
   (`modelSetScale` faulted on offset `0x28`). Both spare capacities are
   doubled in `initunk_005520.c`, and both instantiators now fall back to a
   fresh allocation with a logged warning instead of returning NULL.
3. Both of Bond's tuxedo models convert cleanly: `CdjbondZ` 260 blocks /
   79 nodes and `CheadbrosnanZ` 8 blocks, 0 warnings.

**Screenshots:** `adb exec-out screencap -p` captures the headset's
passthrough/home layer, not the game's OpenXR layer, so it cannot confirm
what the game draws. Have someone wear the headset for the first twenty
seconds after launch instead.

### Session 4, continued — the gun-barrel intro runs to completion

Each item below was found by booting on the headset, reading the tombstone,
and mapping the faulting `pc` to a source line with the NDK's
`llvm-symbolizer --obj=<libgevr.so> -i -f <pc offset>` (the unstripped
library is under `android/app/build/intermediates/cxx/Debug/*/obj/arm64-v8a/`).

1. **Controller callback slot overwritten with `1` (`SIGBUS`, `pc=1` in
   `joyConsumeSamplesWrapper`).** The six input getters in `src/joy.c`
   (`joyGetButtons`, `joyGetStickX/Y`, `joy7000C174/C284`,
   `joyGetButtonsPressedThisFrame`) count "bad reads" per pad in arrays that
   the linker placed directly after `g_ContPlaybackFunc` / `g_ContRecordFunc`.
   A pad number of `-1` (a player with no controller) indexed straight back
   into those slots. The getters now reject pad numbers outside `0..3`. No
   recurrence in six subsequent boots.
2. **`texSelect` fault on a 32-bit address (`0xc9c0xxxx`).** The kseg address
   macros in `include/PR/R4300.h` (`PHYS_TO_K0`, `K0_TO_PHYS` and friends)
   masked or set the top bits of a `u32`; they are pointer-width identities
   now. A `(u32)` cast on a matrix pointer in `bondview2.c` (`perspmtx`) went
   with it.
3. **Animation blend path (`sub_GAME_7F06DB5C`, model.c)** kept the parent
   node and a matrix pointer in `s32` stack slots (`sp1C`). It starts when
   `modelSetAnimation` blends into `bond_eye_fire`, a few seconds into the
   intro. Rewritten with typed locals; the joint-positioned callback is called
   through its real prototype.
4. **`draw_blackbox_to_screen`** was handed `(s32)&view_left` etc. in
   `bondview2.c` (status-bar text); pointers passed as-is now.
5. **Bond body header (`djbond_header.RootNode`) overwritten with `-1` a few
   seconds into the intro.** The blood-drop effect (`die_blood_image_routine`)
   decided it had finished by comparing its read cursor against the address of
   `die_blood_image_end`, a zero-initialised byte that the cartridge linked
   right after the image data. Here it lands in `.bss`, so the decoder never
   stopped and walked through `.data` feeding garbage to the RLE loops. The end
   is now `die_blood_image_1 + sizeof(die_blood_image_1)`. With that, the
   header stays intact (verified with a watch on every step of the intro tick)
   and the intro runs through the blood drop and fade.
6. **`enum HEADS` was unsigned.** `HEAD_FIXED = 0xFFFFFFFF` and
   `HEAD_RANDOM = 0xFFFFFF9F` do not fit an `int`, so clang made the enum
   unsigned and `if (head >= 0)` in `init_menu18_displaycast` (the cast
   screens after the GoldenEye logo) let `HEAD_FIXED` index
   `c_item_entries[0xffffffff]`. Spelled as `-1` / `-97` in
   `src/bondconstants.h`. No other enum in the tree overflows `int`.
7. **`isLoaded`** was only set in debug builds but tested unconditionally in
   `drawjointlist`, which logged one line per joint per frame (thousands of
   lines a second, enough to slow the run). `modelCalculateRwDataLen` sets it
   always now.
8. Earlier in the session: `makeonebody` (`chr_b.c`) passed the head switch
   node through an `s32`; 79 `(s32)&ANIM_DATA_x` / `(s32)ptr_animation_table`
   sites became `uintptr_t`; `set_vtxallocator` takes a typed function
   pointer (the shadow under Bond called through it).

**What the logs proved (2026-09-19, no controllers attached), which is not
the same as what a person has seen:** the game logic ran, unattended, through
legal page, Nintendo logo, Rareware logo, gun-barrel intro, GoldenEye logo,
every cast screen, the demo guard, the title reload and the file-select
screen, which then held waiting for START; every model converted with 0
warnings. **What a person saw:** nothing. Until item 16 below the OpenXR
session was never begun, so the headset showed its loading environment the
whole time, and after item 16 the first display list crashed the renderer
(item 18). Visual acceptance of every screen above is pending; `adb screencap`
does not capture the game layer, so someone has to wear the headset.

9. **`enum BODIES` was unsigned too.** The cast table ends with a body of
   `0xFFFFFFFF` and `interface_menu18_displaycast` tests `body < 0` to stop;
   it never did, so the terminator's string id 0 reached `textMeasure` as a
   NULL string. `BODY_TABLE_END = -1` at the end of the enum in
   `src/bondconstants.h` makes it signed. Watch for this class elsewhere: any
   enum whose sentinel is written as a hex constant that does not fit an int.
10. **`romCopyAligned` (`src/ramrom.c`)** did its alignment arithmetic in
    `s32`; rewritten at pointer width, returning `void *` (prototype in
    `src/ramrom.h` updated).
11. **Attract demo guard, temporary.** After the cast screens the title picks
    a recorded demo (`select_ramrom_to_play`) and loads its stage. The
    `ramrom_*` demo symbols are NULL placeholders in
    `port/src/gevr_engine_shim.c` (the table holds their addresses, so the
    engine cannot tell), and stage loading is not ported. The guard in
    `src/game/ramromreplay.c` logs a warning and calls `bossRunTitleStage()`,
    which is what a finished demo does. Remove it when the demos are bound
    (they need a ROM segment in the manifest and a swap of
    `ramromfilestructure`: two `u64`, then `u32`/enum fields, then the
    recorded controller stream) and the level loader exists. This is not an
    intro bypass; the intros all still play.

12. **Model hit-entry pool overran the player globals (`set_cur_player`
    faulted on a huge index after the demo guard restarted the title).** The
    cartridge's pool of 600 twenty-byte `ModelHitEntry` records was split by
    the decomp into dummy labels (`char g_ModelHitEntries[0xC]`,
    `dword_CODE_bss_80076AE0[0x2E28]`, `g_ModelHitEntriesPenultimate[0x28]`).
    `initModelHitEntryFreeList` threads 600 host entries (40 bytes each) from
    the first label, so every stage load wrote 24000 bytes over the globals
    after it: `g_playerPlayerData`, `g_playerPointers`, `g_playerPerm`,
    `player_num`, the gas/alarm/clock state, and on into `title.c`'s
    instance pointers. Found with a watch that printed the three player
    globals holding addresses of their neighbours at a 40-byte stride.
    `objecthandler.c` now defines the real array and `initunk_005450.c`
    indexes it. Look for the same shape elsewhere before trusting any
    "linker label" pool (see `bg.c`'s `dword_CODE_bss_8007FF90` cast to
    `bg_envdata_entry_local[]` for the level loader).

13. **Second title screen died silently (kernel: "exited due to signal 11",
    no tombstone) inside `texReset()`.** `texLoad()` rewrites the slot it is
    given (a display-list `SETTIMG` word or an image-table `index`) from an
    image id to the decoded texture's address. `texReset()` runs on every
    stage load over the same static tables, so on the reload those slots
    already held addresses; the cartridge recognised that by the kseg0 bit,
    the port did not, and a pointer's low half became a texture number whose
    "compressed size" was copied onto a 4000-byte stack buffer. `texLoad`
    now returns when the slot is not one of the two id forms (`abcdxxxx`,
    `0000xxxx`). Found by logging every call of the reload path, since a
    smashed stack leaves no tombstone.

14. **File select (`init_menu05_fileselect`, reached after the demo guard
    restarts the title).** Its buffer pointer was summed as `s32`
    (`(s32)ptr_logo_and_walletbond_DL + 4096*10`), as was the briefing text
    buffer at `front.c:6502` and the wallet-Bond display-list address in
    `load_walletbond`; all three are pointer arithmetic now. The wallet-Bond
    model no longer fit the cartridge's 0xA000-byte slot once converted
    (38016 bytes plus the display-list rewrite), so `WALLETBOND_MODEL_BYTES`
    (`4096*20`) in `front.c` sizes the model region and moves the two
    neighbours that follow it inside the 0x78000 allocation.
15. **`bgApplyDynamicCCRMLUT` (`bgapply.c`)** found the end of a display
    list by reading byte 0 of the command (the opcode in big-endian order);
    it tests the top byte of `w0` now. With `end == NULL` it used to walk
    past the list.

16. **Nothing ever reached the headset.** The user's first hands-on test
    showed a black loading environment ("it just hangs") while the log
    showed the game running through the intro. The OpenXR session in
    `port/vr/vr_openxr.cpp` was driven only by Perfect Dark's main loop
    (`port/src/pdmain.c`), which `CMakeLists.txt` excludes; nothing in this
    build called `vr_initialize`, `vr_poll_events`,
    `vr_begin_frame_and_update_poses` or `vr_end_frame_and_submit`, so the
    session was never created and every "frame presented" line was only the
    pump's own counter. `port/src/gevr_engine_shim.c` now drives them from
    the frame pump in pdmain.c's order: initialise once the GL context
    exists, then per game frame poll events, begin the XR frame (which also
    paces the loop to the display), let the game draw, submit at the next
    retrace. Verify with `adb logcat GoldenEye-VR:V` ("VR system ready", "Session
    begun") and by wearing the headset. Note for the runbook: log-only
    verification cannot tell rendering from a black screen; someone has to
    look.
18. **First display list executed in VR crashed in `gfx_sp_vertex`
    (`gfx_pc.cpp`) on a near-NULL address.** Perfect Dark's microcode stores
    a 16-bit index into a colour table (loaded by `G_COL`) in the last four
    bytes of a vertex; GoldenEye's Fast3D vertex keeps RGBA (or the normal
    plus alpha, under lighting) there and never issues `G_COL`. The renderer
    read two colour bytes as an index off a NULL table. It now uses the
    vertex's own bytes when no table has been loaded. Note that before item
    16 the renderer never executed a display list at all (its non-VR path
    draws nothing), so every "rendering" milestone in earlier sessions was
    log-only.
19. **Gun-barrel display list overran its slot** ("FATAL: Unknown GBI opcode
    0x00" as the intro began). `initializeGunBarrelIntro` reserves 0x100
    bytes for the list `sub_GAME_7F01BFF8` builds: 31 commands, 248 bytes on
    the cartridge, 496 with 16-byte host commands, so the list ran into the
    RLE image that follows and the image overwrote its end. The slot is
    0x200 now. Any other hand-carved `Gfx` slot in the title files deserves
    the same check when its screen is first seen.
20. **`sysFatalError` blocked on an SDL message box on Android**, leaving the
    process alive and the headset blank; it aborts there now, so a fatal
    error leaves a tombstone.
**First human observations (2026-09-20, in the headset, Quest 3):** legal
screen and Nintendo logo look right; the Rareware logo is somewhat
distorted/stretched; the gun-barrel intro plays but a little too fast and
Bond's textures glitch; the cast screens show every character animating,
textures glitched; after the cast the screen goes black (this is the
file-select folders, which the logs say loaded); controller buttons appeared
to do nothing; there is no sound at all (audio was still stubbed then).
**Superseded:** audio works and the SFX bank has since been debugged - §11.

21. **Speed:** the pump handed the game one retrace per XR frame, 72 a second
    on this headset; the N64 gave 60 and `bossMainloop` ticks once per
    retrace (its `osGetCount` check only demands half a frame). The pump now
    delivers a retrace when 1/60 s is due and otherwise re-presents the last
    image, so the game ticks at 60 Hz while the runtime paces at 72.
22. **Probes in the tree (remove when done):** `gfx:` line once a second
    from `gfx_run` (frames, triangles, flushes, colour-image address) to tell
    a black frame from one that draws nothing; `input: pad0 ...` in
    `osContGetReadData` whenever pad 0 has a button or stick down, at most
    once a second, to prove input reaches the game independently of the
    screen.

23. **The VR layer's logger (`port/vr/vr_log.c`) wrote to `vr_debug.txt`
    by relative path**, which cannot be opened on Android, so every message
    from `vr_openxr.cpp`, `vr_input.cpp` and the renderer's VR code was lost
    (including action-sync failures and the renderer diagnostics). It logs
    to logcat under `GoldenEye-VR` on Android now; the boot script captures that
    tag.
24. **Input reaches the game** (`input: pad0 buttons ...` fires), but with
    Z (`0x2000`) held permanently and nobody touching the controller: the
    "select" boolean action reads true. Under investigation.

25. **`osViBlack` is a no-op in the port now.** Perfect Dark's version
    cleared the screen with a `videoStartFrame`/`videoEndFrame` pair issued
    from the game thread around a video-mode change; the frame pump never
    knew, and after the title reload the folder screen (which the
    statistics showed drawing ~1000 triangles a frame) came out black. The
    blank is cosmetic.
26. **`osGetCount` is locked to delivered retraces**: one frame's worth per
    retrace plus real time inside the frame, capped at one frame. The game
    turns a tick's counter delta into whole frames (`frametiming.c`); with
    retraces landing on 72 Hz boundaries the real gaps alternate 13.9 and
    27.8 ms and rounding ran the game about 17% fast even at 60 retraces a
    second. `osGetTime` stays real time.
27. **Headset screenshots over adb:**
    `adb shell am startservice -n com.oculus.metacam/.capture.CaptureService -a TAKE_SCREENSHOT`
    writes `/sdcard/Oculus/Screenshots/com.gevr.port-<date>.jpg` (one eye).
    This is how the screens were finally seen from the PC; the boot script
    could fire it at timed offsets.

28. **2D screens drawn 1.375 times too large ("stretched" logos, legal text
    cut off):** the renderer maps rectangles through
    `gfx_current_native_viewport`, which the port boots at 320x220 and which
    nothing in GoldenEye updated. `viSetXY` in `src/fr.c` now calls
    `videoUpdateNativeResolution`, so the 440x330 front-end screens and the
    320x240 game screens map correctly.
29. **Input from the PC:** write a hex N64 button mask (and optional stick
    pair) to `/sdcard/Android/data/com.gevr.port/files/gevr_input.txt`; the
    pad reader in `port/src/libultra.c` applies it to pad 0 for four frames
    and deletes the file. `1000` is START, `8000` A, `4000` B, `0000 0 -80`
    stick down. With the screenshot intent (item 27) the menus can be driven
    and seen without anyone in the headset. Test hook, not a feature.

30. **Entering the file select with START (no stage reload) hangs the game
    thread**: the pump and draw statistics stop the moment START lands and
    the process sits at 100% CPU; the screen shows the last (black) frame.
    The six silent `while (1)` spins in `src/memp.c` (bank exhausted,
    bank overrun) are `sysFatalError` calls now, naming the bank and the
    request, so an exhausted bank leaves a tombstone. Suspect: the STAGE
    bank, sized for cartridge records, cannot hold the title stage plus the
    four wallet-Bond instances; the reload path survives only because it
    starts from an empty bank. Enlarging the banks on the port (the arena
    is `osGetMemSize()` bytes, `port/src/main.c`) is the likely fix.

31. **File-select hang, current evidence (2026-09-20, unresolved).** Pressing
    START (or reaching the folders through the demo guard) stops the pump
    and draw statistics; the process sits at 100% CPU showing the last
    frame, which is black. The allocator is not the cause (its spins are
    fatal now and nothing fired). A stall watchdog in
    `port/src/gevr_engine_shim.c` (signals the game thread after five
    seconds without a retrace, once armed by the first retrace) caught the
    thread inside `lvlManageMpGame -> menu_init -> viSetAspect`, i.e. the
    game *is* ticking, while `os_scheduler.frameCount` and the pump's own
    checkpoint counters read as if reset (`entries 1`). Something on the
    menu switch appears to reset or overwrite the pump's state rather than
    block it. Next: log `&os_scheduler`, `frameCount` and the checkpoint
    counters every tick from `bossMainloop` across the START press, and
    compare with `menu_init`'s writes (`viSetFrameBuf2(ptr_menu_videobuffer)`
    and the 440x330 setup are the only unusual things it does). The pump
    checkpoint counters (`gevrPumpStage`/`gevrPumpEntries`/`gevrPumpLoops`)
    are probes and can go once this is solved; the watchdog is worth keeping.
    **The watchdog killed the user's own launches** (both "instant crashes"
    on 2026-09-20 07:53 were its SIGSEGV, SI_TKILL): launched from the
    library, the first frame waits on the runtime's swapchain through the
    loading transition for more than five seconds. It now arms only after
    thirty retraces, logs a stall instead of killing, and signals for a
    stack only when `/sdcard/Android/data/com.gevr.port/files/gevr_watchdog_kill.txt`
    exists.

**Testing from now on is done by the user in the headset** (their request:
blind PC-driven runs cost too much for what they show). The tools for the
PC side remain: headset screenshots (item 27), PC-driven input (item 29),
the watchdog, and the `pump:`/`gfx:` statistics.

**Headset recording, 2026-09-20 08:00 (pulled from /sdcard/Oculus/VideoShots,
frames extracted with ffmpeg; the user's own description agrees):**
- Legal page: 2D text correct and complete; the 3D parts (007 logo, "4"
  badge, signatures) sit at a different scale and overlap the text.
- Nintendo logo: a black extrusion with white edges only, rotating and
  filling the view. On the cartridge it is a lit silver logo. Lighting or
  the reflection-map (texture-gen / lookat) path, not textures.
- Rareware logo: texture intact, cropped top and bottom: too close.
- Gun barrel: the rifling background shows heavy horizontal line/jag
  artifacts (the 300 one-pixel-tall I8 line textures of
  `titleRenderFolderMenuBackgroundLines`); Bond and the blood overlay OK.
- GoldenEye logo: correct.
- Cast screens: name text correct; every character model is far too close
  (chest fills the view, heads cropped, some characters out of view) and
  clips at the near plane as it animates. The eye is not where the game's
  camera is (the game puts its camera thousands of units back); the VR
  path composes the head pose without that distance. Natalya's blouse and
  Trevelyan's weapon show dotted/moire patterns at that magnification.
- Menu button: black screen (item 31), then the user opened the Quest menu.

Recommended shape of the fix for the front end: render the game's frame
(2D and 3D together, with the game's own camera and projection) to a
texture and show it on a flat quad in front of the viewer, like a cinema
screen; only gameplay should use true stereo. That fixes the scale, the
2D/3D mismatch and the clipping in one place.

32. **Halftone grid on textures (RAREWARE lettering, Bond's shirt front,
    weapons) — the "texture glitch".** GoldenEye loads most textures with
    `gDPLoadBlock`'s dxt at 0, which on the RDP means the data is already in
    TMEM's layout: every odd row has the two 32-bit halves of each 64-bit
    word swapped. The decoder does that on purpose (`texSwapAltRowBytes`,
    image.c) and the compiled-in Rareware lettering is stored that way in
    the cartridge; the texture unit swaps them back when sampling. fast3d
    reads the bytes linearly, so those textures showed odd rows shuffled in
    texel pairs. Textures loaded through `gDPLoadTextureBlock` (non-zero
    dxt: the Rareware "R", the GoldenEye logo) were fine, which is what gave
    it away. `gfx_dp_load_block` now records `tmem_swizzled = (dxt == 0)`
    on the loaded texture and `importTextureNative` un-swaps odd rows
    (per `line * 8` bytes) before the format importers run; 32-bit textures
    are left alone. Confirmed fixed by the user in the headset (2026-09-20).

33. **Everything far too close / clipping in the headset — fixed by a virtual
    screen.** The Perfect Dark VR layer never replaces the game's projection:
    it expects the game's own camera code to build its perspective from
    `XrFov`/`XrAspect` and the head pose, and its vertex shader only adds the
    per-eye shear. GoldenEye's camera does none of that, so its 4:3, ~60 deg
    frame was submitted as if it covered the headset's full ~100 deg view:
    magnified, fixed to the head, 2D and 3D disagreeing. Rather than teach
    every GoldenEye camera path about the headset now, the frame is shown on
    a world-locked cinema screen, which is also the right presentation for
    the front end and cutscenes even once gameplay is true stereo.

    How it works (`port/vr/vr_screen.{h,cpp}`, `vr_openxr.cpp`,
    `gfx_pc.cpp`, `gfx_opengl.cpp`):
    - `gfx_run` in VR renders the display list into an off-screen two-layer
      texture array of the game's own aspect (SCREEN_WIDTH x SCREEN_HEIGHT
      scaled to the eye buffer's height, at most 1440 tall) through the same
      multiview path as the eye buffers, with the eye offsets zeroed and a
      new `uVrFlat` vertex-shader uniform that passes clip positions through
      untouched. Two layers rather than a 2D target because every game
      shader declares `num_views = 2`; both layers hold the same flat frame.
    - During that pass (`gevr_screen_pass`) the viewport maps through
      `gfx_adjust_viewport_or_scissor` instead of being forced to the full
      FBO, `g_vr_internal_scale` is 1, the scissor gets no lens margin and
      the CPU clip margins are zero. `gevr_target_w/h` replaces
      `vr_get_internal_render_width/height` wherever the display list maps
      into a target, so the same code serves both passes.
    - Layer 0 is blitted into a dedicated 2D swapchain and submitted as an
      opaque `XrCompositionLayerQuad` in play space (`vr_screen_present`):
      the compositor does the head tracking, so the image is stable and
      text stays crisp. The eye buffers hold only the surroundings: the
      pause-hub floor grid when the runtime gives a floor-relative space.
    - Placement (`vr_screen_recenter`): 2.5 m ahead of the head along its
      floor-flattened view direction, at eye height, level, spanning 60 deg
      horizontally (`VrScreenDistance`, `VrScreenFov` in vr_openxr.cpp).
      Set on the first presented frame; holding the LEFT stick click for
      about a second re-centres it (shim, `gevrVrFrameBegin`).
    - `gevrVrScreenMode` (gfx_pc.cpp, default 1) is the switch. Turning it
      off restores the previous direct path, which is what true-stereo
      gameplay will use once the game's camera is driven by the headset.
    Seen by the user 2026-09-20 (build 14:37): image upright, correct size
    and distance, head-locked correctly. Reported: the pause-hub grid was
    unwanted (removed, build 14:55), the screen flickered in and out
    (layer now submitted every XR frame, build 14:55; both confirmed fixed
    by the user), black horizontal lines through the gun-barrel white,
    blood drip not animating and an instant red tint (both still open).
    See §7 for the plan. Original note follows.
    First thing to check was orientation (an
    upside-down or mirrored image means the blit's row order), then whether
    2D text and 3D scenes sit correctly inside the screen's frame.

17. **Quitting from the universal menu takes ten seconds and ends in
    `destroyTimeout`**: `MainActivity.onDestroy` calls `nativeDestroy` but
    the game thread never exits, so the system kills the process. Harmless
    for now; a clean shutdown path (stop the game loop, `vr_shutdown`) is
    still to do. **Closed:** fixed and confirmed by the user - see §11.

Four `stage:` log lines were left in `bossMainloop` (unload, switch, load
done, first frame) because a stage change is where the next problems will
be. The probes used during this work (`gunprobe:` in `sub_GAME_7F007F30`,
`gevrWatchCheck()` in `title.c` and the shim, `gevrPlayerWatch()`) have all
been removed.

**Build-script caveat:** `tools/gevr_boot_test.ps1` runs gradle under
`$ErrorActionPreference = "Stop"`, so a compile error surfaces as a bare
`NativeCommandError` with the real messages swallowed. Run
`android/gradlew.bat assembleDebug --console=plain` from Git Bash to read
them.

---

## 3. Comprehensive Defect Classification Reference

Virtually every crash encountered in this decompilation port belongs to one of six defect classes:

| Class | Mechanism | Detection / Manifestation | Resolution Pattern |
|---|---|---|---|
| **Class A: Endianness** | ROM bytes are big-endian; ARM64 host reads them natively little-endian. | Reversed `u16`/`u32` values, out-of-bounds indices, garbage animation frame counts. | Per-struct swap passes right after ROM extraction (e.g., `gevr_romswap.c`, `gevr_model.c`). Never use blanket 32-bit word swaps. |
| **Class B: 32-bit Pointer Slots** | Original N64 code stored pointers into `s32`/`u32` fields or arrays. | High 32 bits truncated (`0x00000000xxxxxxxx`); `SIGSEGV SEGV_MAPERR` on dereference. | Keep cartridge offsets as offsets; resolve against host base pointers at runtime. Widen struct fields to `uintptr_t` or pointers. |
| **Class C: Implicit Declarations & s32 Parameters** | Undeclared C functions default to returning `int` (truncates return pointer). Functions taking pointers declared as `s32`. | Compiler warnings (`-Wimplicit-function-declaration`, `-Wint-conversion`). High-half pointer truncation. | Maintain `src/gevr_implicit_protos.h` via `tools/gevr_implicit_decls.py full_build.log`. Fix parameter signatures in headers. |
| **Class D: Bitfield Packing & Linker Relics** | C bitfield ordering is inverted between big-endian MIPS and little-endian ARM. A bitfield followed by a narrower plain member packs into one unit on MIPS but not on LP64, which moves every later field and changes `sizeof`. Taking `&Symbol` of N64 link symbols. | Struct fields read wrong bits; fields read *late* and array strides too large, so `&base[index]` disagrees with the `(index << n)` form used elsewhere; link symbols have no physical address. | Read bitfields by explicit field name or shift/mask. Where a struct must match cartridge bytes, spell the header out as plain members and check with `offsetof` — see §12.7 (`StandTile`). Convert link symbols to pointer variables initialized from ROM manifest. |
| **Class E: Memory Pool Lifecycles** | Matrix or transient buffers allocated in `MEMPOOL_STAGE` across menu/intro state changes. | Pointer suddenly points to `0x0000000000000001` or freed pool data across screen boundaries. | Back screen-spanning transient buffers with persistent static storage (`s_matrixBuffer...`). |
| **Class F: KSEG0 (`0x80000000`) Arithmetic** | Hardcoded `+ 0x80000000` or `- 0x80000000` in macros or pointer calculations. | Out-of-bounds pointers, heap addresses offset by 2GB. | Strip N64 virtual memory offsets; pass pointers transparently through `osVirtualToPhysical`. |

---

## 4. User Directives & Alignment Decisions (Resolved via `/grill-me`)

1. **Intro Sequence Preservation:**
   - *Decision:* Do **not** bypass or fast-forward through the intro sequences. Maintain full fidelity through the Rareware spinning logo and the 007 Gun-Barrel sequence into the menus.
2. **Post-Intro Priority Target:**
   - *Decision:* Proceed directly to the **Dossier & File Select Menus (`MENU_FILE_SELECT`)**.
   - Ensure folder 3D models, wallet Bond model (`PROP_WALLETBOND`), RLE background lines, and dossier selection flow work cleanly before launching stage gameplay.

---

## 5. Immediate Next Steps & Action Plan

### Step 1: Reach the file select (needs input)
The title loop now runs unattended: legal, Nintendo, Rareware, gun barrel,
GoldenEye logo, cast screens, (demo skipped), title again. The file-select
folders only appear on START, so the next milestone is controller input:
`port/src/input.c` (`inputReadController`, SDL game-controller based) feeds
`osContGetReadData` in `port/src/libultra.c`; the frame pump already polls
once per retrace. Map the Quest Touch controls through `port/vr/vr_input.cpp`
(see Step 2 for the buttons the menus need) and confirm `g_ConnectedControllers`
reports pad 0, or the getters in `joy.c` return nothing.

### Step 2: Dossier & File Select Menu (`MENU_FILE_SELECT`)
- **Target Files:** `src/game/front.c` (`init_menu05_fileselect`, `constructor_menu05_fileselect`).
- **3D Models Required:**
  - `PitemZ_entries[PROP_WALLETBOND]`
  - Folder models (`assets/obseg/...`)
  - Ensure all models convert cleanly via `port/src/gevr_model.c`.
- **Menu Background:**
  - `titleRenderFolderMenuBackground` uses `dword_CODE_bss_8006958C` (decompressed by `rle_expand_8bit` from `GEVR_SEG_GUNBARREL`). Verify line buffer rendering.
- **Quest Touch Input Mapping:**
  - Map Quest Touch analog stick / D-pad and buttons (`A`, `B`, `Trigger`) in `port/vr/vr_input.cpp` to GoldenEye's virtual controller:
    - Up / Down navigation: `U_CBUTTONS` / `D_CBUTTONS` or analog Y.
    - Confirm / Open Dossier: `START_BUTTON` or `A_BUTTON`.
    - Back / Cancel: `B_BUTTON`.

### Step 3: Mission Launch & Level 1 (Dam) Prep
- When a file and mission are selected (`MENU_RUN_STAGE` → `lvlStageLoad(LEVELID_DAM)`):
  - **`src/game/bg.c`:** Address remaining 79 pointer-to-integer conversion warnings (`ptr_bg_data` is `s32`, `BG_SEG_TO_PTR` segment math).
  - **Stan Tiles (`T...Z`):** Write swap/conversion pass based on Perfect Dark's `filetiles.c`.
  - **Stage Setups (`U...Z`):** Write setup unpack pass based on Perfect Dark's `filesetup.c`.
  - **Background Blocks (`obLoadBGFileBytesAtOffset`):** Ensure sizing matches host pointer requirements.

---

## 6. Developer Runbook & Environment Reference

### Toolchain & Build Environment
- **OS:** Windows (Host) / Android Horizon OS (Target: Meta Quest 2 / Pro / 3 / 3S).
- **Architecture:** `arm64-v8a` (`aarch64-none-linux-android24`).
- **NDK:** `25.1.8937393`, Clang, CMake 3.22.1.
- **Root CMakeLists:** `CMakeLists.txt` builds `libgevr.so`.
- **Android Studio Project:** Located in `android/`.

### Commands
- **Full Build via Gradle:**
  ```powershell
  cd android
  .\gradlew.bat assembleDebug
  ```
- **Automated Test & Log Capture:**
  ```powershell
  powershell -File tools\gevr_boot_test.ps1 -Seconds 20
  ```
  *(Note: The script automatically wakes the Quest and sends `com.oculus.vrpowermanager.prox_close` to prevent the headset proximity sensor from putting the app to sleep while testing off-head).*
- **Inspect Full Boot Log:**
  ```powershell
  Get-Content $env:TEMP\gevr_boot.log -Tail 100
  ```
- **Model Debug Probe (Host Python):**
  ```powershell
  python tools/gevr_model_probe.py "007 - GoldenEye.z64" <FileName> <numSwitches> <numTextures>
  ```
- **Regenerate Implicit Prototypes:**
  ```powershell
  python tools/gevr_implicit_decls.py path/to/build.log
  ```

### ROM Provisioning & Legal Standards
- **Strict BYO-ROM Policy:** No proprietary ROM bytes are ever committed to the repository. Only segment offsets and lengths are defined in code.
- **Cartridge File:** USA GoldenEye 007 NTSC (`007 - GoldenEye.z64`, cart id `NGEE`).
- **Headset Destination Path:** `/sdcard/Android/data/com.gevr.port/files/data/ge.z64` (or `/sdcard/GEVR/ge.z64`).

---

## 7. Plan for the next agent (written 2026-09-20, 15:00)

Read this section first. It supersedes §5 for ordering. The user tests in
the headset and reports; the agent builds, installs and reads logs. Do not
run blind launch/capture loops from the PC.

### 7.0 State of the installed build (14:55)

- Whole title loop renders in the headset. Textures are correct (item 32).
- The game is shown on a world-locked virtual screen (item 33). Build 14:55
  removed the pause-hub grid from behind it (eye buffers are now black) and
  submits the screen layer on every XR frame, not only on frames the game
  drew. The 72 Hz display vs 60 Hz game means one XR frame in six carried
  no game frame; those frames were submitted without the layer, which is
  the "screen flickers in and out" the user saw. Confirmed fixed by the
  user in the headset (2026-09-20, ~15:10): no flicker, black surround.
- Recentre is currently: hold the LEFT stick click about a second
  (`gevrVrFrameBegin` in port/src/gevr_engine_shim.c).

### 7.1 The governing principle: port, do not invent

> **Stale premise (corrected 2026-09-21).** Written when the port lived
> inside a fork of the GEVR PC repository with its `docs/` on hand. Those
> are not in this repository, so every `docs/NNN-...`, `docs/CONTROLS.md`
> and `packaging/...` citation below is a dead path - they live at
> https://github.com/no6969el/GEVR. More importantly, no GEVR or GETV code
> is in this tree (verified; see CREDITS.md), so "port the GEVR PC version"
> is not an available method. The *observations* below about how a headset
> presentation should behave are still sound - treat them as design notes,
> not as instructions to copy from a tree you have.

The upstream GEVR PC port (`no6969el/GEVR`, source tree `goldeneye-native`
with `ge_vr_xr.cpp` and the `getv/` tools) already solved the presentation
for a headset, and it did it the same way as item 33:

- Front end, menus and cutscenes: a world-locked virtual screen inside a
  small hub room (`docs/175-U-19-THE-VIRTUAL-SCREEN.md`, `docs/178-...`,
  `docs/CONTROLS.md` "Cinema / menus"). Knobs in
  `packaging/KEEP-DEFAULTS-INVENTORY-vr441.md`: `GETV_XR_PLAY_SCREEN=2`,
  `GETV_XR_PLAY_AUTOSCREEN=1` (switches to the screen automatically for
  the front end), `GETV_VR_SCREENWIDE=3.0` (metres),
  `GETV_XR_PLAY_FOVSCALE_CINEMA=85`.
- Gameplay: true stereo with the game's own camera driven by the headset
  (`GETV_XR_PLAY_STEREO=1`, `GETV_XR_FOVSYM=1`, `GETV_XR_HEAD_TRANSLATE=1`,
  `GETV_XR_UNITS_PER_M=100`, `GETV_XR_PLAY_AUTORECENTER=1`; the camera
  change is the "194 head rotation into the game camera in bondview.c"
  referenced from `docs/228-...` §PHASE 3 and `docs/245-...` §2.1).
- Recentre: both stick clicks together (`GETV_XR_RECENTER_CHORD=1`,
  `docs/CONTROLS.md`).
- Sky / playspace surround for gameplay: `GETV_VR_SKYMESH`, `SKYSCISSOR`,
  `SKYFILL` (`docs/19-blue-band.md` explains the blue band).

Perfect Dark's VR layer (vendored under `port/vr`, `port/fast3d`) supplies
the OpenXR session, multiview rendering, the quad layers, input and the
pause hub. Where GoldenEye needs something PD did in game code (camera,
HUD tags, `vr_dl_is_pause_or_menu`), look at how PD's `src/` did it, then
at how GEVR PC did it for GoldenEye, and port the GEVR PC version.

~~First task therefore: locate the `goldeneye-native` tree on this PC.~~
**Dropped 2026-09-21.** The user confirmed nothing from GEVR or GETV is
used here, and a search of the tree agrees. Do not go looking for it. The
references this port actually has are Perfect Dark's port (`port/`) and
Perfect Dark VR (`port/vr/`), both vendored and both in this repository.

### 7.2 Ordered work

1. **Build 14:55 confirmed by the user:** no grid, no flicker, screen
   world-locked and upright. Nothing to do here; the note about
   `vr_end_empty_frame` (an empty end frame submits zero layers) is kept
   only in case the flicker ever returns.

2. **Gun-barrel background: black horizontal lines through the white.**
   Pre-existing (seen as "jagged" before the screen), now obvious at 1440
   rows. The background is ~300 one-pixel-high I8 line texrects. Steps:
   - Dump the DL during the barrel: `adb shell "touch
     /sdcard/Android/data/com.gevr.port/files/gevr_dumpdl.txt"` then
     `adb logcat -s GoldenEye | grep dl` (marker-triggered dump in
     gfx_pc.cpp, `gevrMaybeDumpDl`). Read the texrect commands: uly/lry
     per line, cycle type (COPY vs 1-cycle), tile line/dxt, `G_TEXTURE`
     scale, and whether consecutive lines are 1 or 2 rows apart.
   - Compare with `gfx_dp_texture_rectangle` (gfx_pc.cpp ~2380): copy and
     fill modes add one pixel to lrx/lry (inclusive), 1-cycle does not; a
     zero-height 1-cycle rect draws nothing; copy mode also changes the
     dsdx/dtdy step. If the game alternates modes or draws every other row
     relying on the N64's interlaced output, the fix is in the rect mapping
     for this case, not in the game.
   - Upstream reference: `docs/231-...` (menu texrect fault was in fast3d,
     `GETV_RECTPROBE=1` instrument) and `docs/232-...` §2 (their texrect
     probe fields). GEVR PC renders the same barrel correctly; diff their
     fast3d texrect path against ours.
   - Also rule out item 32's un-swizzle: it applies to rows >= 1 of a
     dxt-0 load. A 2-row line texture would have row 1 swapped; if the
     dump shows 2-row loads for the lines, test with the un-swizzle
     disabled for `G_IM_SIZ_8b` heights <= 2.

3. **Blood drip does not run down; screen tints red at once.** Read
   `src/game/blood_animation.c` and the intro state machine in
   `src/game/title.c` / `bondview2.c` (`insert_bond_eye_intro`, the
   `die_blood_image` decode) and answer: what advances the drip (a tick
   counter, `osGetCount`, `g_ClockTimer`, or the frame count derived from
   the retrace-locked `osGetCount` in port/src/libultra.c), and does the
   effect read the previous frame's colour image (the N64 draws the drip
   over the back buffer). If it uses the frame delta, the retrace clock
   (item 26) may be giving 0 or a huge delta on the first frames: log the
   value the drip uses per tick. If it reads the framebuffer, that needs
   PD's framebuffer-copy path (`G_COPYFB_EXT`, `gfx_copy_framebuffer` in
   gfx_pc.cpp, emitted explicitly by PD's game code) wired into the GE
   intro the way GEVR PC did it. Check `docs/04-interpolation.md` and
   `docs/261-...` for how GEVR PC treats blood.

4. **File select: black after START (item 31).** Unchanged. Next check is
   in item 31: log `&os_scheduler`, `frameCount` and the pump counters
   every tick across the START press; `menu_init`'s
   `viSetFrameBuf2(ptr_menu_videobuffer)` and the 440x330 switch are the
   unusual writes. The watchdog (`gevr_watchdog_kill.txt` marker) gives a
   stack of the game thread. GEVR PC ran the menus; `docs/207-...` (menu
   crash was cadence: the front end at 60 Hz, not 90) is the relevant note
   since our pump paces at 60 within 72.

5. **Recentre chord and screen knobs, to match upstream.** Replace the
   held-left-click recentre with both stick clicks together
   (`get_button_state(0/1, "thumbstick_click")`, port/vr/vr_input.cpp),
   and expose `VrScreenDistance` / `VrScreenFov` (vr_openxr.cpp) in
   `goldeneye-vr.ini` via `port/vr/vr_settings.cpp`. Upstream sizes the screen in
   metres at a distance (3.0 m wide); ours is 60 degrees at 2.5 m, which is
   2.9 m wide. Keep metres if the user prefers.

6. **Hub room behind the screen.** The user rejected the grid floor; the
   eye buffers are black now. Upstream shows "a small hub room". Only
   revisit if the user asks; port the room, not the PD grid.

7. **Gameplay presentation.** When a level loads, decide per upstream:
   stereo with the headset driving `bondview.c` (port GEVR PC's camera
   change, `docs/245-...` §2.1, and PD's HUD tag mechanism for the 2D
   overlay), with the screen as fallback (`gevrVrScreenMode = 0` switches
   back to the direct path). Do not start this before 2 to 4 are closed.

8. **Cleanup owed** (all marked PORT probe / PORT test hook): pump stage
   counters and watchdog in the shim, `gfx:` and `input:` once-a-second
   stats, `badvtx:` in `gfx_sp_vertex`, the DL dump, `gevr_input.txt`
   injection, four `stage:` logs in boss.c, the demo guard in
   ramromreplay.c (needs the ramrom demos bound and byte-swapped instead).
   Also: lighting looks dark on the Nintendo logo and characters
   (unexplored; check `calculate_normal_dir` / lookat handling). Audio and
   the quit path were on this list and are done (§11).

### 7.3 Commands that work

- Build with visible errors: from Git Bash,
  `cd android && ./gradlew.bat assembleDebug --console=plain` (about 20 s
  incremental; a new .cpp under port/ needs a CMake reconfigure, done by
  touching CMakeLists.txt).
- Install: `adb install -r android/app/build/outputs/apk/debug/app-debug.apk`
  then `adb shell am force-stop com.gevr.port`.
- Logs: `adb logcat -s GoldenEye-VR:V GoldenEye:V DEBUG:F` (VR layer, game/port,
  crashes). Ask the user to report right after the event so the ring
  buffer still holds it.
- Symbolize: NDK `llvm-symbolizer --obj=android/app/build/intermediates/cxx/Debug/x1w2s5x6/obj/arm64-v8a/libgevr.so -i -f <offset>`.
- Headset screenshot: `MSYS_NO_PATHCONV=1 adb shell am startservice -n com.oculus.metacam/.capture.CaptureService -a TAKE_SCREENSHOT`,
  files under `/sdcard/Oculus/Screenshots/`; recordings under
  `/sdcard/Oculus/VideoShots/`.

## 8. Session update — 2026-09-20, 15:46 candidate

### Evidence and reference corrections

- Reviewed the user's `video_2026-09-20_15-14-37.mp4` (42.97 s), with
  contact sheets across the clip and 8 fps samples of the blood transition.
  Barrel stripes are visible at roughly 25–29 s; the full rectangle abruptly
  becomes red around 29 s, without a visible descending edge. Bond and Natalya
  animate with readable names afterwards. The final Quest dialog says **App
  name unavailable**, not App not responding. The video alone does not prove
  when START was pressed or the cause of the ending black screen.
- ~~This workspace IS the user's fork/clone of the public GEVR repository.~~
  **No longer true (2026-09-21):** the port was split out into its own
  repository, `MrSco/goldeneye-vr`, and carries no GEVR code. The sibling
  `other_projects/goldeneye-decomp` has remote `n64decomp/007`.
- The user authorized using Perfect Dark's Quest integration as the reference
  and writing the necessary GoldenEye integration. Missing GEVR source is not
  a prerequisite for progress. Keep §7's blocker ordering and black surround.
- Additional source references supplied by the user:
  `https://github.com/akratch/mgb64` (blood validation/upload-buffer work),
  `https://github.com/jkdansereau/goldeneye-pc-port` (native game/fast3d/menu
  implementation). Their fixes require comparison, not blanket copying.
  The latter's texrect and title-strip paths retain the same quarter-pixel
  endpoints as ours. GEVR docs 231/232/261 also include unresolved barrel/blood
  reports; §7's assertion that GEVR PC renders this barrel correctly was too strong.
- The actual intro path advances `die_blood_image_routine(1)` every other
  case-3 intro update (`title.c`), decodes a mask and submits a 96x80 I4 texture.
  It does not explicitly read the previous framebuffer. A framebuffer-copy
  patch is not justified by this path. The authored stream has 42 frames,
  starting with 41 nonzero pixels and ending with all 7680 pixels nonzero.

### Changes in the installed candidate (not yet visually accepted)

1. `port/fast3d/gfx_pc.cpp`: one-cycle, unflipped, one-row I8 rectangles with
   integer top Y and a three-quarter-pixel height now cover a full row. This
   matches the native row's sample coverage when enlarged, without adding an
   inclusive pixel to all texrects or touching COPY mode. The barrel's original
   rectangles otherwise leave a quarter-pixel gap that becomes visible at
   cinema resolution. Headset confirmation is still needed.
2. `src/game/blood_animation.c`: invalidate the address-keyed fast3d texture
   cache before submitting each mutable blood mask, via the existing
   `videoFreeCachedTexture` API. The dyn allocator reuses those addresses.
   Correct the packer call to 3840 output bytes: it consumes two input pixels
   per byte, so the old 7680 count read past the 7680-byte source allocation.
   First/end decoder logs use the `blood:` prefix.
3. `src/game/blood_decrypt.c`: replace truncated u32 pointer arithmetic in
   the transpose with indexed loops implementing the same pixel mapping.
4. `gevrSchedTraceMenu` in the shim, called before/after `lvlManageMpGame`
   in `boss.c`: log menu changes and once per second on file select, including
   scheduler address/frame counter, pump counters and queue state. Prefix
   `menu-pump:`. **This instruments the START hang; it does not fix it.**

### Verification and next test

- `python tools/gevr_blood_probe.py` passes: compiles the real decoder on the
  host with integer typedefs, checks all 42 frames against an independent
  decoder, verifies transpose, filters, I4 packing and buffer canaries.
  No ROM data is embedded in the test file. Existing unused-variable warnings
  in the decoder remain.
- Gradle `assembleDebug` succeeded; log in `artifacts/build-render-fixes.log`.
- APK timestamp: **2026-09-20 15:46:14**, size 25,736,957 bytes. `adb install -r`
  returned Success, then the app was force-stopped. No automatic launch/test loop.
- User was asked to run the full intro, inspect barrel and blood, then press
  START. Read `GoldenEye`/`GoldenEye-VR` logs immediately afterwards, especially
  `blood:`, `menu-pump:`, `pump:`, `gfx:` and `watchdog:`. Keep §7 items 2–4 open
  until headset evidence closes them; gameplay camera work has not started.

## 9. START hang diagnosed — 2026-09-20, after 15:46 test

**User acceptance:** barrel lines gone, blood drip animates correctly. §7 items
2 and 3 are closed. START/trigger still went black in that test.

Captured `artifacts/headset-start-black.log` and `headset-start-stack.log`.
START (`buttons 1000`) arrived at 15:51:05; frame 2285 reached the file-select
transition. Wallet Bond converted with 0 warnings, but the menu tick did not
return. The diagnostic watchdog marker deliberately stopped the hung process
at 15:52:41; this SIGSEGV/SI_TKILL was diagnostic, not a spontaneous crash.
Marker removed afterwards. No automatic relaunch was performed.

The captured game-thread stack was:
`fileGetSaveStageCompletedForDifficulty` (`file2.c:334`) ->
`fileGetHighestStageDifficultyCompletedForFolder` (`file2.c:861`) ->
`interface_menu05_fileselect` (`front.c:2350`) -> `menu_init` ->
`lvlManageMpGame` -> `bossMainloop`.

**Root cause:** `LEVEL_SOLO_SEQUENCE` had only nonnegative enumerators, so
native compilers made it unsigned. The backwards scan from Egypt through Dam
wraps at zero and cannot satisfy its exit condition. This also affects the
highest-unlocked-stage scan and comparisons treating an empty folder as -1.
The jkdansereau port documents precisely this defect as D142 and adds a negative
sentinel in `src/bondconstants.h`.

**Fix:** add `SP_LEVEL_NONE = -1` before Dam and return it for empty-folder
queries. Dam stays 0, Egypt 19, max 20; no saved mission ids change.

**Watchdog correction:** POSIX `sysSleep` passed a full second as
`tv_nsec=1000000000`, which is invalid. Its supposed one-second sleep returned
immediately and flooded false stall messages. Split the interval into seconds
and nanoseconds, retry EINTR, and return immediately for nonpositive intervals.
Earlier watchdog claims about elapsed stall time are unreliable. The stack
capture itself identifies the real loop; old pump-reset/memory-corruption
hypotheses in item 31 are not established by this run.

**Regression test:** `python tools/gevr_menu_probe.py` passes actual extracted
production enum/save-query functions with mocked storage: empty/missing folders,
completed missions at different difficulties, highest-unlocked queries, enum
size and preserved mission ids. Removing the sentinel reproduces the runaway
scan in a timeout-bounded child process. The same probe checks actual `sysSleep`
conversion (1 ms, 1 s, 2.5000001 s), EINTR retry, zero/negative intervals.

Build log: `artifacts/build-file-select.log`. Headset acceptance of the new
file-select fix is still pending; do not mark §7 item 4 closed from host tests.

`assembleDebug` succeeded (17 s). Replacement APK built **2026-09-20 20:24:19**
(host timestamp), 25,737,069 bytes, SHA-256
`FFCC00BE4BE8691C802E4162DC7E5EF7F806294A2AC06054C53C24F8AB3E9AE1`.
`adb install -r` returned Success; app force-stopped afterwards, ready for the
user to launch. Test START/trigger to folders, navigation, and opening a dossier.

## 10. File select reached; briefing crash and texture rows — 2026-09-20

**User test of 20:24 build:** file select opens, START advances through stage
and difficulty selection, then the app crashes. Folder portraits and lower-left
artwork are scrambled; user cannot steer selection. Screenshot supplied in task.
This closes the old START-to-file-select hang (§7 item 4 / §9), but the front end
and level entry are not yet accepted.

Crash saved in `artifacts/level-start-crash.log`: spontaneous SIGSEGV at
20:32:29, BuildId `3eee479cca06080bc6216be84b3c84660233714b`.
`textWrap+108` (`textrelated.c:757`) <- `print_objectives_and_status_to_menu`
(`front.c:6698`) <- `constructor_menu0A_briefing` <- `lvlRender`.
This is a null objective string in the briefing, before actual level loading.

Changes in the 20:39 candidate:

- `gevrRomSwapBriefing` swaps the Ubrief payload's u16 fields immediately after
  decompression in `load_resource`. These were still cartridge big-endian.
  Actual Dam ROM payload is 48 bytes: brief IDs 0x2c00..0x2c03, objectives
  0x2c04..0x2c07 with difficulty 1,2,2,0, then zero entries. Without conversion,
  the Agent objective becomes 0x072c and selects the wrong language bank.
  No null-string workaround was added to the text renderer.
- `load_briefing_text_for_stage` reserves its 512-byte region using byte
  arithmetic rather than assuming an eight-byte host Gfx.
- `inputReadController`: on LEVELID_TITLE the left Quest thumbstick feeds the
  primary N64 stick using the existing deadzone/sensitivity scaling. The
  borrowed PD mapping sent it to the secondary axes, unused by GE menus.
  A/B, trigger and menu button handling still run before this branch.
- `gfx_pc.cpp`: the dxt-zero native upload path now removes row padding while
  undoing the existing odd-row TMEM word swap. The wallet model's first four
  portraits are 65x65 at 4 bits per pixel: 40 source bytes per row, 33 packed
  bytes. Previous importers consumed padding as pixels. I4/IA4 importers now
  reset nibble position each row and emit exactly width*height pixels, including
  odd widths. No blanket change to COPY rectangles or texture decoding.
  Headset evidence is required to decide whether this fully fixes the artwork.

Verification: `python tools/gevr_frontend_probe.py` passes extracted production
briefing conversion, row unpacking and I4/IA4 import functions, with synthetic
pixel checks at widths 65,95,128 and output canaries. `assembleDebug` succeeds;
existing compiler warnings remain. Log: `artifacts/build-briefing-textures.log`.

APK built **2026-09-20 20:39:56**, 25,737,797 bytes, SHA-256
`B88E4A58FE4EE2984B0967A511FB6ABD532BABD63A4A390AB653F414F40F0FB1`.
`adb install -r` returned Success; app force-stopped, no automatic launch.

Next headset test: left stick moves the file-select crosshair; A/trigger opens
the selected folder; inspect portraits and lower-left artwork; proceed through
stage/difficulty to briefing and then attempt mission launch. If another crash
occurs, capture and symbolize its fresh stack against this build before rebuilding.
Gameplay has not yet run successfully; do not claim the level loader or stereo
camera is fixed. Preserve full intros and black surround.


## 11. Audio, the repository split, and provenance — 2026-09-21

### 11.1 Every sound effect was playing the wrong one

Symptom: the Rare logo sting did not sound like the N64's rising cymbal, and
earlier work had chased it through the FX/reverb path, the envelope model and
the soft mixer's SIMD paths without success.

**Root cause was none of those.** GoldenEye's sound ids are 1-based indices
into `ALInstrument::soundArray`, and the retail code encodes that bias in a
*struct offset* rather than in the index: `ALInstrumentAlt_s::soundArray` sits
at offset 12, one 4-byte pointer ahead of `ALInstrument::soundArray` at 16, so
`alt->soundArray[id]` reads standard entry `id - 1`. With 8-byte pointers the
alternate struct's 12-byte header pads out to 16, the gap vanishes, and every
sound id silently became 0-based. `BIG_CLANK_SFX = 261` also read one past the
end of a 261-entry array.

Fix: `SND_SOUND_INDEX_BIAS` in `src/snd.h`, applied at the single lookup in
`sndPlaySfx` (`src/snd.c`). It derives the correction from the two layouts via
`offsetof`, so it is 0 on the N64 and 1 wherever alignment padding has eaten
the gap. Confirmed by ear. Full write-up, including the offline analysis
against an N64 capture: [`docs/RARE-LOGO-AUDIO-HANDOFF.md`](docs/RARE-LOGO-AUDIO-HANDOFF.md).

**This is a third defect class** alongside the six in §3, and the nastiest so
far: it produces no warning, no crash, and data that looks plausible. The
`sfx chain:` log even looked self-consistent, which is what made the earlier
passes trust it. Add to the §3 table when that is next revised:

| Class | Mechanism | Detection | Resolution |
|---|---|---|---|
| **Class G: Struct-encoded index bias** | N64 code encodes an index offset as a struct-field offset rather than in arithmetic. LP64 alignment padding erases the gap. | Silent off-by-one. Values remain valid-looking; logs stay internally consistent. Suspect when an index also reads one past a count. | Derive the correction from both layouts with `offsetof` so it is a no-op on the original target. |

### 11.2 Audio state

Audio works: soft mixer executing aspMain-style acmds (`port/src/mixer.c`) at
22050 Hz through SDL, CUSTOM FX, the GE additive envelope, and a real
`aPoleFilter`. The quit path is fixed too (`audioPause` / `audioResume` /
`audioShutdown` in `port/src/audio.c`, driven from `MainActivity.onPause`), so
§2b's "no sound at all" and item 17's ten-second quit are both closed.

NEON ADPCM decode is back on. `tools/gevr_mixer_ab/` compiles `mixer.c` twice
into one arm64 binary and A/Bs scalar against NEON on real bank data, on the
headset: **bit-exact** over 2,120,512 samples (291 waves, all 13 shift values,
npredictors 1 and 4), NEON 2.75x faster. `aMix` stays scalar - the paths differ
by <= 1 LSB and no candidate formulation is provably the RSP's. Both are named
knobs now (`GEVR_SCALAR_ADPCM`, `GEVR_SCALAR_MIX`) rather than buried
`!defined(GEVR)` tests.

### 11.3 The repository

The port was split out of `GEVR-OpenGLES` - which was two projects in one
folder, the GEVR PC repo tracked and this port entirely untracked - into
`MrSco/goldeneye-vr`. The old folder remains on disk as reference and holds
`artifacts/` (boot logs, audio analysis, the `audio-reference` mixer tree),
which is why the `artifacts/...` citations above point outside this repo.

Also done, all verified by a clean build and a headset run:

- Perfect Dark log tags renamed: `PerfectDark` -> `GoldenEye`, `PD-VR` ->
  `GoldenEye-VR`, `PerfectDark-GFX` -> `GoldenEye-GFX`. The boot script's
  logcat filter and this document's tag references moved with them.
- Settings file is `goldeneye-vr.ini` (was `pd-vr.ini`).
- `sysGetDataPath()`'s fallback pointed at `/data/data/com.perfectdark.port/`;
  this app is `com.gevr.port`. Unreachable but wrong; fixed.
- 14 dead `Java_com_perfectdark_port_*` JNI shims removed - the JVM could
  never bind them.
- The in-app updater and HD-texture-pack downloader removed: they fetched
  `Alex-LeTux/perfect_dark_VR` releases and Perfect Dark HD textures into a
  GoldenEye port. Their only caller, `port/src/optionsmenu.c`, is excluded
  from the build; see the note at the top of that file.

### 11.4 Provenance, corrected

Two directories are vendored upstream source, not our own work, and now carry
their notices: `port/` from the Perfect Dark PC port (its `port/src/` has 15 of
our 16 files) and `port/vr/` from Alex-LeTux's perfect_dark_VR. Both are MIT,
both carry `Copyright (c) 2022 Ryan Dwyer` through the decompilation they fork.
See `port/LICENSE`, `port/README.md`, `port/vr/LICENSE`, `port/vr/README.md`.

CREDITS.md had this wrong in both directions and has been rewritten against
the tree: GEVR and GETV are credited as origin and inspiration only (no code
of either is here), while the Perfect Dark port and perfect_dark_VR were
promoted from "reference" to vendored source.

### 11.5 Still open

Tracked in [STATUS.md](STATUS.md) rather than here, so there is one list to
keep current instead of two. In short: gameplay has never run, the level
loader is unported, the stereo camera has not been started, and the
probe/test-hook cleanup in §7.2 item 8 still stands.

## 12. App identity, and the Dam load crash — 2026-09-21

### 12.1 Quest app name and icon

The Quest UI showed "App name unavailable" and no icon. `aapt2 dump badging`
on the shipped APK gave the cause for the library entry:

```
launchable-activity: name='com.gevr.port.MainActivity'  label='' icon=''
```

`android:label` and `android:icon` were set on `<application>` but not on the
LAUNCHER activity, and Quest's shell reads the activity's without falling back.
Both are set on `MainActivity` now, the icon pointing at a plain 512x512 PNG
because every density of `@mipmap/ic_launcher` resolved to the adaptive-icon
XML, which Quest does not reliably rasterise for sideloaded apps. App name is
"GoldenEye VR"; the package id stays `com.gevr.port`. **Fixed** - the library
list now shows the name and icon.

**The universal menu's quit dialog still says "App name unavailable", and that
is not ours to fix.** VirtualBoyGo, sideloaded on the same headset, shows the
same text, so the dialog does not resolve names for unknown-sources apps at
all. Do not spend time on it. Along the way the manifest gained
`com.samsung.android.vr.application.mode=vr_only`, which every other Quest app
checked declares and this one did not - it did **not** fix the dialog, and it
is kept only because it belongs in a VR manifest.

### 12.2 Dam crashes on load — stan tiles are still big-endian

Selecting Dam and launching crashes. Captured 2026-09-21 11:48 from
`adb logcat -b crash`:

```
signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x000000003413f2b3
  #00 stanLocusAddTileRoomIfNew+64
  #01 sub_GAME_7F0B1DDC+208
  #02 sub_GAME_7F0B21B0+120
  #03 stanTestVolume+108
  #04 getposstan+164
  #05 expand_09_characters+92
  #06 proplvreset2+3204
  #07 lvlStageLoad+1092
  #08 bossMainloop+1032
```

This is the stan (standing-tile) collision data, which §5 step 3 has listed as
unported from the start: the `T...Z` files are still cartridge big-endian and
nothing swaps them. The fault address is the tell - `0x3413f2b3` is odd, so it
was never a valid aligned pointer; it is byte-swapped data being walked as one.
Defect class A (endianness), and likely B as well where the structures hold
32-bit pointer slots.

Reference: Perfect Dark's port does the equivalent job in
`port/src/preprocess/filetiles.c`, which is in this tree and excluded from the
build. GoldenEye's stan format is not Perfect Dark's, so it is a reference for
*shape* - walk the file, swap each record's fields, fix up offsets - not
something to call directly.

### 12.2a Dam: what the probes ruled out (2026-09-21)

Two rounds of probing, both negative - recorded so they are not re-tried:

1. **pad->stan** was the first crash, and zeroing it in gevrConvertSetup fixed
   it. The PadID bounds probe never fired, so the guard index was always in
   range; that suspicion was wrong.
2. The next fault is `0xb4000071b4000077` (SEGV_ACCERR) in
   `sub_GAME_7F0B0914` at `stan.c:1365`, via
   `boundpads[getBoundPadNum(arg1->pad)]` in `domakedefaultobj`. The
   bound-pad probe **also** never fired, and the converter reports
   **368 pads, 96 bound pads** - so the array is not truncated and the index
   is in range. Not an out-of-bounds index.

Also verified with `offsetof`, not by eye: `PadRecord` and `BoundPadRecord`
host offsets match what the converter writes exactly (plink@40, stan@48,
bbox@56, strides 56 and 80).

Both halves of the fault value are the *high* half of a heap pointer (valid
ones here are `0xb40000712e79d4c0`), and it reproduces byte-identically across
runs. That is adjacent heap pointers being read as a record, from a fixed
place - so look for a field read at the wrong offset or a stale pointer, not a
wild index. `mStan` comes from `boundpad->stan`, which the converter now
zeroes and `init_pathtable_something` fills in; the next thing to check is
whether that resolver runs for **bound** pads before `domakedefaultobj` uses
them, since prop.c resolves pads and volumes in separate loops.

### 12.3 The shared menu background draws nothing (2026-09-21)

The mission select is black behind its text. The background is **not 2D art**:
`frontSetupMenuBackground` (front.c) builds a 3D wallet/folder scene and hands
it to `subdraw`. It is shared by **ten** menus, so this one defect blacks out
every post-file-select screen.

**The finding that matters:**

```
menubg: subdraw emitted 10 Gfx commands
```

Ten, for a model that converts to 276 blocks / 90 nodes / 46 display lists.
`subdraw` walks `mdl->obj->RootNode` and emits nothing. **The fault is in the
model node walk, not the camera, not the matrices, not the renderer.** That is
where the next session should start: instrument the `while (root != NULL)`
loop in `subdraw` (model.c:5135) - count nodes visited and log each node's
opcode - and find where the walk terminates.

#### Ruled out, with evidence - do not re-chase these

1. **Renderer state is correct.** A display-list dump taken *on the screen*
   shows `G_SETCIMG` width 440, `G_SETSCISSOR` (0,0)-(440,330), a full-screen
   `G_FILLRECT` (the black), 72 `G_TRI2`, 28 vertex loads, 64 textures and one
   `G_MOVEMEM`. Geometry reaches the renderer; nothing is being dropped and
   the viewport maths is right.
2. **`numMatrices = 1` is correct**, not a bug. The asset declares it:
   `MODELFILEHEADER(walletbond, 0, &SKELETON(walletbond), 0, 0x2B, 0x1,
   3504.53, 0, 0x54)`. `nintendologo` has the same value and renders.
3. **Block alignment does not shift `RootNode`.** The game computes it as
   `&Textures[numtextures]` with no alignment, while gevrModelConvert aligns
   every block to 8 - but for the real counts they agree exactly:
   `sizeof(ModelFileTextures)` is 16, so 43 switches + 84 textures gives 1688
   either way. Checked numerically, not by eye.
4. **"Models with switches are broken" is false.** `djbond` has 7 switches and
   renders (the gun-barrel Bond).
5. **Probably not defects at all:** only DAM and MULTIPLAYER showing is correct
   on a fresh save (gated on `get_highest_unlocked_difficulty_for_level >= 0`),
   and the vertical "PREVIOUS" is how GoldenEye draws it.

#### What is distinctive about this model

| model | switches | matrices | textures | renders |
|---|---|---|---|---|
| nintendologo | 0 | 1 | 1 | yes |
| goldeneyelogo | 0 | 1 | 2 | yes |
| legalpage | 0 | 1 | 5 | yes |
| djbond | 7 | 21 | 13 | yes |
| **walletbond** | **43** | 1 | **84** | **no** |

It is by far the most switch-heavy model in the front end - 43 switches and 46
display lists, i.e. mostly *selectable* parts (folders, portraits). If switch
resolution picks nothing, ten commands is exactly what you would get. That is a
hypothesis, not a finding.

#### Using the DL dump correctly

The marker is consumed at startup, so touching it before launch dumps the
legal page. Touch it **while the screen is up**, with the app already running:

```
adb shell "touch /sdcard/Android/data/com.gevr.port/files/gevr_dumpdl.txt"
```

Its budget is 900 commands (`gevrMaybeDumpDl`, gfx_pc.cpp) and it exhausts
them, so the dump only covers the start of a frame.

### 12.4 Node-walk trace prepared — 2026-09-21

New checkout confirmed clean at `08d8894`. Added a GEVR-only trace armed by
`frontSetupMenuBackground` for two once-per-second samples per process.
`subdraw` records root/runtime-data addresses, switch visibility and controls,
each visited node's opcode/parent/next/child before and after dispatch, and
its top-level command count. Detail is capped at 128 nodes per sample; the
summary counts all visited nodes. Traversal behavior is unchanged.

Correction to §12.3: ten top-level commands do not establish that the walk
produces nothing, since display-list calls can submit nested geometry. The
trace will test that claim directly. No new root cause or visual fix claimed.

Validation: `assembleDebug --console=plain` succeeded; `git diff --check`
passed; `adb install -r` returned Success. App was not launched automatically.
Next: user opens mission select in the headset, then inspect `menubg-walk:`.

### 12.5 Device trace disproves empty node walk — 2026-09-21

User screenshot: mission select still black behind text/cursors. Capture in
`C:/Users/Occor/AppData/Local/Temp/gevr-menu-walk.log` at 12:46:39 shows 26
visited nodes, visible switches restoring their child links, eight opcode-4
nodes each emitting two commands, and 17 commands total. These first samples
precede the settled mission-select state; later command totals fall to 11.
They disprove the general claim that this background's node walk does nothing.
The original 10-command observation cannot alone localize the defect.

The same capture records the known Dam crash at 12:46:45, fault address
`0xb4000071b4000077`, via `sub_GAME_7F04088C` / `domakedefaultobj`.

Next probe: G_NOOP tags scope renderer diagnostics to this background only,
once per menu per process. `menubg-rsp:` logs the first model/projection matrix,
eight vertices with clip-space coordinates, and triangle totals split into
clipped, culled and submitted. This avoids attributing cursor/text geometry
to the background. Fixed the existing log throttle to use sysGetMicroseconds:
osGetTime is N64 ticks, so dividing it by one million was not one second.
Build passed, installed successfully, no automatic launch. Visual fix pending.


### 12.6 Missing menu returns corrupt matrices — 2026-09-21

Background-scoped capture at 12:53:29: menu 7 loads M[0][0] = -18431.8
(instead of 0.25), and a corrupted projection. All 216 triangles are clipped;
zero culled or submitted. Menu 6's preceding sample had sane matrices and
54 submitted triangles. Evidence remains in the temp capture cited above.

`constructor_menu07_missionsel` falls off its end without returning its
updated display-list pointer. Disassembly of the installed pre-fix binary
shows it writes DL at [x29,-96] but returns an uninitialized [x29,-88]. The
caller appends scissor, full sync and end-list commands. Their word patterns
match the damaged matrices: ED scissor alters the projection and B8000000
end-list gives the model's -18431.75 when combined with its original fraction.
This explains why valid node traversal still produces a black screen.

Added `return DL;` in mission select and mission complete (the two Gfx-returning
functions in front.c with no return at all). Debug build passed; installation
returned Success. Post-fix disassembly returns the same DL slot that
frontDrawCursor updates. `git diff --check` passed. Renderer probes retained
for confirmation, user asked to relaunch and inspect mission select. No
claim that all ten menu screens or Dam are fixed; visual confirmation pending.

User subsequently confirmed mission select looks fixed. Dam still reproduces
the same fault. New report: system recenter via holding the right-controller
menu button leaves the cinema screen left of the current view.

### 12.7 The stan tile struct did not describe the stan data — 2026-09-21

The Dam defect is a host/cartridge layout mismatch in `StandTile`, found by
measuring rather than by reading.

`gevrConvertStan` (port/src/gevr_stage.c) deliberately leaves every collision
tile **at its cartridge offset and size** — an 8-byte header followed by
8-byte points — and only byte-swaps the 16-bit fields in place. It has to:
tile links are `(link << 3)` relative to `firstroom - 0x80`, and
`list_of_tilesizes` is `8 + 8*points` (`0x20` for three points, `0x58` for
ten). Its own loop computes `bytes = 8 + 8*points` from `read16(src+tile+6)`.

The host struct did not describe that data. `u32 id : 24;` followed by
`u8 room;` packs into four bytes on big-endian MIPS, but LP64 gives the
bitfield its own 4-byte unit and will not pack the trailing `u8` into it.
Measured with `offsetof`, not by eye:

| | room | mid | tail | points | sizeof |
|---|---|---|---|---|---|
| cartridge / N64 | 3 | 4 | 6 | 8 | 8 |
| host, before | 4 | 6 | 8 | **10** | **12** |

Every tile field read two bytes late, across dozens of sites in `stan.c`.

**Why that produces both Dam symptoms.** `tail.hdrTail.pointCount` landed on
the first point's `x`, a signed coordinate, so `(tail >> 12) & 0xf` is an
arbitrary nibble:

- `list_of_tilesizes[11]` is `0`, so any tile walk drawing nibble 11 never
  advances — a live-lock, which is the **black-screen hang** the user sees.
- Any other wrong nibble walks off the tile array into arbitrary heap.
  `&standTileStart[link]` compounds it: that idiom is `link * sizeof(StandTile)`
  and only agrees with the `(link << 3)` form used at stan.c:596, 2171 and 2459
  when `sizeof(StandTile)` is 8. It was 12.
- `tile->room` read the colour word instead, so room ids were wrong
  everywhere they are used — portals, AI, explosions.

Note which code was *not* wrong: `stanMatchTileName` reads a tile through
`StandTilePoint *`, which is 8 bytes with no padding, so it was always
correct. That is why the stan pointers looked plausible right up to the point
they were walked.

The fix spells the header bytes out under `GEVR` (`u16 idhi; u8 idlo; u8 room;`),
restoring room@3, mid@4, tail@6, points@8, sizeof 8. Verified with `offsetof`
in both configurations. `StandTile::id` has no readers; `StandFileTile` keeps
the bitfield and has no users at all.

This also supersedes the speculation in §12.2a that the fault value's two
pointer high-halves meant a misread record at a fixed offset. It was a heap
walk that had left the tile array.

Build succeeded, `git diff --check` passed, `adb install -r` returned Success.
**Not yet confirmed in the headset** — the hang and the fault are both
explained by this, but neither has been observed fixed on device. The
`dam-pad:` probes from the previous session are kept so the pad and stan
pointers are still logged if a fault survives.

#### The reusable part

`grep -n ': 24;' src/bondtypes.h` found only `StandTile` and the unused
`StandFileTile`. The general trap is **any bitfield immediately followed by a
narrower plain member** — MIPS packs them into one unit, LP64 does not. It is
silent: the struct compiles, the pointers look like pointers, and the damage
only shows when the data is walked. Add it to the §3 defect-class table.

## 13. The Dam load: nine defects, one class — 2026-09-21

This session took Dam from "crashes almost immediately in prop creation"
to "loads, spawns Bond, and dies in the first rendered frame". **Nothing
has been seen working in the headset.** The user tested after every fix
and never saw anything but a crash back to the Quest shell. Treat every
item below as *logged*, not *seen*.

### 13.1 What was actually wrong

Nine defects, found in this order. Each was proven from device evidence
or from `offsetof`/disassembly, never from reading alone — every time
this session reasoned from source instead, it was wrong.

| # | Defect | How it was caught |
|---|---|---|
| 1 | `StandTile` was 12 bytes with every field 2 late; the cartridge and `gevrConvertStan` both use 8 | `offsetof`, both configs |
| 2 | Prop-def header word-swapped as a `u32`, so type landed at byte 0 and the END record was never seen — the walk ran off the list | converter wrote 329 props, game reached cmd 536 |
| 3 | zlib's overlap guard compared pointers truncated to 32 bits | same room inflating on some runs, hanging on others |
| 4 | …and then the honest guard still fired, because `bgDecompress` passes disjoint buffers the original never had | the log the previous fix added |
| 5 | Player gait root node truncated by `(int)&player_gait_hdr` | fault address == the argument in the register dump |
| 6 | `standTileStart` truncated by `(u32)` in the locus walk | fault address was the low half of a pointer inside the stan buffer |
| 7 | `tileStack[39]` overrun; the function's own limit allows index 54 | stack protector, `__stack_chk_fail` |
| 8 | Bond's animation rwdata overlapped his own `Model` | `offsetof`: model 0x5d0–0x6d0, rwdata at 0x690 |
| 9 | `stanTileDistanceRelated` cleared a fixed 64 bytes over a 24-byte record, zeroing the caller's locals | tile valid at the call, NULL inside, same frame |

Plus, from a compiler-warning sweep and not yet exercised: four pointer
truncations (`cleanup_objects.c`, `propobj.c:8482`, `glass.c:305-306`,
`explosion.c:1726`) and the same byte-order defect as #2 in two of the
game's own prop walks (`cleanupObjects`, `setupFindObjForReuse`).

### 13.2 The one class behind almost all of it

Every defect except #2 and #7 is the same thing: **a 32-bit assumption
that was exact on the N64 and is lossy or wrong on LP64.** Three shapes:

1. **A pointer through a 32-bit slot** — `(s32)`, `(u32)`, `(int)`, or an
   `s32` variable holding an address. Items 3, 5, 6 and the sweep.
2. **A struct whose host size differs** — so a hardcoded byte count, a
   neighbouring buffer, or an array index lands somewhere else. Items 1,
   8, 9.
3. **A fixed count that was derived from the N64 size** — the 64-byte
   clear, the 16-word loop, `tileStack[39]`.

The useful generalisation: **wherever the original encoded a size, an
offset or a count as a literal, check it against `offsetof`/`sizeof` on
the host.** The compiler will not warn. Shape 1 it *does* warn about —
`-Wpointer-to-int-cast` and friends — and that sweep is in §13.4.

### 13.3 Where it dies now

```
lvlRender -> bgRoomVisibilityRelated -> bgDetermineVisibleRooms
          -> bgProcessNextQueuedPortal -> sub_GAME_7F0B7F84
```

`bg.c:4026`, `*((u8 *) i) = depth`, where `i` is an `s32` holding
`&D_800442FC[portalnum]`. Fixed in the last commit but **not tested**.
That is the next thing to check.

Note this was in the warning sweep and I dismissed it as a dead
artifact, because the `if (i);` next to the assignment made it look
unused. It is used eighty lines later. When triaging that sweep again,
follow the variable, do not read the adjacent line.

### 13.4 Still open, with what is known

- **12 `stanwalk:` bad tiles every run.** Pads resolving to garbage
  stan pointers. The range check added in `sub_GAME_7F0B0914` bails
  safely so they no longer crash, but they are wrong and unexplained.
  Item #9 fixed the *player's* tile going NULL; these are different.
- **`animFlipFlag` and `field_5C0` write into Bond's `Model`.**
  `struct player` spells the embedded Model as `Model *model` plus a run
  of `field_*` placeholders covering its interior. Nearly all are
  unreferenced, but those two are read and written by `bondhead.c` and
  both fall inside the host Model's footprint (0x5d0–0x6d0). This is
  gameplay-path corruption and will matter now that gameplay runs. The
  clean fix is to declare the embedded `Model` properly and map those
  two names onto real Model fields; it was left alone because it needs
  its own evidence and is bigger than any crash so far required.
- **99 remaining pointer-truncation warnings** in
  `scratchpad/trunc.txt`. Most are benign — `bg.c`'s `csize` arithmetic
  subtracts two segment-tagged offsets so the truncation cancels, and
  `propobj.c`'s `(u32)rodata->Primary & 0xffffff` is deliberate. But #6
  and the portal slot both came out of this list, so it is worth
  re-triaging with §13.3's lesson in mind.
- **The recenter fix from the start of the session is still unverified.**

### 13.5 Debug hooks added this session — all owe removal

`dam-pad:` (prop.c, propobj.c) · `stanwalk:` bounds + range check, and
`stanlocus:` NULL guard (stan.c) · `bggdl:` (bg.c) · `setupwalk:`
(gevr_setup.c) · `bondanim:` (initBondDATAdefaults.c) · `spawn:`
(bondview_r.c) · `move:` (bondview2.c).

The `stanwalk:` and `stanlocus:` guards also *change behaviour* — they
return early instead of walking a bad tile. Keep them until the bad
tiles in §13.4 are explained, then remove the guard with the probe.

### 13.6 Method notes for whoever picks this up

- **`adb logcat -d` beats a background pipe.** A long-lived
  `nohup adb logcat > file` died mid-session and produced 3.6 MB with
  zero app lines. The device ring buffer had everything.
- **The watchdog marker gets a stack out of a hang**:
  `adb shell touch /sdcard/Android/data/com.gevr.port/files/gevr_watchdog_kill.txt`
  while it is hung. That is how the zlib hang was localised.
- **Disassemble rather than infer which pointer is null.**
  `llvm-objdump -d --disassemble-symbols=<fn> -l` against
  `android/app/build/intermediates/cxx/Debug/*/obj/arm64-v8a/libgevr.so`,
  then add the `+N` from the tombstone to the symbol's base. This
  settled items 5, 6 and the current one in minutes each.
- **Measure structs with a compiled probe**, not by reading comments.
  The comments carry N64 offsets and are right about the N64.
- Three times this session a value was valid at a call and wrong inside
  it. Every time, the cause was something writing memory it did not own
  — not a failure to set it. If the source says a pointer cannot be
  null and it is, stop reading and go looking for the writer.

## 14. Dam plays its intro; and the reference that should have been used

**The user's question mid-session was the most valuable thing in it:** why
rediscover all this when ports exist? The answer was that we were reading the
wrong references, and correcting that changed the rate of progress
immediately. See §14.3 before doing anything else.

### 14.1 What the user now sees

Selecting Dam plays the intro: the dam renders textured (wall, towers,
railings, sky), the camera moves, and on the run before last both captions
appeared in order - "Nine years ago", then "Byelomorye Dam, Arkangelsk,
USSR" - matching the original. The last run reached **first-person Bond
view** and crashed there.

Two visible defects, both open:

- **Stray flickering text about a satellite** during the captions. Not
  chased at all. Readable text from elsewhere in the game plus flicker
  suggests a text index or a clobbered text buffer rather than a renderer
  fault. The reference has a cluster here (`front.c`, 30 sites; its D295 is
  a `strcpy` into `char difficultytext[4]` whose NUL lands on an adjacent
  coordinate - exactly this shape).
- **The captions did not appear on the last run.** They did on the run
  before, with the intro camera pinned to the same index, so this may be a
  regression from the swirl-camera commit (12ac680) rather than variance.
  Check that first if the captions stay missing.

### 14.2 Where it dies now

```
lvlRender -> maybe_mp_interface -> gunUpdateAndFireBothHands
          -> gunUpdateAndFire -> modelInit -> modelInitRwData
```

Fault address `0x00000001acfad174`. **The reference names this one: D102.**

> the 1P weapon Model and its RW-data pool were punned onto
> `hand->field_B68` / `hand->modeldatas`; on x86-64 `struct Model` (0xE8) is
> too big for that layout and `modelInit()` aliases `objinst->datas` onto the
> pool base.

Their fix adds dedicated `weaponModel` / `weaponRwPool` fields to `struct
hand` in `bondview.h` and routes both through macros
(`HAND_WEAPON_MODEL` / `HAND_WEAPON_RWPOOL`, `gunfire.c:43-52`). Ours is
0x100, not 0xE8, so the same pun is worse here. **That is the next fix, and
it is already written down.**

### 14.3 The reference, and how far to trust it

`https://github.com/jkdansereau/goldeneye-pc-port` - the same GoldenEye
decompilation taken to 64-bit, **429 `#ifdef PORT` sites** and a 202-entry
findings ledger (`docs/dev/findings.md`, plus the class document
`docs/porting-notes.md`). Cloned locally as `../gepc-ref`, reference only;
nothing is vendored, and its `port/` host layer is unused because ours is
Perfect Dark's. Indexed for working through in
[docs/gepc-port-worklist.md](docs/gepc-port-worklist.md).

Why the other references do not answer this:

- **goldeneye-decomp** targets the N64. Every defect in §13 and here exists
  only because the same source runs on 64-bit little-endian. On its own
  target they are not bugs, so it never had to solve them.
- **Perfect Dark's port** solved the class, but for PD's structures, and only
  its host layer is vendored here.

**The critical caveat, learned by finding fixes it does not have:** that port
emulates N64 RDRAM in a low pool (`0x70xxxxxx`), so a pool address survives a
32-bit round trip there, and it builds with `-fno-stack-protector`. Both of
this session's last two crashes were sites it leaves untouched for exactly
those reasons - the `(u32) g_IntroSwirl` truncation and `f32 mtx[15]`.

> **This port has a superset of that port's defects. Silence in its ledger is
> not evidence a site is clean.**

Conversely it is useful for ruling things out: D57 and D92 are already handled
here, which saved chasing the rwdata pools.

### 14.4 Fixes since §13

| what | source |
|---|---|
| lookat and projection matrix kept as pointers, not `s32` | ours |
| modelview stack floored so `G_MTX` cannot write at index -1 | ours |
| held-item buffer address out of an `s32` in `solo_char_load` | ours |
| prop room list clamped to the four bytes it has (3 writers) | ours |
| second animation not blended when absent | ours |
| **cutscene body model and held weapon reserve their real size** | **D243** |
| `playerTick`'s matrix local given its sixteenth float | class D8 |
| swirl table truncation + point buffer span | **D189**, both unfixed there |

### 14.5 Debug hooks added this session

`introcam:` - **pin the intro camera** by writing an index to
`/sdcard/Android/data/com.gevr.port/files/gevr_introcam.txt`; currently `0`.
Dam picks one of six at random, which silently made several "it crashed
again" rounds different crashes. Delete the file for stock behaviour.

Also `noanim2:` / `nullanim:` (model.c) and the earlier set in §13.5. The
`stanwalk:`, `stanlocus:` and anim guards change behaviour, not just logging.

### 14.6 Still open

- **D102 weapon-model pun** - the live crash, fix already described above.
- **Stray satellite text**, and the missing captions on the last run.
- **`G_POPMTX on an empty modelview stack`**, once per run. The clamp stops
  it corrupting `tex_upload_buffer`, but GoldenEye's display list genuinely
  pops more than it pushes and nobody has found why.
- **12 `stanwalk:` bad tiles per run.** Pads resolving to garbage stan
  pointers. Guarded, unexplained.
- `animFlipFlag` and `field_5C0` in `bondhead.c` write inside Bond's `Model`
  (see §13.4).
- The recenter fix from the start of §13 is still unverified.

### 14.7 Method that is working

Read §13.6 first; it all still applies. Added since:

- **Work the ledger, not the tombstone.** Two crashes running were resolved
  without a device round-trip to diagnose, because the class was already
  written down.
- **`adb logcat -G 16M`.** The default 256 KiB buffer rolls before a test can
  be read; several dumps this session were empty of app lines for that reason.
- **Pin the intro camera** before concluding two crashes are the same bug.


## 15. Dam repair pass: built and installed, not yet verified in headset

Changes in the current working tree:

- D102: `struct hand` owns `weaponModel` and `weaponRwPool[192]`. All first-person
  gun model references use them; `render_pos` is explicitly assigned alongside
  `mtxlist` because the old code relied on their overlapping storage.
- D98: `initBONDdataforPlayer` now allocates `sizeof(struct player)` on GEVR,
  replacing the retail 0x2A80-byte allocation. This is essential with expanded
  host records and the added weapon storage.
- Gait model: the player's `model` is now an actual Model. `bondhead.c` uses its
  `gunhand` and `animframe1` fields (retail offsets 0x24 and 0x28), rather than
  the `animFlipFlag` and `field_5C0` placeholders. The existing separate gait
  RW pool remains. Legacy placeholder fields are retained but unused here.
- Music: `sub_GAME_7F0C0BF0` lacked `return get_mTrack2Vol()`. Mission transitions
  passed an undefined value to the music volume setter. Return restored.
- The attached video shows genuine mission failure messages, not unrelated
  satellite text: satellite link destroyed, main computer damaged, objective B
  failed. The setup converter handled TAG ID/OffsetToObj as one u32; they are
  separate u16/s16 fields. Splitting the conversion preserves tag lookup and
  signed relative object references. This is a concrete cause of false failures;
  device verification is still needed.
- Sky: our Fast3D explicitly drops G_RDPHALF_* commands, while the unchanged GE
  sky emitted its triangles through those commands. Adapted the reference's
  D176/D227/D245 geometry replacement, including perspective interpolation and
  shared texture-coordinate scaling per fan. Source credit and MIT notice are
  in CREDITS.md and docs/gepc-reference-LICENSE.txt. Its host layer is unused.
- Captions: synthetic conversion checks preserve both camera language IDs and
  the 56-byte host stride. Added `caption:` logs at both frozen-camera triggers
  with text, queue count, timer and hidden flag. No confirmed caption fix yet;
  neither the swirl change nor allocation corruption is proven to explain it.

Validation: `python tools/gevr_setup_probe.py` passes on Windows x64 against the
production converter using synthetic big-endian tag/camera records (no ROM).
Android `assembleDebug` succeeds with existing compiler warnings. The final APK
was installed successfully using adb; no blind launch loop was run. The device
log ring is 16 MiB. User was asked to launch Dam and report gameplay, music,
sky stability and both captions. None of these changes is marked seen yet.

The initial APK installed during this pass preceded the player-allocation fix;
it was superseded by a second install. Use the latest APK. Local build and
pre-fix device logs, plus video contact sheet, are under scratchpad/.


## 16. User confirms music, sky, and false objective text fixed

The user reports Dam music plays correctly, the rogue text is gone, and the sky
no longer flickers. Both bottom captions remain absent, and it crashes at Bond's
view. Fresh log: scratchpad/dam-latest.log, process 23769 at 22:09:00.

The D102 crash is passed. New crash is microcode_generation_ammo_related+372,
called by generate_ammo_total_microcode. Fault address 0x02000C8C; tconfig is
0x02000C84 (the cartridge 9mm icon address). image_bank.c already documented that
these literal addresses still needed host resolution.

Implemented texGetAmmoIcon for all 14 cartridge ammo icon identities, returning
the real compiled sImageTableEntry arrays. Both HUD hands and the watch ammo
screen use typed pointers and ->width instead of byte[4] (the retail layout).
set_rgba_redirect_generate_microcode now returns the updated display-list pointer.
Build succeeds and APK install succeeds. This new fix is not headset-verified yet.

No caption: logs appeared on the failed run, so neither frozen-camera text trigger
was reached. Added intro-mode: transitions with timer, camera pointer and ramrom
flags, plus intro-skip: input edge and timer. Input logs show Z_TRIG near loading;
this suggests early skipping but does not prove it. Asked user to release all
buttons after selecting Dam; they said they will test again. Do not suppress
skipping or claim a caption fix without checking this evidence.


## 17. Movement works; shooting crash and first-frame intro skip

User confirms Bond can move, then crashes as soon as they shoot. Log captured in
scratchpad/dam-shoot.log: process 25116, crash at 22:14:08 in texSelect+344 from
bullet_spark_render+1752. The sprite code still used frame*12 for sImageTableEntry
arrays. Host entries have widened pointers; later frames read a bogus texture
address. Changed all five accesses to one typed frameimage pointer. Also applied
D219's adjacent fixes: zero-initialize the vertex template instead of reading a
Vtx from a u32 global, and read color members instead of retail byte offsets.

Caption evidence is now decisive: intro-mode 0 -> 1 at 22:13:53.413, then
intro-skip timer=1.0 buttons=2000 previous=0000 on the first frame, followed by
fade and swirl. The new player's button history starts at zero while menu trigger
input carries through loading. On the initial frozen-intro tick (timer==0), seed
oldbuttons from the current sample. Held selection input no longer creates a
false rising edge; release/repress on subsequent frames still skips normally.

Android assembleDebug passes; new APK installed successfully. Shooting and
captions need user confirmation on this APK. Music, sky, rogue-text removal,
first-person HUD, and movement have passed the previous failure points.

## 18. Dam is playable — 2026-09-22

The user played Dam: moved, shot, killed guards, took damage, picked up an
AK, with music and SFX. That is the first time gameplay has run. It is a
long way from right, and everything below is what is wrong with it.

### 18.1 How it got here from §14

| what | source |
|---|---|
| Bond's `Model` embedded in `struct player`, allocation sized for it | closes §13.4 |
| 1P weapon `Model` + rwdata pool out of the `field_B68` pun | D102 |
| Gun render data set explicitly, not from the global template | D215 |
| Guard attack animation table indexed without truncation | D94 |
| Equipped-weapon reads via `WeaponObjRecord::weaponnum` | D119 |
| Sky renderer | D176/D227/D245 |
| `get_mTrack2Vol()` returns its value | D6 class |
| Object tag id/offset converted as separate halfwords | ours |
| Held selection trigger no longer reads as an intro skip | ours |
| Texture arena rounded to 8 | D217 ① — **did not fix the textures** |
| `vtxstore_allocate` returns a real pointer | ours |
| `sub_GAME_7F09BAC4` reads the model correctly, patches a full pointer | D255 + ours |
| **`chr.c` actually declares `vtxstore_allocate`** | ours |
| Throw macros use real fields instead of raw offsets | D115 |
| `hand.field_A48` is an `ALSoundState *` | D208 |

Two of those are worth reading the commits for:

- **The implicit declaration.** Fixing `vtxstore_allocate`'s signature did
  nothing, because `chr.c` never included `vtxstore.h` and was calling it
  through an implicit declaration returning `int`. The truncation was at the
  *call site*. Worse, an incremental build does not warn: nothing rebuilt
  `chr.c`, so nothing complained. `touch` the file and rebuild to see it.
- **D115.** The throw macros were raw byte offsets into `struct player`,
  N64-sized, so `matrix_4x4_copy(THROWMTX, ..)` was scribbling 64 bytes of
  live state **on every shot**. It had to land before D208, which changes
  `sizeof(struct hand)`.

### 18.2 Textures — the probe has the answer, nobody has acted on it

**Correction (2026-09-22 follow-up): the diagnosis below is not established.**
`rdp.palette` is a fixed `uint16_t[256]` array emulating TLUT storage; its
constant address is expected. `load_tlut` copies new entries into this array
and separately updates `rdp.palette_addrs`, the source pointers used in the
cache key. The old probe printed the array address, not the cache-key pointers.
The probe now logs both source pointers and a hash of decoded palette contents,
still capped at 24 distinct CI textures per process. A new headset capture is
needed. Address-based keys also do not rule out content reuse at one address;
neither a cache defect nor a TLUT loading defect is proven by the old logs.

The AK draws correctly and the PP7 does not; the HUD, bullet-impact sparks,
blood and ground weapons are also wrong. The `citex:` probe added in
`gfx_pc.cpp` logs each distinct colour-indexed texture once. Twenty entries
from one session, and **every one has the same palette pointer**:

```
citex: addr=0xb400007ba940e5a0 siz=1 ... pal=0x7bb390e040 32x32 ... palfmt=49152
citex: addr=0xb400007ba9413bc8 siz=0 ... pal=0x7bb390e040 16x16 ... palfmt=49152
citex: addr=0xb400007ba9447c70 siz=1 ... pal=0x7bb390e040 32x32 ... palfmt=32768
...
```

Different addresses, different sizes, different palette *formats* - 32768 is
`G_TT_RGBA16`, 49152 is `G_TT_IA16` - and **one palette address for all of
them**. CI textures each need their own TLUT. That is the defect, and it
explains why only some assets look wrong: the ones that happen to match
whatever single palette is loaded look right.

Ruled out already, by evidence:

- D217 ① (arena 8-byte alignment) is in the tree and changed nothing.
- D217 ② (content-hash cache key) cannot apply — `TextureCacheKey` already
  includes `palette_addrs[2]`.

So start at whatever sets `rdp.palette` - the `G_LOADTLUT` / `G_SETTIMG`
path in `gfx_pc.cpp` and whoever feeds it - and find out why it never
changes. One entry in that log is also plainly wrong on its own terms:
`1x1 line=16 bytes=1024`.

### 18.3 Controls are badly wrong, and two buttons crash

Reported from the headset:

- **Left stick does nothing.** `input.c` says so in a comment: GoldenEye's
  front end consumes the primary N64 stick, so the Perfect Dark gameplay
  mapping uses the secondary one. In gameplay the left stick is still wired
  to the menu stick.
- **Right stick behaves like the C-buttons** — strafe and forward/back.
- **Clicking the left stick looks up. Clicking the right stick crashes.**
- **No button for crouch, and none for the door switch**, so the level
  cannot be finished.
- **Grip on either controller crashes.** Grip is mapped to `CONT_R`, which
  is aim — and the crash is in the aim path, three times in one session:

  ```
  lvlRender -> maybe_mp_interface -> gunDrawSight -> texSelect -> texSetRenderMode
  ```

  Fault addresses are wild full-width garbage (`0x9e4141f7a93fb9f8`,
  `0x4d0bfd07ad540508`, `0x8886afd7acb59268`) — not a truncation, so
  something is handing `texSelect` a bad texture record.

- **Menu button (pause) crashes** after one frame of the watch animation:

  ```
  bondviewWatchAnimationTick -> bondviewStepWatchAnimation
    -> modelSetAnimFrame2 -> modelSetAnimFrame -> modelConstrainOrWrapAnimFrame
  ```

  Fault address 4, so a null animation - the same shape as the `anim`/`anim2`
  crashes in §14, and the watch model is a separate `Model` at
  `g_CurrentPlayer + 0x230` reached by a hardcoded offset in `bondview2.c`.
  That offset is N64-sized and is a strong first suspect, being the same
  defect as D115.

The mapping itself lives in `port/src/input.c` around line 885 and in
`port/vr/vr_input.cpp`. Per the standing rule, port GEVR's control scheme
rather than inventing one.

### 18.4 Also seen

- Bullets pass through the guard tower glass without breaking it.
- Overall level brightness is too high. The dark-lighting item in §13.4 was
  about the Nintendo logo and characters; this is the opposite and new.
- The HUD ammo counter draws with wrong textures.

### 18.5 Still open from earlier sections

`G_POPMTX on an empty modelview stack` once per run (clamped, unexplained);
twelve `stanwalk:` bad tiles per run (guarded, unexplained); the recenter
fix from §13 still unverified.

### 18.6 Reading the logs at all

Our own probes were emitting several thousand lines per level load and
rolling the ring buffer past every crash — two reports in §14 had no
tombstone because of it. `dam-pad:`, `setupwalk:` per-record and `bggdl:`
are removed. Keep new probes bounded, and use:

```
adb logcat -G 16M
adb logcat -b all -d > capture.log
```

## 19. IA16 palette decoder fixed — 2026-09-22

The corrected probe capture (scratchpad/palette-test.log, PID 25755 at
06:05:55–06:06:33 device time) shows distinct palette source pointers and
content hashes. This disproves the claimed single-source-palette diagnosis.

A concrete defect remained in palette_to_rgba32: after load_tlut's PD_BE16,
IA16 intensity is the high byte and alpha the low byte. We decoded them in
reverse. For example 0x58ff became white with alpha 0x58 instead of opaque
grey. Direct IA16 decoding already uses the correct order. The reference
port documents this exact correction as D228; credit added to CREDITS.md.
Only the IA16 decoder was changed; RGBA16 and cache behavior are unchanged.

Validation: tools/gevr_palette_probe.py compiles the production decoder and
checks all 65,536 IA16 entries against cartridge byte order, plus all 65,536
RGBA16 entries. Pass. Android assembleDebug passes and adb install -r reports
Success. This is a rendering fix, unlike the preceding diagnostic-only APK.
Visual acceptance of PP7, HUD and effects remains pending; do not claim all
reported texture issues are resolved until the user checks. The aim/watch
crashes and control mapping remain open.

## 20. Texture reference audit after D228 failed visual acceptance

User reports textures remain bad. Compared the reference renderer directly,
not just PORT guards in the worklist. Findings and dispositions are recorded
in [docs/texture-port-audit.md](docs/texture-port-audit.md).

Applied D74 load preservation, RC2 base-image sizing, D161 CI-without-TLUT,
and D217 palette-content cache identity. Also reconciled all native upload
dimensions with UV normalization and removed the CI4 rectangle dimension
override after row unpacking. Kept our working TMEM unswizzle; do not apply
the reference's engine-side swizzle no-op as well.

Both production-code synthetic probes pass; Android assembleDebug passes;
adb install -r reports Success. This replaces the IA16-only APK. Headset
verification of PP7/HUD/effects remains pending. No blind launch performed.

## 21. Non-VR Quest controls and aim/watch crash fixes

User confirmed only HUD bullet-ammo texture improved; PP7 (called pp9 in
the report), smoke, initial impacts and other textures remain wrong.
Priority shifted to controls. User explicitly clarified: gepc-ref parity in
Quest screen mode first, tracked VR controls and full immersion later.

Applied reference D137: gunDrawSight now accepts Gfx** and uses a Gfx* local
through texSelect/display_image_at_position, including the header declaration.
Applied D140: watch Model plus 192-word RW pool are real player members;
initialization no longer uses player+0x230/+0x2ec/+0x220. All old watch scale,
animation-frame and render-matrix aliases now access the Model members.
Applied D191: bondviewSelectCuff indexes ModelNode pointers at host stride.
Player allocation already uses sizeof(struct player).

Screen input now emits stock N64 actions, clearing inherited PD bindings and
unused secondary axes. Life initialization selects native 1.2 Solitaire.
Left stick = C-direction movement, right = analog look. Right trigger fires;
either grip aims; B/X uses/reloads; A/Y cycles weapons; left Menu opens watch.
Native crouch/stand uses grip + left-stick down/up. Left stick navigates
title/watch menus. Stick clicks emit no N64 actions (existing app recenter
handling is separate). Movement is digital, matching native C-buttons.

tools/gevr_controls_probe.py compiles production mapping and sight code with
synthetic input: movement/look, release, menus, ignored clicks and a >32-bit
display-list cursor all pass. Android assembleDebug passes with existing
warnings. adb install -r reports Success. No launch was forced. Aim/watch and
controls remain pending headset acceptance, not claimed crash-free.

## 22. Watch/post-mission pointer slots, and the texture-audit revert

User report against the §21 APK: controls usable but right-stick pitch the
wrong way round; aim reticle correct at level start then degrading into a
translucent garbled square; crash on "next" from the post-mission screen;
crash partway through the watch raise animation; file-select portraits
"cut in half" (a regression, they were fine before).

### 22.1 The file-select regression came from our own texture audit

§20 replaced this renderer's UV normalization in `gfx_sp_tri1` — which
derived tex_width/tex_height from the *loaded block* (`orig_size_bytes /
line_size_bytes`, the scheme gepc-ref also uses) — with an invented
`gfx_native_texture_dimensions()` helper that returns the *SETTILESIZE*
window for every tile wider than 1. Upload and normalization then disagreed
by the row padding, which is exactly a texture drawn at half width.

That helper is gone. Uploads use SETTILESIZE dimensions and the draw
normalizes against the loaded block again, as before §20. The CI4 rectangle
branch is restored. Kept from §20: D228 IA16 channel order, D161 CI without
TLUT, the D217 palette-content hash in the cache key (now using SETTILESIZE
dimensions), TLUT-mode changes marking bindings dirty, and D74's preserved
LOADBLOCK sources. Not verified on device yet — the user has to look.

Lesson, again: gepc-ref had a coherent scheme here. Half-adopting it and
inventing the seam is worse than either side.

### 22.2 More 32-bit slots holding 64-bit pointers

Found by grepping the full build log for `-Wint-conversion`,
`-Wpointer-to-int-cast` and `-Wimplicit-function-declaration` instead of
guessing. Fixed:

- `draw_current_hand_item_and_ammo` (options.c): `s32 sp7C/sp78` held the
  Bank Gothic font and fontchar pointers, then passed them to
  textMeasure/textRender. This is drawn as the watch comes up — the most
  likely cause of the watch crash.
- `draw_watch_inventory_page`: same for `pFontFile2`/`pFontChars2`.
- `draw_abort_cancel_confirm`: `sp54/sp50/sp4C` held the abort/confirm/cancel
  strings from langGet.
- `watchRenderControllerOpaque` was called through an `(s32)` cast on both
  the single- and dual-controller paths.
- `frontGetPlayersFavoriteWeaponInHand` returned `int` and mpmenu.c called
  it blind; front.c:7369 strcpy'd the result. That is the post-mission
  "next" crash. Return type fixed and the prototype added to
  src/gevr_implicit_protos.h.
- `struct player.ptr_text_first/second_mp_award` were `s32` fields storing
  langGet results; mpmenu's rank lines parked two more in `s32 q`/`h2`.
- `constructor_menu16_nocontrollers` (front.c): `s32 text`.

A sweep of all 247 implicitly-declared functions against their definitions
found no other pointer-returning one. The `uintptr_t` warnings around
`OS_K0_TO_PHYSICAL` are benign here: our `osVirtualToPhysical` is the
identity and returns the full pointer.

### 22.3 The systemic fix we have not done: gepc-ref's dram.c

gepc-ref keeps `s32 pFontFile` and friends *as s32* and works, because
`port/src/dram.c` maps 8 MB of "N64 DRAM" twice — a s32-safe view at
0x70000000 holding all game RAM, and a KSEG0 mirror at 0x80000000 for code
that rebuilds pointers with `offset | 0x80000000`. Every mempool pointer is
then 0x70xxxxxx: positive as s32 and lossless through every 32-bit slot in
the decomp. Their header shims make PHYS_TO_K0 the identity and
OS_K0_TO_PHYSICAL a small offset so fast3d's seg_addr resolves GBI w1 words.

We have no equivalent; our allocations are ordinary heap pointers, so every
one of these slots is a landmine and we have been defusing them one at a
time for several sessions. Porting dram.c (fixed mapping on arm64, mempool
carved from it, the two header shims) would retire the whole class. That is
the next big piece of work, and it is a port, not an invention.

### 22.4 Stale patched texture ids across a stage reload

texReset patches image ids into the compiled `globalDL_0x***` display lists
and `s_*images` tables in place. gepc-ref never does: it allocates a fresh
copy of the Globalimagetable segment from MEMPOOL_STAGE each texReset,
romCopies into it and patches *that* (`globalbank_rdram_offset + GIMG_OFF(sym)`).
Re-running our version over already-patched data leaves dead pointers —
which fits a reticle that is right on the first load and wrong later.

`gevrResetStaticTextureIds()` (assets/oddtextures.c, called at the top of
texReset) restores every static Gfx list and image table to its compiled
contents first, giving the same fresh-copy invariant without the segment
copy. Kept. Whether it is enough for the reticle is unverified.

### 22.5 Look inversion

`set_cur_player_look_vertical_inverted(1)` now runs once per process in
init_player_BONDdata instead of on every player init, so the watch's
Control option and a loaded folder's saved setting still win. Note
bondview2.c:4865 reads it inverted: `invertPitch = get_..._inverted() == 0`.

Android assembleDebug passes; `adb install -r` reports Success. Nothing in
§22 is verified on the headset. The reticle, PP7/smoke/impact textures and
the remaining texture complaints from §20/§21 are all still open.

## 23. D74 crashed the Dam on load — reverted

The §22 APK would not start the Dam at all. Tombstone (pid 12567, SDLThread,
SIGSEGV SEGV_ACCERR, fault addr on a page boundary), symbolized against
android/app/build/intermediates/cxx/Debug/*/obj/arm64-v8a/libgevr.so:

```
#00 import_texture_ci8            gfx_pc.cpp:922
#01 importTextureNative           gfx_pc.cpp:991
#02 import_texture                gfx_pc.cpp:1151
#03 gfx_sp_tri1 -> gfx_sp_tri4 -> gfx_run_dl -> gfx_run
```

`import_texture_ci8` reads `addr[i]` for `i < width * height`, sized from
SETTILESIZE. §20's D74 adoption removed the LOD arm of the fallback in
`import_texture`, so a detail tile kept its own (smaller) LOADBLOCK source
while the importer still read a full tile's worth — straight off the end of
the mapping. The original fallback existed to give LOD tiles a source big
enough for that read.

The LOD condition is back. D74 is only safe with gepc-ref's whole scheme,
where the loaded block sizes the read as well as supplying it; taking half
of it is worse than taking none. Same mistake as §22.1, one function along.

`port/fast3d/gfx_pc.cpp` now differs from 7bea347 only by D228 (IA16 channel
order), D161 (CI with TLUT disabled decodes as intensity), the palette
content hash and the extra texture-cache key fields, plus the citex probe.
All of those are decode- or cache-identity changes; none of them resizes a
read.

User confirms the file-select portraits are fixed by the §22.1 revert.
Everything else from §22 is still unverified on the headset.

### 23.1 Symbolizing a tombstone

```
adb logcat -b all -d > scratchpad/capture.log
grep -n "crash_dump64: performing dump" scratchpad/capture.log | tail -1
llvm-addr2line -C -f -e android/app/build/intermediates/cxx/Debug/*/obj/arm64-v8a/libgevr.so 0x<pc>
```

The `pc` values in the backtrace are already library-relative; ignore the
`offset 0x1258000` the APK line reports. llvm-addr2line lives in
$ANDROID_SDK/ndk/*/toolchains/llvm/prebuilt/windows-x86_64/bin.

## 24. The Dam crash was gevrResetStaticTextureIds — removed

Reverting D74 (§23) did not fix it; the same tombstone came back, symbolized
to the same `import_texture_ci8` / `gfx_pc.cpp:922` read of `addr[i]`.

The renderer was not the cause. Proof by inspection of the helper §22.1
removed: for any tile with `width > 1 || height > 1` it overrode its computed
values with the SETTILESIZE ones, so it and the reverted code produce
*identical* read sizes. It differed only for the 1x1 sentinel, where it read
*more* (16x64 from a 1024-byte block, versus one texel). Removing it cannot
make a read run off the end.

What actually changed under the Dam was §22.4. `gevrResetStaticTextureIds()`
restores every `s_*images` table and `globalDL_0x***` list to its compiled
image ids at the top of texReset — but texReset only re-resolves the 17
global display lists, `genericimage`, the 6 `explosion_smokeimages` and the 5
`scattered_explosions`. Everything else (impact, flares, the ammo icons,
crosshairimage, monitor, skywater, mainfolder, the mp tables) is resolved
lazily by whoever draws it. Blanking those back to `0xabcdXXXX` ids mid-run
leaves anything already resolved during the title or file select pointing at
a raw id, which reaches the renderer as a texture source and reads off the
end of whatever it lands in.

It is also the one change in §22 that had never run on hardware: the build
that introduced it (§21's tail) was never installed, so the user's last good
Dam was the APK before it.

assets/oddtextures.c, assets/oddtextures.h and src/game/image_bank.c are
back at 7bea347. The stale-patched-id problem it was written for is real and
still open — see §22.4 — but the fix for it is gepc-ref's scheme: allocate a
fresh copy of the Globalimagetable segment per stage and patch that
(`globalbank_rdram_offset + GIMG_OFF(sym)`), so nothing is ever restored
underneath a consumer. Not a blanket restore of tables texReset does not own.

Kept from §22: the pointer-slot widenings. Two §21-era tombstones confirm
they were aimed at real crashes — `strcpy` from
`constructor_menu0D_missioncomplete` (06:43) and `textMeasure` from
`draw_text_q_watch_v201_beta` (06:47).

Build passes, `adb install -r` Success, and the app launches and stays up.
Whether the Dam loads is for the user to check; I cannot drive the menu.

## 25. The real Dam crash: a tile window with lrs < uls

§23 and §24 were both wrong guesses. Reverting D74 did not fix it, removing
`gevrResetStaticTextureIds` did not fix it, and the dimension helper §22.1
removed provably could not have caused it (for any tile with `width > 1 ||
height > 1` it overrode its computed values with the SETTILESIZE ones, so it
and the reverted code read exactly the same bytes).

Driving the headset from the PC (see §25.1) made it reproducible here, and a
bounded probe in `import_texture_ci8` named it in one run:

```
ci8-oob: tile=1 65515x65500=4291232500 src=1024 orig=1024 line=16 fline=1024
ci8-oob: tile=1 65514x65500=4291167000 ...
ci8-oob: tile=1 65514x65499=4291101486 ...
```

65515 is -21 and 65500 is -36, drifting one texel per frame with a scroll.
`gfx_dp_set_tile_size` computes `width = (lrs - uls + 4) / 4` into a u16, so
a window whose lower edge sits *above* its upper edge wraps to ~65500. The
importers then read `width * height` bytes from a 1024-byte source.

The emitter is GE's own water/sky quad: sub_GAME_7F09343C binds tile 0 and
tile 1 to the same TMEM image and offsets tile 1's uls/ult (90,150 in
quarter-texels) past lrs/lrt to move its sample point — see the comment
above `skyPortBeginFan` in src/game/sky.c. Our `gfx_dp_set_tile_size` is
byte-identical to gepc-ref's, so the renderer is faithful; the wrapped
extent is simply something it was never asked to survive.

`import_texture` now detects `lrs < uls || lrt < ult` and sizes that tile
the way gepc-ref sizes every texture — from the loaded block
(`line_size_bytes` and `size_bytes`). Sane tiles keep their SETTILESIZE
dimensions untouched.

Why it only appeared now: before §20 the texture cache key held no
dimensions, so this quad was imported once and served from cache forever
after. §20 added width/height and the palette hash to the key, the drifting
width made every frame a miss, and each re-import read further past the end
until it hit an unmapped page. The bug was always there; the cache hid it.

Verified here: the Dam loads, runs at 72 fps, and renders (scratchpad/
dam-fixed.jpg) with the sky's clouds present.

### 25.1 Driving the headset from the PC

The boot script's trick plus the input hook makes the whole front end
reachable without wearing the headset:

```
adb shell am broadcast -a com.oculus.vrpowermanager.prox_close
adb shell am start -n com.gevr.port/.MainActivity
adb shell "echo '8000 0 0' > /sdcard/Android/data/com.gevr.port/files/gevr_input.txt"
adb shell am startservice -n com.oculus.metacam/.capture.CaptureService -a TAKE_SCREENSHOT
```

Without `prox_close` the app gets no surface ("SDL Surface timeout after
5000ms") and stops. Mask 8000 is A, 1000 START, 4000 B; the third field pair
is the stick. Fourteen presses of A from a cold start reaches the Dam. Use
this before handing a build over, not after.

## 26. The watch crash: two gepc-ref fixes we never ported

Reproduced here by driving the headset (§25.1): fourteen A presses to the
Dam, then START. Crash every time, in `modelGetNodeRwData` called from
`set_enviro_fog_for_items_in_solo_watch_menu` <- `draw_current_hand_item_and_ammo`
<- `draw_watch_mission_status_page`. Fault addresses like
`0xaebe1b28b400007b` are the signature of an 8-byte pointer read at a 4-byte
offset: the low half is one pointer's *top* word, the high half is the next
pointer's *low* word.

A bounded probe at the call site cleared the header itself:

```
watchitem: item=5 hdr=0x..4290 sw=0x..1498 nsw=36 ntex=12 tex=0x..15b8
           root=0x..1678 sw0=0x..1798 sw1=0x..16a8 sw2=0x..1708
```

`tex - sw` is 0x120 = 36 * 8, so the switch table is at host stride and the
header is correct. The disassembly put the fault on `ldrb w8, [x8]` reading
`root->Opcode`, i.e. the *argument* was the spliced pointer.

**D140** (ported from gepc-ref): further down the same function,

```c
for (j = 0; j != 20; j += 4)
    if (*(ModelNode **)((u8 *)bodymodel->Switches + j + 0x48)) ...
```

walks `Switches` with raw byte arithmetic. 0x48 and 0x5c and the `j += 4`
step are all N64 4-byte-pointer constants; at the host's 8-byte stride every
one of those reads is misaligned. 0x48/4 = 18 and 0x5c/4 = 23, so the host
form is `Switches[18 + (j >> 2)]` and `Switches[23 + (j >> 2)]`. gepc-ref
carries exactly this fix with exactly this diagnosis.

**D264** (also ported): `ModelRenderData renderdata = *(ModelRenderData *)&D_80035D00;`
copies 64 bytes spanning two adjacent N64 globals. A host link separates
them, so the copy came back all zeros and `flags == 0` gated every geometry
node in `subdraw()` — the watch's item preview drew nothing. The explicit
template (`zbufferenabled = TRUE, flags = 3`) restores it. Applying this
moved the crash from +1104 to +1440 in the same function, which is what
exposed the D140 site.

Verified here: the watch raises and opens fully, showing "Q WATCH v2.01
BETA / MISSION STATUS: INCOMPLETE / ABORT: CANCEL CONFIRM" with the PP7
(SILENCED) preview rendering on the screen (scratchpad/watch-full.jpg).
The preview is D264's doing.

### 26.1 Post-mission screen — not re-verified

I could not drive the watch's ABORT/CONFIRM through the input hook (the
4-frame pulse does not register there), so the post-mission screen was not
reached. The fix for it rests on the 06:43 tombstone: `strcpy` called from
`constructor_menu0D_missioncomplete`, which is front.c:7369
`strcpy(stagename, frontGetPlayersFavoriteWeaponInHand(0, 0))` — a function
that returned `int` and truncated its pointer. That is now `char *` with a
prototype in gevr_implicit_protos.h so mpmenu.c stops calling it blind. The
chain is solid but untested on hardware; dying in-level is the way to check.

### 26.2 Still wrong

Gun and hand textures render near-white in gameplay (see
scratchpad/watch-open.jpg) — the user's "pp9, crosshair, brightness, smoke"
report. Nothing in §25 or §26 touches that; it is the next piece of work.

## 27. Pause music played over the level music: musicFadeTick never ran

User report: opening the watch starts the pause track while the level track
keeps playing, and closing it leaves the pause track playing under the level
track.

The state machine is intact. `set_missionstate` 1 -> 3 does
`musicTrack2Play(0x18)` + `musicTrack1FadeOut(0.5f)`, and 3 -> 1 does
`musicTrack1FadeIn(1.0f, ...)` + `musicTrack2FadeOut(1.0f)` — exactly the
stock behaviour, both confirmed firing on device. But those functions only
*record* a fade: target volume, remaining frames, `MUSIC_FADESTATE_FADE_OUT`.
The volume ramp, and the `alCSPStop` that silences a track at the end of a
fade-out, live in `musicFadeTick()` (src/music.c). Grepping showed
`MUSIC_FADESTATE_FADE_OUT` set in three places and read in none.

On the N64 `musicFadeTick()` is called from `__scHandleRetrace`
(src/sched.c:322), right after `joyPoll()`.

**First attempt was wrong**: I added the call to Perfect Dark's
`schedEndFrame` in port/src/pdsched.c, which CMakeLists.txt:216 filters out
of the build entirely — dead code, and the device showed no change. The live
retrace handler is in port/src/gevr_engine_shim.c, the one that already
carries a comment about `joyPoll()` having run from the scheduler's retrace
handler on the N64. The call now sits directly after it, one tick per
retrace, which is what `FADE_FRAMERATE` counts.

Verified on device with a bounded probe before removing it:

```
missionstate: 1 -> 3
musicfade: t1 state=-1 left=26 vol=28399 -> 0 | t2 state=0 vol=32767
musicfade: t1 state=-1 left=25 vol=27307 -> 0 | t2 state=0 vol=32767
...
musicfade: t1 state=-1 left=7  vol=7651  -> 0 | t2 state=0 vol=32767
missionstate: 3 -> 1
```

Track 1 ramps to zero over half a second while the pause track holds at
full. Both directions run through the same tick.

This affects every cross-fade in the game, not just the watch — mission
start/end, the 1 <-> 2 X-track swaps and the fade-ins on the way back all
depended on it.

Lesson worth keeping: before wiring anything into a port/src file, check
CMakeLists.txt:216 — pdmain, pdsched, communityart, mpsetups, optionsmenu
and preprocess are all excluded. An edit there builds clean and does nothing.

Noticed in passing and left alone: `model GwppksilZ: no room to rewrite its
display lists (file 25264 bytes, allocation 30000)` on the silenced PP7.

## 28. Still open

Gun, hand and HUD textures render near-white in gameplay (see
scratchpad/watch-open.jpg) — the user's "pp9, crosshair, brightness, smoke"
report. Nothing in sections 25 to 27 touches it.

The post-mission screen has not been re-verified on hardware; see 26.1.

Recommended next move: D140 and D264 were both sitting in gepc-ref behind
`#ifdef PORT` and had simply never been swept into this tree. A systematic
pass over gepc-ref's PORT guards, rather than case-by-case debugging, is
likely to find the texture defects too.

## 29. Systematic sweep of gepc-ref's PORT guards

D140, D264 and the music fade tick were all things gepc-ref had already solved
behind `#ifdef PORT` and that had never been carried here, so the whole
reference tree got swept rather than debugged case by case.
`tools/gevr_port_guard_sweep.py` walks all 399 PORT guard sites in
`../gepc-ref/{src,port}`, pulls the finding ids out of each block, and checks
whether we mention them anywhere. 187 distinct findings; 35 cited here, 152
not.

Results, caveats and a triage order are in
[docs/gepc-port-guard-sweep.md](docs/gepc-port-guard-sweep.md).

The important caveat is in there too: uncited is not unported. D67 (image_entry
bitfield order) and D85 (texpool is storage, not a pointer) both show as gaps
and are both already solved here by a different route. The 152 are candidates
to triage, not a defect list.

### 29.1 What the first pass found

Triaged the renderer and the texture loader, to match the open complaint about
gun/HUD/crosshair/smoke textures.

`port/fast3d/gfx_pc.cpp` has nothing to port — all eight of its uncited
findings (D116 D157 D172 D219 D229 D252 M-110 M-157 M-158) are env-gated
diagnostic probes, not fixes.

One real gap: **M-113/M-114** in image.c. The decoder stored 16-bit wide-pixel
texels as native u16 while `import_texture_rgba16` reads the pool as
big-endian bytes and `import_texture_ia16` takes intensity from `addr[0]` —
so every non-zlib 16-bit image came back byte-swapped. The 32-bit path must
*not* be swapped, because `import_texture_rgba32` does `PD_BE32()` on a native
load; swapping it was the reference's own M-114 regression. Both importers
were read here first rather than trusting the reference's split.

Applied as PORT_PIXEL16 / PORT_PIXEL32, site for site: eleven 16-bit stores
swapped, nine 32-bit stores left native. Palettes were already correct
(image.c:307 writes them big-endian explicitly), which is why paletted
textures decode and the fire family does not.

Scope, from the reference's ROM census: IMAGE_FIRE_0..14 plus texnums
1198-1201, 2430, 2510-2523. Every ammo, flare, crosshair and muzzle-flash
wide-pixel image is RGBA32 and untouched — so this should fix fire and smoke
and will *not* fix the crosshair or the washed-out gun. Those still need a
cause.

Verified only that the build installs, the Dam loads and runs at 72 fps. The
headset stopped returning anything but black frames to the metacam capture
service this run, so the colour change is unconfirmed; it needs the user's
eyes.

## 30. Guard-house crash and the washed-out gun: D45, the gun model buffer

User report: textures unchanged by §29; further into the Dam, a crash on
"opening a door in the guard house".

The log said otherwise. The trigger was `input: pad0 buttons 8000` — N64 A,
weapon change, not the use button — followed by

```
model Gtt33Z: 17584 -> 22880 bytes, 138 blocks (47 nodes, 21 display lists)
FATAL: Unknown GBI opcode 0x00 at 0xb400007bb0773800.  w0 00000000 w1 00000000
```

`Gtt33Z` is the DD44, a Dam guard's drop, loaded on demand at the switch. Its
display lists were walked into zeros fourteen milliseconds later.

`sub_GAME_7F0762E0` (objecthandler_2.c) moves the file's tail from the first
display list onward to the end of the gun's model region, then rewrites each
display list back from the front through `texLoadFromGdl`, which expands the
texture markers into real RDP commands. The region is `D_80032464[hand]`,
still the N64's 0x7530. Host model files are ~30% bigger (16-byte Gfx), so the
slack between writer and unread tail shrank until the writer overtook it. The
silenced PP7 had already been logging the other failure mode on every level
load: `no room to rewrite its display lists (file 25264 bytes, allocation
30000)`, after which the rewrite *returned early*.

**D45** (gepc-ref, unported and uncited here, found by the §29 sweep's file
list) grows four coupled sizes in gun.c, all taken together:

| | N64 | D45 |
|---|---|---|
| `size_item_buffer` (whole per-hand buffer) | 0x14820 | 0x23000 |
| `D_80032464` (gun model region) | 0x7530 | 0xF000 |
| suit region (`Csuit_lf_handZ` expands to 0x16F9C) | 0xBD70 | 0x18000 |
| trigger / watch-laser region (0x16030) | 0xAFD0 | 0x17000 |

The texture pool is the buffer minus the region, so growing the region alone
would have shrunk the pool from 0xD2F0 to 0x5820. bondview2.c's no-chr path
borrows the same two buffers for Bond's body and head and sizes itself from
`getSizeBufferWeaponInHand`, so it follows automatically.

Headroom: boss.c now logs the stage pool at the end of every level load.
With D45 the Dam leaves **14,134,008 bytes** free, so the extra 118 KB is
immaterial.

**The bigger result:** because the PP7's rewrite used to bail, its display
lists never went through `texLoadFromGdl` and its own textures were never
resolved. The gun and hand were drawn with whatever was still bound — the
grainy grey rock. That was the "pp9 texture" and hand complaint. With room to
rewrite, the PP7 renders as a black glossy pistol in a skin-toned hand
(scratchpad/d45-gun.jpg against scratchpad/watch-now.jpg).

Verified on device: the Dam loads, no "no room" line, the PP7 renders
correctly, firing works. Not verified: the DD44 switch itself — picking one up
needs a dead guard, which the input hook cannot arrange. The mechanism is the
same one the PP7 demonstrates.

Still open: crosshair, smoke and HUD brightness. Any gun that previously hit
the "no room" bail may also have been fixed by this; worth a look at every
weapon.

## 31. Padlock, ammo icon, smoke, impacts, crosshair — and §24 was wrong

User report: textures still wrong (crosshair, HUD bullet, smoke, bullet hits);
can't shoot the padlock past the guard house. All found with evidence rather
than screenshots, using two new tools:

- **Input hook frame count** (port/src/libultra.c): gevr_input.txt takes an
  optional fourth field, frames to hold — `0010 0 0 300` aims for five
  seconds. Needed for anything that must be held.
- **Texture dumper** (port/fast3d/gfx_pc.cpp, `gevr_upload_native`): every
  native import goes through it. Off unless
  `/sdcard/Android/data/com.gevr.port/files/gevr_texdump` exists; re-checked
  every 64 uploads so it can be switched on mid-level. Writes each distinct
  texture once as a PAM under files/texdump/ (cap 96) and logs its tile.
  `scratchpad/sheet.py` (session scratch) renders a contact sheet: composite,
  raw RGB, alpha. This is how every texture fix below was found and checked.

### 31.1 Padlock — D135, the object bullet-hit parser (propobj.c)

`bgTestHitOnObj` walks an object's display list for triangles to ray-test.
It read the opcode as `*(s8 *)gdl` — the wrong byte of a 16-byte host Gfx —
indexed vertices and triangle nibbles as N64 byte offsets, and recovered the
texture number through `*(u16 *)(padC | 0x80000000)`, a KSEG0 address that
faults on the host. It could never find a triangle, so shots passed through
every object; guards die through a different path, which is why only props
were immune. Ported gepc-ref's D135 (read `words.w0`/`w1`, generic hit for
texnum) with one adaptation: our model converter tags segmented addresses
with bit 0 (gevr_model.c convertGdl), so the vertex offset is masked
`0x00fffffe` as seg_addr does, not the reference's `0x00ffffff`.

Probes confirmed shots reach `sub_GAME_7F04E720` for on-screen objects. The
padlock itself is out of reach of the input hook, so it is not verified.

### 31.2 HUD ammo icon — padded 32-bit rows

The 9mm icon is 5x12 RGBA32 but its LOADBLOCK holds 12 rows of 8 texels
(probe: tile line 16, 384 bytes). `import_texture_rgba32` copied 5-texel rows
contiguously, sliding each row three texels. gfx_sp_tri1 already normalises
UVs against the 8x12 block, so for a padded 32-bit block load the importer
now uploads the block (also how gepc-ref sizes it). Unpadded tiles — the
32-wide crosshair — are unchanged. Verified: a clean brass cartridge.

### 31.3 Smoke, impacts, crosshair decay — stale image tables (§24 reversed)

A probe in texSelect showed six consecutive smoke entries with headers
reading 0 and 0x3000 and `texFindInPool` failing, at addresses *below* the
Dam's texture pool. The title screen is a stage and runs texReset first; it
patches the static `s_*images` tables with pointers into the title's pool.
At the Dam's texReset `texLoad` sees a pointer, treats it as loaded, and
skips — so smoke pointed into freed title memory for the rest of the game,
and lazily-loaded tables (crosshair, impacts) went stale on the next stage.

That is exactly what `gevrResetStaticTextureIds` (§22.4) fixed, and **§24
removed it on a wrong diagnosis**: the Dam crash it was blamed for was the
negative tile window found in §25, and its objection — that lazily used
tables would hand raw ids to the renderer — was also wrong, because
`texSelect` checks `index < NUM_TEXTURES` and loads on demand. Restored
verbatim from a2ade78. After it: zero texSelect misses, smoke entries read
texnums 2176..2181 in sequence, and every one resolves into the Dam's pool.
The proper long-term form is still gepc-ref's per-stage segment copy.

### 31.4 D95 — master display list overran every frame (dyn.c)

The -mgfx budget is in bytes for 8-byte N64 Gfx; host Gfx is 16. `gdl++`
has no bounds check, so the display list ran off g_GfxBuffers[1]/[2] into the
per-frame vertex/matrix buffers and, in busy scenes, beyond. Ported gepc-ref
D95: scale by sizeof(Gfx)/8. Not the cause of 31.3 on its own, but a real
per-frame overrun.

### 31.5 Muzzle flash — 32-bit odd-row swizzle

For RGBA32/RGB24 texSwapAltRowBytes swaps words 0<->2 and 1<->3 in every
16-byte group of odd rows; importTextureNative only undid the 4-byte pattern
and skipped 32-bit. Added the 32-bit undo, keeping the row pitch so the
importer's own sizing applies. Verified: the muzzle flash is a clean flame
where every other row was dashes.

### 31.6 Tile windows larger than the load

Replaced §25's negative-window fixup with one rule, placed after the LOD
fallback has finalised `loaded_texture`: if the SETTILESIZE window is wider
or taller than the loaded block, size the tile from the block using exactly
gfx_sp_tri1's per-size formula (32-bit: line/2 by size/line/2). Covers §25's
sky quad and the smoke/fire effect's tile 1 (RGBA16, 16-texel rows through a
56x56 window, which the N64 repeats with the tile mask). Reading a window
bigger than the load is always an over-read, so no correct case changes.

### 31.7 Verified on device

Difficulty screen, Dam overview and firing: no regressions; an impact is now
a dust puff with rock debris instead of a translucent noisy square.

Still open: texture 086 in the dumps (32x32 IA8, noisy with an 8x8 alpha
grid) — unidentified, may be genuine. Tile 1 of the fire effect shows a
thin strip of row padding on its right edge.

## 32. Sweep second pass: model.c and snd.c

Full triage in [docs/gepc-port-guard-sweep.md](docs/gepc-port-guard-sweep.md).
Most of model.c's uncited findings turned out to be solved here already under
other wording (D52, D59, D92, D99, D101, D56/D57, and D43/D45 by a different
route). Two ports:

- **D53.2** — `struct ModelSlot` is now a whole `Model`
  (objecthandler.h, static-asserted). It held only the first eight fields;
  the unka0 note in model.c already recorded a write past it that reached the
  tank record. Any later Model field written on a non-animated model landed
  in the next slot's header.
- **D305** — snd.c's voice-preemption scan guards an empty tracked-sound list
  before the do-while dereferences it (fault 0x62), under heavy SFX load.

Not ported, with reasons in the doc: D147/D152/D285 (thread races; our
audio runs on the retrace), M-66 (a deliberate behaviour deviation), D156
(cutscene NaN guard — a candidate if a cutscene hangs).

Verified on device: Dam loads (13.9 MB stage pool left), firing works, the
watch opens with the corrected ammo icon.

## 33. Watch controls crash, left trigger, AI scripts, menu sprites

User report: textures fixed; the padlock can be shot; the game is a little
too bright; the AK fires far too fast; guards on 00 Agent shoot at walls; a
crash in the watch when scrolling to the controls page (stick left twice);
wants the left trigger to fire; "SELECT FILE" loses half its final E and
there is a faint white line beside the eraser icon.

### 33.1 Watch controls page crash (gunfire.c)

Reproduced over adb (Dam, START, stick left twice). SIGSEGV at
`0x00000000a3ec18f0` in matrix_4x4_copy <- watchRenderController:
`matrix_4x4_copy((u32)modelstack.render_pos + i * sizeof(Mtxf), ...)`, twice
in the function. gepc-ref has the same cast and survives only because its
dram.c keeps pointers under 4 GB. Widened. Same file: the knife-slash
keyframe tables were held in `u32 var_a0_2` (three functions); now
`Weapon1PTransformKeyframe *`. RenderPosView is a union, 64 bytes, so the
render_pos indexing strides agree.

### 33.2 Left trigger fires (port/src/input.c)

It was unmapped; either trigger now sets Z.

### 33.3 AI: D209, D210, D310 (chraction.c, chrai.c)

- D209: `act_ubytes.padding[45]` read act_gopos.unk59 (walk/run/sprint) by
  raw byte; host pointers moved it, it read 0, and every guard was locked to
  the walk animation. Named field.
- D210: `act_init.padding[0x13] = -1` missed act_patrol.lastvisible60 for
  the same reason; patrolling guards read an uninitialised "last saw Bond".
- D310: `PRINT(STRING)` expands to the bare AI_PRINT byte (aicommands2.h), so
  global lists (chraidata.c) hold 1-byte PRINT records, but chraiitemsize
  NUL-scans past them into later records. m_RunToBondPersistent puts one right
  after TRYRunToBond. Ported d310ItemSize at chraiGoToLabel and at ai()'s
  PRINT case; level-local lists keep the NUL scan, which is right for them.

Not verified: guard behaviour cannot be exercised through the input hook.
"Shooting at walls" may or may not be one of these.

### 33.4 Padded rows, every texel size (gfx_pc.cpp)

IMAGE_SELECTFILE is 122x18 IA8; TMEM pads its rows to 128. Rectangles
normalise UVs against the padded block, the upload was 122 wide, so the last
texels fell off. The ammo-icon fix (§31.2) covered only 32-bit; the rule now
lives in import_texture's tile fixup for every size: a block load whose rows
are wider than the window uploads the block. The 32-bit special case is gone.

Triangles normalise against the SETTILESIZE window instead, so any tile the
fixup resizes (padded, larger-than-load, negative) is flagged `from_block`
and triangles use the block dimensions for it too — without that, §31.6's
fire tile would have been sampled at the wrong scale on triangles. The flag
is cleared by every SETTILESIZE, so unresized tiles are untouched.

Verified on device: SELECT FILE complete, ammo icon still clean, Dam and
firing unchanged. The eraser's white edge remains: the icons wrap in S and
rectangles get no half-texel filter offset, so edge samples blend with the
opposite column. gepc-ref behaves the same; left as a known item.

### 33.5 Too bright: every colour was gamma-encoded twice (vr_openxr.cpp)

Measured, not eyeballed: a temporary switch filled the screen swapchain with
exact 128 grey and the metacam capture read 188 — linear_to_srgb(0.502).
The game's colours are already display-encoded; on Android the port picked
a GL_RGBA8 swapchain, which the Quest compositor reads as linear and encodes
again. The desktop path already preferred GL_SRGB8_ALPHA8 with raw writes,
and the framebuffer-effect blit's linear_to_srgb only makes sense with that.
Android now does the same when the driver has GL_EXT_sRGB_write_control
(Quest does): SRGB8_ALPHA8, with GL_FRAMEBUFFER_SRGB_EXT disabled so writes
stay raw. Without the extension it falls back to the old order. Re-measured:
128 in, 128 out. The Dam now has its dusk-blue sky and dark rock instead of a
washed-out grey.

### 33.6 AK fire rate: weapon timing bytes read in reverse (gunfire.c)

WeaponStats.RecoilSpeed is one 32-bit literal per weapon
(gunWeaponStats.inc.c — the KF7's is 0x40C0006) read back as four bytes
through `union { s32 RecoilSpeed; s8 b44[4]; }`: fire cycle, recoil return,
re-fire window, re-fire offset. N64 order is 04 0C 00 06; on the host the
union returns 06 00 0C 04, so every weapon's cadence thresholds were
permuted. The sixteen `weapon_stats->b44[i]` reads now go through
GEVR_RECOIL_BYTE, which shifts the bytes out of RecoilSpeed in cartridge
order. The only such union in the headers. gepc-ref has the same code and an
open, unexplained report (D240) that player gunshot cadence differs from the
N64 — very likely this. Not verified in play: the input hook cannot pick up a
KF7.

## 34. Sweep: bg.c and bondview2.c (plus frametiming D155)

bg.c — already handled here under other forms: D91 (u8 * slot), D312 (reads
the G_VTX count from w0 directly), D154 and D69/D79/D85 (our own background
conversion). Ported:
- **D128** — sub_GAME_7F0B37EC set PORTALFLAG_SPECIAL through
  `((u8 *)g_BgPortals)[(portal << 3) + 6]`, the N64's 8-byte stride; our
  entry is 16 bytes, so the write landed inside another portal's
  offset_portal pointer. Levels in specialportalarray (Control) would fault
  on a later line-of-sight walk.
- **D271** (D106 as revised) — portal screen bounds: only non-finite values
  count as degenerate, so near-plane-straddling portals cull exactly as the
  N64's comparisons did and NaN cannot poison them.

bondview2.c — D140/D56 and D191 already here. Ported:
- **D177** — Bond's movement declared `curLocus` as the 8-byte
  move_bond_temp_struct placeholder; the stan locus functions fill a
  StandTileLocusCallbackRecord, 24 bytes here, overrunning the stack frame.
Everything else in bondview2.c is cutscene or timing diagnostics (D146,
D160, D173, D193, D243, M-series).

frametiming.c — **D155**: osGetCount is wall-clock, so a loading stall
became hundreds of g_ClockTimer ticks of catch-up in one frame; capped at 6
as the reference does. No effect at normal frame rates.

Verified on device: Dam loads, walking and firing work, and the watch pages
through the controls screen without crashing. Open: the N64 controller model
that should sit in the middle of the controls page is not visible.

## 35. Watch briefing page crash: setup records overwriting each other

User report: from the controls page, one more left crashed. Page order is
Mission status, Inventory, Controls, Game options, Briefing; the crash was
`strcat(objectiveBuffer, NULL)` in draw_watch_mission_briefing_page.

Found with three bounded probes (records' raw bytes, the objective list, the
setup walk), all removed:

1. **Briefing text records (type 35) were 16 bytes on the host**, the N64
   size. setup_briefing_text_entry_parent then links them by writing an
   8-byte host `next` pointer at +16 — the N64 field was at +12, inside the
   record — so every link overwrote the first 8 bytes of the *next* record.
   On Dam that wiped two briefing pages (M, Q) and the header of objective 0
   (`00000017 00000000` became a pointer), so the setup walk never registered
   it, objective_ptrs[0] stayed NULL, and the objectives page strcat'd NULL.
   Fixed the way type 23 already was: host_prop_bytes(35) = 24 and a
   converter case that leaves room for the pointer. sizepropdef derives from
   the same table, so the walk and the layout agree.
2. **objective_entry / watchMenuObjectiveText read the text id as a u16 at
   +0xA and difficulty as an s8 at +0xF** — the low halves on a big-endian
   N64. The converter stores those payloads as native 32-bit words (as
   MissionObjectiveRecord reads them), so on the host those offsets hold the
   high halves: 0. Readers now take the low part of the word
   (OBJECTIVE_TEXT / OBJECTIVE_DIFFICULTY / BRIEFING_TEXT in
   objective_status.c), correct on both byte orders.

Verified on device: all four Dam objectives register (menus 0-3) with real
text ids and per-objective difficulties; the briefing page shows A-D; seven
lefts and three rights through the watch without a crash. This also puts
objective A back into the mission-completion check, which had been skipping
the NULL slot.

## 36. Controls after the watch, watch stick directions, chr.c/front.c sweep

User report: after pausing into the watch and unpausing, the right stick did
nothing and the left stick became look; watch navigation went the wrong way.
Two separate defects.

1. **The control style drifted away from 1.2 Solitaire.** The Quest mapping
   in port/src/input.c (right stick = N64 stick, left stick = C-buttons) only
   makes sense under Solitaire. init_watch_at_start_of_stage sets HONEY, and
   the watch Controls page (sub_GAME_7F0A611C) scrolls the style with the
   stick. Ported gepc-ref D194/D238: inputReadController re-asserts Solitaire
   on every poll with cur_player_set_control_type (plain field writes), and
   logs each distinct style it corrects. The Controls page therefore always
   shows 1.2 SOLITAIRE.
2. **Every down/left stick push in the watch read as up/right.** options.c
   never included joy.h, so joyGetStickX/Y were implicit `int` functions.
   MIPS callees returned an s8 sign-extended to the full register; on AArch64
   the caller extends, so -80 came back as 176 (confirmed with a probe in the
   list scroller, removed). This is a third form of the implicit-declaration
   defect, so tools/gevr_implicit_decls.py now flags narrow (s8/u8/s16/u16/
   bool) and float returns as well as pointers, keeps the entries already in
   the header (their calls no longer warn because of it), and ignores
   prototypes and static functions. The regenerated src/gevr_implicit_protos.h
   goes from 41 to 90 prototypes. Worth knowing about among the 49 new ones:
   - eight f32 watch-inventory placement getters in gunfire.c called from
     bondinv.c (the result was read from w0, not s0) — a candidate for the
     invisible controller model / item models on the watch;
   - bool ray and hit tests: bgTestRayIntersectsBbox, intersectRayTriangle,
     propobjFindHit, objTestForInteract, doorTestForInteract,
     modelTestRayIntersectsNodeBBox — candidates for "guards shooting at the
     wall" if their callers were seeing garbage upper bits;
   - u16 joyGetButtons/joyGetButtonsPressedThisFrame in options.c/spectrum.c.
   Regenerate after a full rebuild (see the tool's docstring).

Also in input.c, for the watch only: the grips are L/R (the N64's own page
turn), and in menus whichever stick is pushed further navigates.

Verified on device: left from Mission status goes to Briefing and right goes
to Inventory and then Controls; down on the Controls style reads -80; after
unpausing, forward on the left stick moves and the right stick turns.

Sweep (docs/gepc-port-guard-sweep.md, fourth pass):
- chr.c — nothing to port. D43/D45 solved differently (CollisionRelatedNode
  resolved as a segment-5 offset); D120 caps a PointUsage walk that only
  cycles with the reference's broken converter, and ours swaps those arrays
  (BK_S16S / BK_COLVTX); the rest are telemetry or intro-puppet experiments.
- front.c — ported **M-148** (difficultytext[4] took "Secret Agent\n": a
  stack overrun on every file-select frame), **D221** (folder hit band built
  from separate locals the ABI does not pair) and **D50** (legal screen reads
  legal_text_ptr before assigning it). D164 and D178 were already handled
  (ARRAYCOUNT; ob.c swaps Ubrief* on load); D63/D64/D65/D146/D243/D250 are
  diagnostics.

## 37. The real cause of "left stick becomes look after the watch": bool

User report after §36: pause, change nothing, unpause, and the left stick is
still look. §36's Solitaire re-assert was right but not this bug.

port/src/input.c decides "in a menu" from g_CurrentPlayer->pause_state. A
probe there showed pause_state jump from 0 to -1065353216 (0xC0800000, the
bits of -4.0f) on pausing and never change again, while the game paused and
unpaused normally. offsetof(struct player, pause_state) was 548 in input.c
and 564 in the game's files; sizeof 14144 vs 14160.

bondtypes.h does `typedef s32 bool` only `#ifndef bool`. Every VR header
includes <stdbool.h>, so a port file that includes one first sees a 1-byte
bool in every game struct. struct player has six bool members before
pause_state (prevupdown, movecentrerelease, ...): 24 bytes on the game side,
8 with padding on the port side. input.c was reading a float four fields
early, so `menu` stayed true after unpausing: left stick on the N64 stick
(look), right stick unread, no C-buttons. inputRumble's equipcuritem read
was misplaced the same way.

Fix: the 17 struct members declared bool in bondtypes.h and bondview.h are
now s32 — what bool already is on the game side, so the game's layout does
not move, and every translation unit agrees. (The 30 bool bitfields in
bondtypes.h are inside `#if 0`.) bondtypes.h carries a note forbidding bool
members. bondview2.c exports gevrPlayerLayout(); input.c compares it with
its own sizeof/offsetof once at startup and logs an error on mismatch.
Only input.c, gevr_engine_shim.c, pdmain.c (excluded) and
vr_settings_defaults.c mix stdbool with game headers; only input.c reads
game structs.

Verified on device: pause_state now reads 1, 3, 2, 0 across a pause and
unpause from the port side; no layout error; moving and firing work after.

Sweep, fifth pass (docs/gepc-port-guard-sweep.md): bondtypes.h and
propobj.c — nothing new to port. D69/D78 (bitfield order), D43/D45, D52
already here; D88 solved by the setup converter widening intro cameras to
56 bytes; D151/D157 handled in §35; propobj D52 sites already word-indexed;
D135 ported in §31; D218/D222 belong to the reference's FOV-scale option;
D202/D207/D318/M-65/M-71 are diagnostics.

## 38. Gun tint, stan callback ABI (D253/D177/D89), D233

User report: a thin coloured band on the PP7 silencer joint that turns blue,
red or green depending on where Bond stands; AI "still not right".

**Gun tint — the mechanism is faithful, the bright band is unexplained.**
The first-person gun (model render type 3) combines
`lerp(ENV, TEXEL0, SHADE_ALPHA) * SHADE`, with ENV = g_CurrentPlayer->tileColor,
the saturation of the stan tile under Bond (set_color_shading_from_tile).
Checked end to end with probes (all removed):
- stan tile colours convert correctly: Dam is ~92% neutral (0xfff), with a
  few red (0xb22/0xc22), teal (0x6ab) and amber (0x874) tiles — plausible
  baked lighting;
- on a white tile the tint is 0 and the joint renders dark;
- forcing every tile to 0x6ab gives a tint of (0x00,0x26,0x2a) and the gun
  and hand take a faint teal cast — N64 behaviour. No bright band;
- vertex colours are copied byte for byte (convertVertices), env/fog words
  are packed and unpacked consistently, fast3d keeps vertex alpha for
  SHADE_ALPHA.
The band in the user's screenshot measures ~(147,190,221), brighter than
the tint can reach (<=0x7f per channel before the SHADE multiply), so it is
not the tile tint. Not reproduced at the Dam start over eight headings.
Needs the user's location. Also seen: a solid blue strip along the wall
at the start (floor geometry missing, clear colour showing) — not fixed by
D233, open.

**stan.c (sweep) — ported:**
- **D253**: stanCheckLinkedSpecialTile is standTileLocusCallback_B_t, called
  with three f32s, but was declared with s32s. On AArch64 floats and ints use
  separate registers, so outFlags came from x5 (garbage) instead of x2 — a
  wild write whenever a locus touched a crouch/ladder tile (Bond's movement
  and AI stan queries). The only such mismatch in the build
  (-Wincompatible-function-pointer-types lists two others, both harmless).
- **D177**: outFlags is the caller's StandTileLocusCallbackRecord; its leading
  s32 * is 8 bytes, so outFlags[1] hit the pointer's upper half instead of
  count (ladders never registered). Written through the named fields.
- **D89**: NULL-tile guard in sub_GAME_7F0B0914 (the LOS/walk), returning
  the N64-effective TRUE.
- D90 already here; D79/D88 handled in bondtypes.h/the converter.

**fast3d — D233 ported**: skip the trivial-reject AND when any vertex has
w < 0 (outcodes invalid behind the camera), as the backface test does.

**AI:** guard vision is chrCanSeeBond = stanTestLineUnobstructed (the stan
walk above) + same end tile; bullet collision against walls works for the
player (impacts appear), and bgBuildRoomVtxBounds already reads the G_VTX
count from w0 (D312). Waiting on a concrete description of what the guards do.

## 39. Blue strip, stan pointer overwrite, crouch toggle, rumble, guard AI

**Blue strip along the wall at the Dam start — fixed.** Its colour is Dam's
sky/fog colour (0x10,0x30,0x60): a hole. Eliminated in turn with probe
builds: not portal culling (all portals forced full-screen), not fog
(fog forced off), but backface culling (culling off filled it). Logging the
culled triangles showed geometry mode 0x1205 (F3D G_CULL_FRONT) under a
negative-determinant matrix. GoldenEye compensates for mirroring itself —
propobj.c draws a DOORFLAG_FLIP door through a mirrored matrix with
CULLMODE_FRONT, gunfire.c picks cull modes for mirrored dual weapons — and
the N64 RSP culls on screen winding alone. fast3d's gfx_is_matrix_inverted()
(a Perfect Dark VR addition, not in gepc-ref) flipped the test again, so the
flipped tunnel door showed its inside faces (the "rust pillar") and lost its
floor edge. It now returns false. Tried and rejected on the way: skipping
the cull for w<0 triangles (draws genuine back faces), per-vertex mirror
tracking (same result as the original).

**Stan pointer overwrite in object placement — fixed.** A walk from the
tile 0xb400007db400007d (two pointer high halves) failed every time; traced
with return-address logging to sub_GAME_7F04088C (propobj.c), which passed
f32 locals to chraiGetCollisionBounds's `struct rect4f **` and `s32 *`
out-parameters. The 8-byte pointer store overran byrefA into mStan. Typed
correctly under GEVR. The only such call in the build's
-Wincompatible-pointer-types output.

**Crouch.** 1.2 Solitaire only crouches while aiming (R + C-down, standing
again on release). Left stick click now toggles crouch (the Perfect Dark VR
port's stick-click crouch, VrStickClickToCrouch): input.c keeps the toggle,
bondview2.c forces crouchDown while set and one crouchUp on release,
respecting WEAPONSTATBITFLAG_DISABLE_CROUCH. Reset on stage change. Not
verified on device (the input hook cannot press the stick).

**Rumble.** __osMotorAccess asks for 5 s at full strength per motor start
and inputRumble sends it to both controllers. The user's left controller has
been dropping off until its battery is pulled; haptics are the only traffic
to it. Pulses are now capped at 0.3 s and identical commands are not re-sent
within 200 ms. Cause of the dropout not proven.

**Guards (user: miss a lot, only notice up close, don't reacquire).**
Probed chrCheckTargetInSight / chrCanSeeBond on device. Vision range
(visionrange 100 = 100 m), the fog limit (75000 units), the FOV gate and the
random notice gate all read nominal. Most failures (824 of 920 sampled) are
the stan line walk stopping at a tile edge with no link — GoldenEye's AI
vision is tile-based, so guards cannot see across unwalkable gaps, barriers
or ledges; the rest are prop polygons. Hit chance (chrlvFireWeaponRelated
region, chraction.c ~6400) reads nominal: weapon stats are compiled-in,
difficulty modifiers from lv.c, 007 getters properly declared. No port
defect found in the AI beyond §38's D253. Open; needs a specific scene to
compare against N64 footage.

## 40. Guards frozen in place (JP timer), rumble never reached the controllers

**Guards ignoring Bond (user video, 007 difficulty).** The guard reacted and
fired, but toward the direction it already faced. A probe in chrlvSetSubroty
showed the target flags right (TARGET_BOND), the angle to Bond right, and a
turn step of 0: `0.0628 * speed * g_JP_GlobalTimerDelta * playspeed`. The
build defines VERSION_US **and BUGFIX_R1** (from the initial commit; the
reference builds ntsc-final with BUGFIX_R0, matching the N64 Makefile). R1
paths read g_JP_GlobalTimerDelta, which only the JP/PAL branch of lv.c
updates, so here it was always 0: guards could not turn toward Bond, their
aim never blended onto him (chr.c:1844), death animations and
monitor/object-interaction timers stalled. lv.c now mirrors
g_GlobalTimerDelta into it under GEVR + BUGFIX_R1 (on the JP cartridge the
two are the same value). Verified: guard 6's angle to Bond now holds at 0 as
he tracks. Switching the build to BUGFIX_R0 like the reference is the fuller
fix, but it flips ~50 sites and struct fields (bondview.h, chr.h) - not done
blind.

**No rumble when firing.** Three gaps, all port-side:
- osPfsInit returned PFS_ERR_NOPACK, so joyRumblePakInit never tried the
  motor. It now returns PFS_ERR_DEVICE (a Rumble Pak) where the port can
  vibrate; GoldenEye saves to EEPROM, only joy.c calls it.
- osContGetQuery reported CONT_ABSOLUTE without CONT_JOYPORT, which
  joyRumblePakInit also requires.
- osMotorInit/Start/Stop were stubs; they now route through osMotorProbe and
  __osMotorAccess as Perfect Dark's port does.
- inputRumble gated the Quest haptics on vr_init_done, set only by pdmain.c's
  VR loop, so screen mode never vibrated. It now asks vr_haptics_ready()
  (OpenXR session and haptic action exist).
Traced end to end on device (rumblepak init -> start -> motor access ->
inputRumble), no haptic errors. Firing is 0.1 s per shot (gunfire.c).

**Left controller dropouts** are not the app: the system log shows the left
controller (a9280d99bab13a51) as `disconnected detached update-required`,
its radio reporting `Failed to receive ready signal from host`, plus
IMU-corrupt errors on both controllers. And until this section the game sent
it no haptics at all. User action: update the controller firmware in Quest
settings (or unpair and re-pair it).

## 41. US revision flags (BUGFIX_R0), chrai.c/stan.c sweep

**BUGFIX_R1 -> BUGFIX_R0.** The ROM is US, and gepc-ref's ntsc-final and the
N64 Makefile build US with BUGFIX_R0. Before switching, every R0/R1 block (88)
was scanned for port fixes (tools: scratchpad r1scan.py): the only R1-only
content was the JP-style hudmsgBottomShow storing font pointers as
`(s32)(uintptr_t)` (truncating) plus caption debug logs. On the R0 side the US
font globals copy_1stfonttable/copy_2ndfonttable were `s32` (fine under
gepc-ref's s32-safe dram.c, truncating here): now uintptr_t under GEVR, with
setFontTables taking pointers and the caption renderer
(gevrBottomCaptionChars/Font) reading them on the US path, BankGothic by
default and ZurichBold for intro captions, as the US original does. A full
rebuild's int-conversion / pointer-to-int / implicit-declaration /
function-pointer warnings were diffed against the R1 build: nothing new.
§40's g_JP_GlobalTimerDelta mirror is now dormant (R1 only). Verified on
device: intro plays, guards engage and hit Bond.

Still different from the reference's ntsc-final set: LEFTOVERDEBUG and
LEFTOVERSPECTRUM (27 files, mostly debug menus, but also ob.h,
bondview_internal.h, initBondDATAdefaults.h, token.h and alternative code such
as a LEFTOVERDEBUG variant of stan.c's edge test). The US ROM was built with
them. Separate step: scan those blocks the same way first.

**Sweep.**
- chrai.c: complete. D310 ported in §34; D309 diagnostics; D243 M-145/M-169/
  M-170 cutscene experiments.
- stan.c: complete. D253/D177/D89 ported in §38; D90 and D189 already here
  (STAN_LOCUS_TILESTACK_MAX 55; record-sized clear); D177's pointer-add sites
  already use u8 */array indexing; D88 diagnostics.

## 42. LEFTOVERDEBUG / LEFTOVERSPECTRUM on (build flags now match ntsc-final)

The US Makefile and gepc-ref's ntsc-final set both; the US ROM contains that
code. Reviewed every block first (scratchpad ldscan.py):
- real game-behaviour values the US ROM uses: options.c watch perspective
  aspect 1.333 and positions (0xA0/0xAA), chr.c aim rise/fall 10/20,
  objective_status2.c particle spread, propobj.c projectile SFX interval 6,
  stan.c's LEFTOVERDEBUG sub_GAME_7F0B07BC edge test, glass2.c's
  hudMakeDamageSegments variant, ob.c's obLoadBGFileBytesAtOffset order;
- model.c null/range checks that only osSyncPrintf and call return_null()
  (a no-op) - diagnostics, none fired in play;
- debug menus, speed graph, indy host grabs, the ZX Spectrum emulator menu.
Port changes needed: get_counters() in gevr_engine_shim.c (sched.c is
excluded; clock 1 so the speed graph divides safely); a %p debug print in
model.c subdraw; uintptr_t size math in debugmenu.c; the implicit-prototype
header regenerated (debug-menu callers of getCurrentPlayerProp etc.).
A full rebuild's truncation/implicit warnings were diffed against the R0
build: the rest are confined to the debug menus. Verified on device: Dam,
gameplay and the watch, no debug check fired.

## 43. Sweep: the remaining D-numbered reference findings

Re-ran the sweep (83 uncited left, most M-series probes already triaged) and
went through every remaining D-numbered one. Ported:
- **D96** (bondconstants.h): PROPRECORD_STAN_ROOM_LEN 4 -> 8. The room-list
  producers yield up to 7 rooms; chrprop.c had clamped to 3, silently dropping
  a guard's 4th+ room. PropRecord is runtime-only and every user sizes by the
  constant (explosion/smoke copies included).
- **D150** (str.c): strcpy/strncpy/strcat treat a NULL source as "" (langGet
  returns NULL for an unloaded bank). The pointer is laundered through an
  empty asm, because these are nonnull builtins and a plain NULL test is
  deleted.
- **D129** (language.c): langGet bounds-checks the bank index.
- **D301** (file2.c): fileGetIsCheatUnlocked returns FALSE for a NULL save.

Already here or not applicable: D58 (gun-barrel reserve 0x200), D86 (gait
RootNode), D139 (cleanup propdef type), D100/D102/D115/M-189 (inline player
Model, gait rwdata buffer, separate weaponModel/weaponRwPool, throw offsets
by field), D122/D126/D132/D88 (setup converter), D54 (ALParam widened with
static asserts; music sequence header swapped by our loader), D66 (romCopyAligned
returns void *), D80/D85/D154/D312/D313 (room DL conversion and hit tests),
D105 (fast3d handles the Z fill), xprintf va_list (a struct on AArch64, not an
array). Reference-only options/tooling: D121, D181, D211, D216, D225, D249,
D48/D49/D50/D69 sidecars, D59 dram.c. Diagnostics/probes: D51, D63-D65, D75,
D104, D236, D309, D322, M-series. Not ported: D156/D311 NaN root-motion guard
(cutscene-only symptom not seen), ramrom demo replay (D66/D87; demos not
loaded by this port).

Verified on device: gameplay and the watch briefing page.

## 44. Watch controller model, cutscene re-seed, the "black notch"

- **Watch Controls page: N64 controller model invisible** (gunfire.c
  watchRenderController). The decomp copies the model render data from
  `D_80035D04 + 0x3c`, an N64 global-adjacency trick (gepc-ref D264): on the
  host that reads unrelated bytes, `flags` came out 0 and subdraw drew none of
  the controller's nodes. Now built explicitly (zbuffer on, flags 3). The
  rwdata buffer was also 26 words for a model needing more; widened to 128
  with a logged bound check. Verified on device.
- **D243 M-190** (chrai.c, bondview2.c): the cutscene look-at filter
  (field_3B8, field_3C4..3CC) kept easing from the pre-teleport position when
  a scripted teleport moved Bond during POSEND. chrai's teleport handler now
  bumps an epoch; bondview2 re-seeds the filter from the new position when it
  changes. The only live fix among the reference's cutscene experiments.
- **The black notch top-right** was the Quest's "screenshot saved" toast,
  captured in the frames that follow a screenshot. Not a game bug.
- Probe/experiment sweep closed: model.c M-174..M-187 are GE_D243M-gated
  logging; M-183/M-185 clamps guard against M-188 (the stale 0xfb sizeof(Model)
  literal), which we already have; chr.c M-159..M-173 are GE_D243X2 tests the
  reference labels "not a fix"; objecthandler M-154, chraction M-178/M-187,
  event.c M-120, image.c M-89, snd.c D322/M-67/M-70 are diagnostics;
  vtxstore M-140 (D255), frametiming D117/D134 (D155), bg.c M-30 (D154) were
  already ported.
- Eraser white edge on the file screen: user says low priority; open.

Verified on device: Dam intro into gameplay, no errors in the log.

## 45. Facility crash on the first thrown mine (texture pool end)

User report: Facility, crash about three minutes in, using a mine. Tombstone:
SIGSEGV at address 2 in modelGetNodeRwData from weaponSetGunfireVisible,
under generate_player_thrown_object. `PchrremotemineZ`'s Switches[0] read 2.

A per-frame probe of every loaded item model's switch table showed the entry
flip 0 -> 2 on the frame bondview2 builds Bond's third-person body. The left
hand's weapon buffer (the texture pool for that body) ended exactly where the
mine's model file began. texInitPool (our D217 port) rounded the base up to 8
and then took the end from the rounded base, so the pool ran up to 7 bytes
past its buffer; the first tex record landed in the mine's switch table. The
reference computes the end from the caller's base; ours now does too. The
Dam's buffer happened to be 8-aligned, which is why only Facility showed it.

Verified on device: Facility, table stays 0, mine thrown (5 -> 4) and
detonated, no crash.

## 46. True stereo gameplay (Dam), with the virtual screen kept for everything else

**Sources.** Upstream GEVR PC was re-cloned to `../gevr-up` (docs only, latest
release vr445; no game source is published) and Perfect Dark VR's `port`
branch to `../pdvr` (HEAD 9984611, 2026-09-21). Our vendored `port/vr` matches
pdvr `0045feb` plus our ~290 lines; pdvr has since added controller-tracking
robustness (hold the last pose on tracking loss, reject bad samples), a PSVR2
profile and Quest XR-layer colour format fixes - worth a sync (open).

**Approach: Perfect Dark's, gated like GEVR.** Our renderer is PD's single-pass
multiview fast3d: the game builds one centre-eye camera and the vertex shader
shears clip space per eye (IPD x `vr_world_scale`). GEVR PC instead loops the
render per eye inside `lvlRender`, which forced per-eye model rebuilds and pool
and viewport fixes (its docs 258, 292-303); not taken. From GEVR: stereo only in
ordinary first-person play (docs/20 `geVrWorldCamera`), auto-recentre on the
way in, both stick clicks recentre, 100 units per metre.

- `bondview2.c` `gevrStereoFrame` (called at the top of `lvlRender`): decides
  stereo per frame - PlayMode stereo, VR ready, one player, `CAMERAMODE_FP`,
  not the tank camera, not paused (watch), not dead. Sets `gevrVrScreenMode`
  (gfx_pc.cpp) and `g_gevrStereo`. Port of PD `joy_for_vr`: the right stick
  turns a body yaw (2 deg/tick smooth, or `SnapTurn` degrees). Entering stereo
  recentres (`vr_align_with_game_angle(0)`); leaving it re-hangs the screen in
  front of you.
- `bondviewApplyVertaTheta` (PD `bmoveUpdateVerta`) starts with
  `gevrStereoApplyHead`, the port of PD `vr_player_rot`: look {0,0,1} and up
  {0,-1,0} through the head quaternion, then the body yaw, y negated; writes
  `vv_theta`/`vv_verta` so movement, aim and the gun follow the head. Verified:
  the game's own `applied_view` then equals the head look vector exactly.
  Game-side turns (teleports, scripted facings) are folded into the body yaw.
- The first-person camera takes the full head look/up (roll included) at
  `bondviewUpdateCameraMatrices`, recomputed at render time from the newest pose.
- `lv.c`: in stereo the player gets `XrFov` x (fovy / 60) and `XrAspect` through
  `viSetFovY`/`viSetAspect`, as PD's `VrApplySettingsOnStart` does, so the
  projection and the portal/scissor scales (`currentPlayerSetCameraScale`)
  agree. Overriding only `guPerspectiveF` left the rooms clipped to a band.
- `vr_openxr.cpp`: C bridges (`gevrVrReady` returns int, not bool - the game
  sees bool as s32), `vr_screen_set_visible` so the quad is not submitted over
  stereo frames, `gevrVrSetWorldScale`.
- **Fixed in PD's layer:** `vr_begin_eye_render` cleared depth with whatever
  `glDepthMask` the last draw left; with writes off the eye buffer's depth was
  never cleared and every depth-tested room failed (sky, gun and HUD drew, the
  level did not). Now forces the mask on for the clear and restores it. Found by
  counting triangle fates: 2,450 per frame reached GL and none showed.
- Input (`input.c`), stereo only: the right stick no longer reaches the game
  (its turn/look are replaced), right X feeds the body yaw. Chords: both stick
  clicks = recentre; hold the right stick click 1 s = switch stereo/screen
  (saved as `PlayMode` in goldeneye-vr.ini); while the screen is up, hold both
  grips + right stick = screen nearer/further (up/down, same physical size) and
  bigger/smaller (left/right), saved as `ScreenDistance`/`ScreenFov`. Left
  stick click (crouch) now toggles on release and not as part of a chord.

**Verified on device:** Dam in stereo at the headset's full FOV (rocks, road,
barriers, sky, gun, HUD); head yaw/pitch drive the view and `applied_view`
matches; watch open -> screen in front, close -> stereo again; no errors.
**Not verifiable from the PC** (the input hook cannot press VR buttons, and the
headset lay on the desk): depth/IPD comfort, turning, recentre, the mode
switch and the screen adjustment - for the user.

**Not yet:** head translation (PD walks the body after the head with
collision; GEVR ships `HEAD_TRANSLATE=0`), controller aim (gun follows the
hand), HUD on a head-locked quad, znear clamp if a blue band shows up close to
walls (GEVR docs/19), syncing the vendored VR layer with pdvr HEAD.
