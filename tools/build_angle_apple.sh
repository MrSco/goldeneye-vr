#!/usr/bin/env bash
# Builds ANGLE (OpenGL ES on Metal) for the Apple builds: iOS device, iOS
# simulator and macOS arm64, release, Metal backend only.
#
#     tools/build_angle_apple.sh [ios-device|ios-simulator|mac ...]
#
# ANGLE lives in ONE place on the Mac, shared by every project, never in this
# repo: $ANGLE_DIR (default ~/angle). The first run fetches the ANGLE checkout
# with Chromium's depot_tools (about 15 GB, an hour or more); later runs only
# build. Each slice lands in $ANGLE_DIR/out/<slice>; the iOS slices are
# libEGL.framework + libGLESv2.framework (and both together as
# $ANGLE_DIR/xcframeworks/*.xcframework), the macOS slice is dylibs.
#
# The revision is the one rebelancap's iOS ports (perfect-dark-ios and its
# siblings) ship: ANGLE 2.1.1, e25e8d8565f7. The gn args follow theirs, plus
# use_lld = false: that revision's lld cannot read the newer SDKs' .tbd stubs
# (Xcode 27: "unknown target"), so Xcode's own linker links it.
set -euo pipefail

ANGLE_DIR="${ANGLE_DIR:-$HOME/angle}"
ANGLE_REV="${ANGLE_REV:-e25e8d8565f7}"
SLICES="${*:-ios-device ios-simulator}"
TOOLS="$ANGLE_DIR/depot_tools"
SRC="$ANGLE_DIR/src"
export PATH="$TOOLS:$PATH"

mkdir -p "$ANGLE_DIR"
if [ ! -x "$TOOLS/gclient" ]; then
  git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "$TOOLS"
fi
if [ ! -d "$SRC/.git" ]; then
  git clone https://chromium.googlesource.com/angle/angle "$SRC"
fi
cd "$SRC"
if [ "$(git rev-parse --short=12 HEAD)" != "$ANGLE_REV" ] || [ ! -f "$SRC/.gclient_synced" ]; then
  git fetch origin
  git checkout --detach "$ANGLE_REV"
  [ -f .gclient ] || python3 scripts/bootstrap.py
  DEPOT_TOOLS_UPDATE=1 gclient sync -D --no-history
  echo "$ANGLE_REV" > "$SRC/.gclient_synced"
fi
export DEPOT_TOOLS_UPDATE=0

for slice in $SLICES; do
  OUT="$ANGLE_DIR/out/$slice"
  mkdir -p "$OUT"
  case "$slice" in
    ios-device)    TARGET='target_os = "ios"
target_environment = "device"
ios_deployment_target = "15.0"
ios_enable_code_signing = false' ;;
    ios-simulator) TARGET='target_os = "ios"
target_environment = "simulator"
ios_deployment_target = "15.0"
ios_enable_code_signing = false' ;;
    mac)           TARGET='target_os = "mac"
mac_deployment_target = "13.0"' ;;
    *) echo "unknown slice $slice" >&2; exit 1 ;;
  esac
  cat > "$OUT/args.gn" <<EOF
$TARGET
target_cpu = "arm64"
is_debug = false
is_component_build = false
angle_enable_metal = true
angle_enable_gl = false
angle_enable_vulkan = false
angle_enable_d3d11 = false
angle_enable_null = false
angle_enable_swiftshader = false
angle_enable_essl = true
angle_enable_glsl = true
angle_assert_always_on = false
angle_build_tests = false
treat_warnings_as_errors = false
use_lld = false
EOF
  gn gen "$OUT"
  autoninja -C "$OUT" libEGL libGLESv2
  echo "built $OUT"
done

# The headers the game compiles against.
rm -rf "$ANGLE_DIR/include"
cp -R "$SRC/include" "$ANGLE_DIR/include"

# The iOS slices as xcframeworks, so Xcode picks device or simulator itself
# (port/ios/project.yml); a plain .framework path cannot.
if [ -d "$ANGLE_DIR/out/ios-device/libEGL.framework" ] && [ -d "$ANGLE_DIR/out/ios-simulator/libEGL.framework" ]; then
  mkdir -p "$ANGLE_DIR/xcframeworks"
  for lib in libEGL libGLESv2; do
    rm -rf "$ANGLE_DIR/xcframeworks/$lib.xcframework"
    xcodebuild -create-xcframework       -framework "$ANGLE_DIR/out/ios-device/$lib.framework"       -framework "$ANGLE_DIR/out/ios-simulator/$lib.framework"       -output "$ANGLE_DIR/xcframeworks/$lib.xcframework"
  done
fi
echo "ANGLE ready in $ANGLE_DIR (set ANGLE_DIR for the iOS build if you moved it)"
