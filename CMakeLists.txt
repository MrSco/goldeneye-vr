cmake_minimum_required(VERSION 3.16)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

project(gevr C CXX)

# The application targets Meta Quest. Native regression tests use their own
# compiler commands in port/tests and do not need a desktop game target.
# GEVR_HEADLESS builds the dedicated server instead (docs/dedicated-server.md):
# the same game sources on Linux, with no headset, window, sound or input.
option(GEVR_HEADLESS "Build the dedicated server (Linux, no headset)" OFF)
if(NOT ANDROID AND NOT GEVR_HEADLESS)
  message(FATAL_ERROR
    "GoldenEye VR targets Android/Meta Quest only. Build through android/gradlew.bat assembleRelease (or assembleDebug), or configure with -DGEVR_HEADLESS=ON for the dedicated server.")
endif()

include(TargetArch)
include(FindPkgConfig)
include(AssetUtils)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default build type
if (NOT EXISTS ${CMAKE_BINARY_DIR}/CMakeCache.txt)
  if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
  endif()
endif()

add_subdirectory(port/external/pdphysics)

# Options
set(ROMID "ntsc-final" CACHE STRING "Which ROM to build for: ntsc-final")

target_architecture(TARGET_ARCH)
if(TARGET_ARCH STREQUAL "i386")
  set(TARGET_ARCH "i686")
endif()

if((TARGET_ARCH MATCHES "unknown") OR (TARGET_ARCH STREQUAL ""))
  message(STATUS "Target arch undetermined, default to arm64 on Android")
  if(ANDROID)
    set(TARGET_ARCH "arm64-v8a")
    set(TARGET_IS_64BIT TRUE)
  endif()
elseif(TARGET_ARCH MATCHES "64")
  set(TARGET_IS_64BIT TRUE)
else()
  set(TARGET_IS_64BIT FALSE)
endif()

message(STATUS "Target arch: ${TARGET_ARCH}")

if(GEVR_HEADLESS)
  set(TARGET_OS "linux")
else()
  set(TARGET_OS "android")
endif()

set(TARGET_PLATFORM "${TARGET_ARCH}-${TARGET_OS}")
message(STATUS "Target platform: ${TARGET_PLATFORM}")

