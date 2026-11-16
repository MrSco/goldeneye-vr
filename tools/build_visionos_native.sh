#!/usr/bin/env bash
# Builds Vision Pro's native game library and app (docs/visionos-native.md):
# the PC VR game with port/visionos's OpenXR runtime over Compositor Services,
# OpenGL ES through ANGLE. (tools/build_visionos.sh is the other Vision Pro
# build: the Quest APK in Klepton.)
#
#     tools/build_visionos_native.sh [simulator|device] [--app]
#
# Needs: Xcode with the visionOS SDK, cmake + ninja (Homebrew), xcodegen for
# --app, and ANGLE's visionOS slices in $ANGLE_DIR (default ~/angle;
# tools/build_angle_apple.sh makes them from the iOS ones).
#
# Output: build/visionos-<kind>/libgevr_visionos_all.a; with --app,
# port/visionos/GoldenEyeVR.xcodeproj is generated and the app built into
# build/visionos-<kind>/app. Signing for a device build comes from the
# environment: DEVELOPMENT_TEAM, and GEVR_SIGN (xcodebuild's signing
# arguments) as for tools/build_ios.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
export PATH="/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin:$PATH"

KIND="${1:-simulator}"
APP=0
[ "${2:-}" = "--app" ] && APP=1
case "$KIND" in
  simulator) SYSROOT=xrsimulator; DEST="generic/platform=visionOS Simulator" ;;
  device)    SYSROOT=xros;        DEST="generic/platform=visionOS" ;;
  *) echo "usage: $0 [simulator|device] [--app]" >&2; exit 1 ;;
esac
ANGLE_DIR="${ANGLE_DIR:-$HOME/angle}"
BUILD="build/visionos-$KIND"

cmake -S . -B "$BUILD" -G Ninja \
  -DCMAKE_SYSTEM_NAME=visionOS \
  -DCMAKE_OSX_SYSROOT="$SYSROOT" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DANGLE_DIR="$ANGLE_DIR"
cmake --build "$BUILD" --target gevr_visionos
echo "library: $BUILD/libgevr_visionos_all.a"

if [ "$APP" = 1 ]; then
  [ -d "$ANGLE_DIR/xcframeworks-visionos/libEGL.xcframework" ] ||
    { echo "no ANGLE visionOS slices in $ANGLE_DIR: run tools/build_angle_apple.sh" >&2; exit 1; }
  # the asset catalog (generated, git-ignored): the layered icon Klepton's build uses too
  ASSETS="$ROOT/port/visionos/Assets.xcassets"
  rm -rf "$ASSETS"
  mkdir -p "$ASSETS"
  printf '{ "info" : { "author" : "xcode", "version" : 1 } }\n' > "$ASSETS/Contents.json"
  cp -R port/visionos/AppIcon-gevr.solidimagestack "$ASSETS/AppIcon.solidimagestack"
  NAME=$(sed -n 's/.*versionName "\([^"]*\)".*/\1/p' android/app/build.gradle)
  export ANGLE_DIR GEVR_ROOT="$ROOT" GEVR_VERSION="${NAME%%-*}"
  export GEVR_BUILD="${GEVR_BUILD:-$(sed -n 's/.*versionCode \([0-9]*\).*/\1/p' android/app/build.gradle)}"
  export GEVR_BUNDLE_ID="${GEVR_BUNDLE_ID:-com.mrsco.goldeneyevr}"
  export DEVELOPMENT_TEAM="${DEVELOPMENT_TEAM:-}"
  ( cd port/visionos && xcodegen generate --quiet )
  xcodebuild -project port/visionos/GoldenEyeVR.xcodeproj -scheme GoldenEyeVR -configuration Release \
    -sdk "$SYSROOT" -destination "$DEST" -derivedDataPath "$BUILD/dd" \
    CONFIGURATION_BUILD_DIR="$ROOT/$BUILD/app" ${GEVR_SIGN:-CODE_SIGNING_ALLOWED=NO} build
  echo "app: $BUILD/app/GoldenEyeVR.app"
fi
