cmake_minimum_required(VERSION 3.16)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

project(gevr C CXX)

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

if(WIN32)
  set(TARGET_OS "windows")
elseif(ANDROID)
  set(TARGET_OS "android")
elseif(APPLE)
  set(TARGET_OS "osx")
elseif(UNIX)
  set(TARGET_OS "linux")
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
)

if(TARGET_IS_64BIT)
  add_definitions(-DPLATFORM_64BIT=1)
endif()

if(ANDROID)
  add_definitions(
    -DANDROID=1
    -DXR_USE_PLATFORM_ANDROID=1
    -DXR_USE_GRAPHICS_API_OPENGL_ES=1
  )
elseif(WIN32)
  add_definitions(
    -DXR_USE_PLATFORM_WIN32=1
    -DXR_USE_GRAPHICS_API_OPENGL=1
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

if(ANDROID)
  add_compile_options(-Wno-format-security -Wno-invalid-token-paste)
endif()

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
)

# Libraries
set(LIBS )

# SDL2 & ZLib
find_package(SDL2 QUIET)
find_package(ZLIB QUIET)

if(NOT SDL2_FOUND OR NOT ZLIB_FOUND)
  include(cmake/android.cmake)
endif()

if(TARGET SDL2::SDL2)
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

# Graphics
if(ANDROID)
  list(APPEND LIBS GLESv3 EGL log android)
elseif(WIN32)
  list(APPEND LIBS opengl32 dbghelp -static-libstdc++)
else()
  list(APPEND LIBS GL stdc++ m dl)
endif()

# OpenXR Loader
if(ANDROID)
  message(STATUS "OpenXR: Using bundled Quest Android loader")
  add_library(openxr_loader SHARED IMPORTED)
  set_target_properties(openxr_loader PROPERTIES
    IMPORTED_LOCATION
    ${CMAKE_SOURCE_DIR}/OpenXR/Libs/Android/arm64-v8a/Release/libopenxr_loader.so
  )
elseif(WIN32)
  add_library(openxr_loader SHARED IMPORTED)
  set_target_properties(openxr_loader PROPERTIES
    IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/OpenXR/Libs/Windows/x64/openxr_loader.dll
    IMPORTED_IMPLIB   ${CMAKE_SOURCE_DIR}/OpenXR/Libs/Windows/x64/openxr_loader.lib
  )
endif()

# Source files
file(GLOB_RECURSE SRC_PORT "${CMAKE_SOURCE_DIR}/port/*.c")
# Exclude PD-specific files not used by GoldenEye
list(FILTER SRC_PORT EXCLUDE REGEX ".*(preprocess|pdmain|pdsched|communityart|mpsetups|optionsmenu).*")
# Perfect Dark's ROM asset pipeline (romdata.c) and mod loader (mod.c) are written
# against Perfect Dark's ROM ids, file table and rzip codec, none of which apply to
# GoldenEye. port/src/gevr_romload.c stands in for them; see that file for what
# a GoldenEye replacement has to do.
list(FILTER SRC_PORT EXCLUDE REGEX "port/src/(romdata|mod)\.c$")
# port/vr/vr_screen.cpp added 2026-09-20; gevr_setup.c 2026-09-20 (touching this line makes the glob re-run)
file(GLOB_RECURSE SRC_PORT_CXX "${CMAKE_SOURCE_DIR}/port/*.cpp")

# Exclude standalone Windows probe cpp files from port
list(FILTER SRC_PORT_CXX EXCLUDE REGEX ".*xr_probe\\.cpp$")

file(GLOB_RECURSE SRC_GAME "${CMAKE_SOURCE_DIR}/src/game/*.c")

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
file(GLOB_RECURSE SRC_ASSETS "${CMAKE_SOURCE_DIR}/assets/*.c")
list(FILTER SRC_ASSETS EXCLUDE REGEX "\.inc\.c$")

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

set(SRC
  ${SRC_PORT}
  ${SRC_PORT_CXX}
  ${SRC_GAME}
  ${SRC_ENGINE}
  ${SRC_ASSETS}
  ${SRC_INFLATE}
  ${SRC_LIB_GU}
  ${SRC_LIB_AUDIO}
  ${SRC_LIB_AUDIO_RE}
  ${SRC_SEGAUDIO}
)

# Output target
if(ANDROID)
  add_library(gevr SHARED ${SRC})
  target_include_directories(gevr PRIVATE ${GEVR_INCLUDES})
  target_compile_options(gevr PRIVATE -include "${CMAKE_BINARY_DIR}/port/include/versioninfo.h")
  target_link_libraries(gevr ${LIBS} openxr_loader)
  target_link_options(gevr PRIVATE -Wl,--error-limit=0)

  # Also build pd alias so that any legacy loader references succeed
  add_library(pd ALIAS gevr)
else()
  add_executable(gevr ${SRC})
  target_include_directories(gevr PRIVATE ${GEVR_INCLUDES})
  target_compile_options(gevr PRIVATE -include "${CMAKE_BINARY_DIR}/port/include/versioninfo.h")
  target_link_libraries(gevr ${LIBS})
  if(WIN32)
    target_link_libraries(gevr openxr_loader urlmon)
  endif()
endif()
