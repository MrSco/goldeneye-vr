# GEVR Android Port — Handoff Document (Session 3)

> **Note on `artifacts/`:** build and boot logs referenced throughout this document live in the original working folder `GEVR-OpenGLES/`, not in this repository. They are debug evidence, not source.

**Project:** Native standalone Meta Quest (Horizon OS / Android `arm64-v8a`) port of GoldenEye 007 VR (`MrSco/goldeneye-vr`), powered by OpenGL ES 3.2, Meta OpenXR Mobile SDK, and SDL2.

---

**Next agent: read §10 first, then §9/§8/§7. User confirmed file select now opens; barrel stripes, blood animation, flicker and grid fixes remain accepted. New 20:39 build addresses briefing crash, left-stick menu navigation and padded texture rows; headset validation pending.**

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
to do nothing; there is no sound at all (audio is still stubbed in
`port/src/gevr_engine_shim.c`, by design for now).

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
    still to do.

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
| **Class D: Bitfield Packing & Linker Relics** | C bitfield ordering is inverted between big-endian MIPS and little-endian ARM. Taking `&Symbol` of N64 link symbols. | Struct fields read wrong bits; link symbols have no physical address. | Read bitfields by explicit field name or shift/mask. Convert link symbols to pointer variables initialized from ROM manifest. |
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

The upstream GEVR PC port (`no6969el/GEVR`, source tree `goldeneye-native`
with `ge_vr_xr.cpp` and the `getv/` tools; NOT in this repo, only its docs
are, under `docs/`) already solved the presentation for a headset, and it
did it the same way as item 33:

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

First task therefore: locate the `goldeneye-native` tree on this PC (the
docs cite `goldeneye-native\getv\tools\...` and `ge_vr_xr.cpp`; ask the
user if it is not under `C:\Users\Occor\Documents\other_projects`). Every
item below names what to compare against in it.

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
   `pd-vr.ini` via `port/vr/vr_settings.cpp`. Upstream sizes the screen in
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
   Also: no audio (stubbed), quit takes 10 s (item 17), lighting looks dark
   on the Nintendo logo and characters (unexplored; check
   `calculate_normal_dir` / lookat handling against GEVR PC first).

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
- This workspace IS the user's fork/clone of the public GEVR repository.
  Its docs mention a separate `goldeneye-native` development tree, but that
  does not establish that the user has that tree locally. The sibling
  `other_projects/goldeneye-decomp` has remote `n64decomp/007`, not GEVR VR code.
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