# Git hash & branch
execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE VERSION_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
execute_process(
  COMMAND git rev-parse --abbrev-ref HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE VERSION_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

if((VERSION_HASH STREQUAL ""))
  set(VERSION_HASH "vr-quest")
endif()
if((VERSION_BRANCH STREQUAL "") OR (VERSION_BRANCH STREQUAL "HEAD"))
  set(VERSION_BRANCH "main")
endif()

set(VERSION_TARGET "${TARGET_PLATFORM}")
set(VERSION_ARCH "${TARGET_ARCH}")
set(VERSION_ROMID "${ROMID}")
set(VERSION_BUILD "${CMAKE_BUILD_TYPE}")

configure_file("${CMAKE_SOURCE_DIR}/port/include/versioninfo.h.in" "${CMAKE_BINARY_DIR}/port/include/versioninfo.h" @ONLY)

# Definitions
add_definitions(
  -DMATCHING=0
  -DPIRACYCHECKS=0
  -DROM_SIZE=12
  -DAVOID_UB=1
  -D_LANGUAGE_C=1
  -DVERSION=2
  -DVERSION_US=1
  -DLANG_US=1
  -DREFRESH_NTSC=1
  -DGEVR=1
  # US ROM, so the US revision: gepc-ref ntsc-final and the N64 Makefile use
  # BUGFIX_R0. BUGFIX_R1 is the JP/PAL revision and read JP-only state here.
  -DBUGFIX_R0=1
  # The US Makefile builds with these too (gepc-ref ntsc-final): leftover
  # debug checks/prints and the ZX Spectrum emulator menu are in the US ROM.
  -DLEFTOVERDEBUG=1
  -DLEFTOVERSPECTRUM=1
)

if(TARGET_IS_64BIT)
  add_definitions(-DPLATFORM_64BIT=1)
endif()

if(GEVR_HEADLESS)
  # _GNU_SOURCE: glibc hides the POSIX and BSD names bionic always shows
  add_definitions(-DGEVR_HEADLESS=1 -D_GNU_SOURCE)
  # The Quest builds with NDK 25's clang 14, where these are warnings; newer
  # clang makes them errors. Same code, same rules.
  add_compile_options(
    $<$<COMPILE_LANGUAGE:C>:-Wno-error=implicit-function-declaration>
    $<$<COMPILE_LANGUAGE:C>:-Wno-error=int-conversion>
    $<$<COMPILE_LANGUAGE:C>:-Wno-error=incompatible-function-pointer-types>
    $<$<COMPILE_LANGUAGE:C>:-Wno-error=implicit-int>
    -Wno-error=incompatible-pointer-types
  )
else()
  add_definitions(
    -DANDROID=1
    -DXR_USE_PLATFORM_ANDROID=1
    -DXR_USE_GRAPHICS_API_OPENGL_ES=1
  )
endif()

# Compiler options
add_compile_options(
  -fno-inline-functions
  -fno-strict-aliasing
  -funsigned-char
  -fwrapv
  -Wall
  -Wno-address
  -Wno-int-in-bool-context
  -Wno-misleading-indentation
  -Wno-missing-braces
  -Wno-multichar
  -Wno-tautological-compare
  -Wno-unused-but-set-variable
  -Wno-unused-value
  -Wno-unused-variable
  -fdiagnostics-color=always
  -fms-extensions
  -Wno-microsoft
)

add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-pointer-sign>)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-function>)
add_compile_options(-Wno-format-security -Wno-invalid-token-paste)

# Include directories
set(GEVR_INCLUDES
  "${CMAKE_BINARY_DIR}/port/include"
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/include/PR"
  "${CMAKE_SOURCE_DIR}/port/include"
  "${CMAKE_SOURCE_DIR}/port/fast3d"
  "${CMAKE_SOURCE_DIR}/port/vr"
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/src/game"
  "${CMAKE_SOURCE_DIR}/src/libultra/audio"
  "${CMAKE_SOURCE_DIR}/OpenXR/Include"
  "${CMAKE_SOURCE_DIR}/port/include/net"
  "${CMAKE_SOURCE_DIR}/port/src/net"
)

# Libraries
set(LIBS )

# SDL2 & ZLib
if(GEVR_HEADLESS)
  # A static SDL with its dummy video and audio drivers: the game's own input,
  # audio and system layers compile unchanged and see no devices.
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC_PIC ON CACHE BOOL "" FORCE)
  include(FetchContent)
  FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.32.8
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL2)
  find_package(ZLIB REQUIRED)
  list(APPEND LIBS SDL2::SDL2-static)
else()
  find_package(SDL2 QUIET)
  find_package(ZLIB QUIET)
endif()

if(NOT GEVR_HEADLESS AND (NOT SDL2_FOUND OR NOT ZLIB_FOUND))
  include(cmake/android.cmake)
endif()

if(GEVR_HEADLESS)
  # SDL2::SDL2-static above
elseif(TARGET SDL2::SDL2)
  list(APPEND LIBS SDL2::SDL2)
elseif(SDL2_LIBRARY)
  include_directories(SYSTEM "${SDL2_INCLUDE_DIR}")
  list(APPEND LIBS "${SDL2_LIBRARY}")
endif()

if(TARGET ZLIB::ZLIB)
  list(APPEND LIBS ZLIB::ZLIB)
elseif(ZLIB_LIBRARY)
  include_directories(SYSTEM "${ZLIB_INCLUDE_DIR}")
  list(APPEND LIBS "${ZLIB_LIBRARY}")
endif()

if(NOT GEVR_HEADLESS)
  # Graphics
  list(APPEND LIBS GLESv3 EGL log android)

  # OpenXR Loader
  message(STATUS "OpenXR: Using bundled Quest Android loader")
  add_library(openxr_loader SHARED IMPORTED)
  set_target_properties(openxr_loader PROPERTIES
    IMPORTED_LOCATION
    ${CMAKE_SOURCE_DIR}/OpenXR/Libs/Android/arm64-v8a/Release/libopenxr_loader.so
  )
endif()

# Source files
# gevr_frame_timing.c / gevr_gpu_timer.cpp: per-XR CPU and asynchronous GPU diagnostics
# gevr_hud_bounds.cpp: fenced ammo-crop readback without render-thread pixel waits
# gevr_collision_slide.c: collision-checked clearance retry for local VR edge slides
# gevr_xr_metrics.cpp: optional runtime performance counters for automated profiling
# gevr_surface_probe.c: opt-in issue #30 diagnostics; gevr_locomotion.c: presentation interpolation
file(GLOB_RECURSE SRC_PORT CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/port/*.c")
list(FILTER SRC_PORT EXCLUDE REGEX "port/(external|tests)/.*")
# Exclude PD-specific files not used by GoldenEye
list(FILTER SRC_PORT EXCLUDE REGEX ".*(preprocess|pdmain|pdsched|communityart|mpsetups|optionsmenu).*")
# Perfect Dark's ROM asset pipeline (romdata.c) and mod loader (mod.c) are written
# against Perfect Dark's ROM ids, file table and rzip codec, none of which apply to
# GoldenEye. port/src/gevr_romload.c stands in for them; see that file for what
# a GoldenEye replacement has to do.
list(FILTER SRC_PORT EXCLUDE REGEX "port/src/(romdata|mod)\.c$")
# port/vr/vr_pause_menu.cpp added 2026-10-03; port/vr/vr_screen.cpp added 2026-09-20; gevr_setup.c 2026-09-20; port/fast3d/gevr_texpack.cpp 2026-09-25; port/vr/vr_haptics.cpp 2026-09-29; src/game/gevr_heldgun.c 2026-10-04; port/src/gevr_gex.c, gevr_gexmodel.c, gevr_pdanim.c 2026-10-04; src/game/gevr_bot.c, gevr_sndpath.c 2026-10-07; port/src/gevr_bodyslot.c, src/game/gevr_bodyslots.c 2026-10-08; port/src/gevr_touch_model_data.c, src/game/gevr_touch.c 2026-10-09; port/src/gevr_bean.c, gevr_bean_load.c, gevr_bean_tex.c, gevr_bean_logic.c, gevr_bean_world.c 2026-10-11; port/src/gevr_romlayout.c, gevr_romcampaign.c 2026-10-11; port/src/gevr_speedboat.c, src/game/gevr_dam_boat.c 2026-10-11; port/src/gevr_bean_folder.c, gevr_bean_font.c, gevr_bean_images.c 2026-10-11; port/src/gevr_bean_import.c 2026-10-12; port/src/gevr_bean_ce.c 2026-10-19 (touching this line makes the glob re-run)
# src/game/gevr_ally.c added 2026-10-17 (refresh the game source glob).
file(GLOB_RECURSE SRC_PORT_CXX "${CMAKE_SOURCE_DIR}/port/*.cpp")
list(FILTER SRC_PORT_CXX EXCLUDE REGEX "port/(external|tests)/.*")
# port/server: the dedicated server's main and the headset, renderer and
# launcher entry points it answers in their place (GEVR_HEADLESS only).
list(FILTER SRC_PORT EXCLUDE REGEX "port/server/.*")
list(FILTER SRC_PORT_CXX EXCLUDE REGEX "port/server/.*")
if(GEVR_HEADLESS)
  # Kept from port/vr: the settings (goldeneye-vr.ini), the log and miniz.
  # Everything that draws, reads the headset or runs the launcher is left out;
  # port/src/main.c boots the server as it boots the headset.
  list(FILTER SRC_PORT EXCLUDE REGEX "port/fast3d/.*")
  list(FILTER SRC_PORT_CXX EXCLUDE REGEX "port/fast3d/.*")
  list(FILTER SRC_PORT_CXX EXCLUDE REGEX "port/vr/.*")
  list(APPEND SRC_PORT_CXX "${CMAKE_SOURCE_DIR}/port/vr/vr_settings.cpp")
  file(GLOB SRC_SERVER CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/port/server/*.c" "${CMAKE_SOURCE_DIR}/port/server/*.cpp")
  list(APPEND SRC_PORT ${SRC_SERVER})
endif()

# Dab's Fast3D build restores inlining locally; keep the original game's
# no-inlining/aliasing/arithmetic flags and floating-point semantics intact.
# OFF provides a compiler-only A/B control for Quest profiling.
option(GEVR_FAST3D_INLINE "Allow Fast3D function inlining in optimized builds" ON)
if(GEVR_FAST3D_INLINE)
  set(SRC_FAST3D_CXX ${SRC_PORT_CXX})
  list(FILTER SRC_FAST3D_CXX INCLUDE REGEX "port/fast3d/[^/]+\\.cpp$")
  set_property(SOURCE ${SRC_FAST3D_CXX} APPEND PROPERTY COMPILE_OPTIONS
    "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-finline-functions>")
endif()

file(GLOB_RECURSE SRC_GAME CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/game/*.c")

# indy_comms.c drives the cartridge port to talk to an SGI Indy dev host.
# ob.c still routes unbound files through it, so on a headset it faults.
# port/src/gevr_indy_stub.c reports failure instead.
list(FILTER SRC_GAME EXCLUDE REGEX "src/game/indy_comms\.c$")

# GoldenEye's own engine layer: boot (boss.c), scheduler (sched.c), frame
# rate (fr.c), memory, ROM and video plumbing. Only src/game was globbed
# before, which is why bossEntry, osCreateScheduler, g_StageNum and g_ViModes
# had no definitions to link against.
#
# Excluded: these drive N64 hardware the headset does not have, and the port
# layer already provides the service in each case.
#   crash.c                - do not compile yet
#   sched.c                - RSP task + VI scheduling; the port drives frames
#   motor.c                - rumble pak; port/src/libultra.c stubs osPfs*
#   tlb_manage.c           - MIPS TLB
#   rmon.c                 - the N64 remote debug monitor
#   init.c                 - N64 boot: osInitialize, PI manager, TLB setup.
#                            The port boots through port/src/main.c instead.
# Audio (music.c, snd.c, audi.c) is linked; soft mixer + SDL playback.
file(GLOB SRC_ENGINE "${CMAKE_SOURCE_DIR}/src/*.c")
list(FILTER SRC_ENGINE EXCLUDE REGEX
     "src/(crash|sched|motor|tlb_manage|init|rmon)\.c$")
# GoldenEye's game data: level backgrounds, character and weapon models,
# animation tables and fonts. The decomp links these into the binary rather
# than reading them out of a ROM at runtime, and they were never globbed,
# which left roughly 900 asset symbols undefined at link time.
#
# *.inc.c files are #included by the others and are not compiled on their own.
#
# Only the decompiled render-state lists and image tables are linked: every
# piece of Rare's actual content - animations, fonts, glyphs, briefings, the
# Rareware logo, level data, text - is read out of the player's own cartridge
# at runtime (gevr_romload.c, gevr_rarelogo.c), so the app carries none of it.
set(SRC_ASSETS
  "${CMAKE_SOURCE_DIR}/assets/font_dl.c"
  "${CMAKE_SOURCE_DIR}/assets/oddtextures.c"
)

# obseg/{stan,bg,setup,text} hold one ROM segment per level, and each segment
# defines the same global names (intro, propDefs, pathwaypoints, padlist...).
# They are meant to be loaded one at a time at runtime, not linked together -
# 3431 symbol names collide across them. They need the ROM/file loader that
# port/src/gevr_romload.c describes; until then they stay out of the link.
list(FILTER SRC_ASSETS EXCLUDE REGEX "assets/obseg/(stan|bg|setup|text)/")

file(GLOB SRC_INFLATE "${CMAKE_SOURCE_DIR}/src/inflate/*.c")
file(GLOB SRC_LIB_GU "${CMAKE_SOURCE_DIR}/src/libultra/gu/*.c")
file(GLOB SRC_LIB_AUDIO "${CMAKE_SOURCE_DIR}/src/libultra/audio/*.c")
file(GLOB SRC_LIB_AUDIO_RE "${CMAKE_SOURCE_DIR}/src/libultrare/audio/*.c")
# Host-endian ALBank conversion used when binding music ctl banks from ROM.
set(SRC_SEGAUDIO
  "${CMAKE_SOURCE_DIR}/port/src/preprocess/segaudio.c"
  "${CMAKE_SOURCE_DIR}/port/src/preprocess/common.c"
)

set(SRC_NET
  "${CMAKE_SOURCE_DIR}/port/external/enet.c"
  "${CMAKE_SOURCE_DIR}/port/src/net/netbuf.c"
  "${CMAKE_SOURCE_DIR}/port/src/net/net_core.c"
  "${CMAKE_SOURCE_DIR}/port/src/net/net_objects.c"
  "${CMAKE_SOURCE_DIR}/port/src/net/net_match.c"
  "${CMAKE_SOURCE_DIR}/port/src/net/net_coop.c"
  "${CMAKE_SOURCE_DIR}/port/src/net/net_coop_menu.c"
  "${CMAKE_SOURCE_DIR}/port/src/net/net_voice.c"
  "${CMAKE_SOURCE_DIR}/port/src/net/net_spatial.c"
  "${CMAKE_SOURCE_DIR}/port/src/net/net_discovery.c"
  "${CMAKE_SOURCE_DIR}/port/src/net/net_player_sync.c"
  "${CMAKE_SOURCE_DIR}/port/src/net/net_ice.cpp"
)

# Igor Pavlov's LZMA SDK (public domain), Dab's Mod's cut-down copy: reads the
# player's own GoldenEye XBLA .7z (port/src/gevr_bean_import.c)
set(SRC_LZMA)
foreach(name 7zAlloc 7zBuf 7zCrc 7zCrcOpt 7zDec 7zFile 7zIn 7zStream Alloc Bcj2 Bra Bra86 BraIA64 CpuArch Delta Lzma2Dec LzmaDec)
  list(APPEND SRC_LZMA "${CMAKE_SOURCE_DIR}/port/external/lzma/${name}.c")
endforeach()

# The patch half of HDiffPatch (port/external/hdiffpatch, MIT), Dab's Mod's copy
# with its glue: applies the GoldenEye XBLA Community Edition updater the player
# supplies to their imported release (port/src/gevr_bean_ce.c). Its files include
# one another from its own root; built without its threads and window diffs.
set(HDP_DIR "${CMAKE_SOURCE_DIR}/port/external/hdiffpatch")
set(SRC_HDP
  "${HDP_DIR}/hdpglue.c"
  "${HDP_DIR}/file_for_patch.c"
  "${HDP_DIR}/libHDiffPatch/HPatch/patch.c"
  "${HDP_DIR}/libHDiffPatch/HDiff/private_diff/limit_mem_diff/adler_roll.c"
  "${HDP_DIR}/dirDiffPatch/dir_patch/dir_patch.c"
  "${HDP_DIR}/dirDiffPatch/dir_patch/dir_patch_tools.c"
  "${HDP_DIR}/dirDiffPatch/dir_patch/new_dir_output.c"
  "${HDP_DIR}/dirDiffPatch/dir_patch/new_stream.c"
  "${HDP_DIR}/dirDiffPatch/dir_patch/ref_stream.c"
  "${HDP_DIR}/dirDiffPatch/dir_patch/res_handle_limit.c"
)
set_source_files_properties(${SRC_HDP} PROPERTIES
  INCLUDE_DIRECTORIES "${HDP_DIR}"
  COMPILE_DEFINITIONS "_GNU_SOURCE;_FILE_OFFSET_BITS=64;_IS_USED_MULTITHREAD=0;_IS_NEED_WINDOW_DIFF=0"
  COMPILE_OPTIONS "-w;-O2"
)

include(FetchContent)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "Do not build Opus tests" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "Do not build Opus tools" FORCE)
set(OPUS_INSTALL OFF CACHE BOOL "Do not install Opus" FORCE)
FetchContent_Declare(opus
  URL https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz
  URL_HASH SHA256=6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1)
FetchContent_MakeAvailable(opus)
target_compile_options(opus PRIVATE $<$<CONFIG:Debug>:-O2>)

set(NO_SERVER ON CACHE BOOL "Build only the libjuice ICE client" FORCE)
set(NO_TESTS ON CACHE BOOL "Do not build libjuice tests" FORCE)
add_subdirectory(port/external/libjuice)

set(SRC
  ${SRC_PORT}
  ${SRC_PORT_CXX}
  ${SRC_NET}
  ${SRC_GAME}
  ${SRC_ENGINE}
  ${SRC_ASSETS}
  ${SRC_INFLATE}
  ${SRC_LIB_GU}
  ${SRC_LIB_AUDIO}
  ${SRC_LIB_AUDIO_RE}
  ${SRC_SEGAUDIO}
  ${SRC_LZMA}
  ${SRC_HDP}
)

# Build id for the launcher (port/cmake/buildid.cmake), regenerated every build.
set(GEVR_BUILDID_C "${CMAKE_BINARY_DIR}/gevr_buildid.c")
add_custom_target(gevr_buildid
  COMMAND "${CMAKE_COMMAND}" -DSRC_DIR=${CMAKE_SOURCE_DIR} -DOUT=${GEVR_BUILDID_C}
          -P "${CMAKE_SOURCE_DIR}/port/cmake/buildid.cmake"
  BYPRODUCTS "${GEVR_BUILDID_C}"
  VERBATIM)
list(APPEND SRC "${GEVR_BUILDID_C}")

if(GEVR_HEADLESS)
  # The dedicated server: one executable, no Steam Audio (net_spatial.c falls
  # back without it) and no OpenXR loader.
  add_executable(gevr-server ${SRC})
  add_dependencies(gevr-server gevr_buildid)
  target_include_directories(gevr-server PRIVATE ${GEVR_INCLUDES})
  target_compile_options(gevr-server PRIVATE -include "${CMAKE_BINARY_DIR}/port/include/versioninfo.h")
  target_link_libraries(gevr-server ${LIBS} juice-static opus gevr_physics m pthread dl)
  return()
endif()

# Output target
add_library(gevr SHARED ${SRC})
add_dependencies(gevr gevr_buildid)
target_include_directories(gevr PRIVATE ${GEVR_INCLUDES})
target_compile_options(gevr PRIVATE -include "${CMAKE_BINARY_DIR}/port/include/versioninfo.h")
add_library(steamaudio SHARED IMPORTED)
set_target_properties(steamaudio PROPERTIES IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/port/external/steamaudio/lib/android-armv8/libphonon.so")
target_include_directories(gevr PRIVATE "${CMAKE_SOURCE_DIR}/port/external/steamaudio/include")
target_compile_definitions(gevr PRIVATE GEVR_STEAMAUDIO=1)
target_link_libraries(gevr ${LIBS} openxr_loader juice-static opus steamaudio gevr_physics)
target_link_options(gevr PRIVATE -Wl,--error-limit=0)

# Alias for legacy Android loader references.
add_library(pd ALIAS gevr)
