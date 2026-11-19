#!/usr/bin/env bash
# Builds the Apple TV game library and the Xcode app (docs/apple-tv.md).
#
#     tools/build_tvos.sh [simulator|device] [--app]
#
# Needs what tools/build_ios.sh needs: Xcode with the tvOS platform, cmake +
# ninja (Homebrew), xcodegen for --app, and ANGLE's Apple TV slices in
# $ANGLE_DIR (default ~/angle; tools/build_angle_apple.sh tvos-device
# tvos-simulator builds them once, beside the iOS ones).
#
# Output: build/tvos-<kind>/libgevr_ios_all.a (the iOS build's library, built
# for tvOS: the game, SDL, opus, libjuice and the physics in one archive); with
# --app, port/tvos/GoldenEyeVR.xcodeproj is generated and the app built into
# build/tvos-<kind>/app.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
export PATH="/opt/homebrew/bin:$PATH"

KIND="${1:-simulator}"
APP=0
[ "${2:-}" = "--app" ] && APP=1
case "$KIND" in
  simulator) SYSROOT=appletvsimulator ;;
  device)    SYSROOT=appletvos ;;
  *) echo "usage: $0 [simulator|device] [--app]" >&2; exit 1 ;;
esac
ANGLE_DIR="${ANGLE_DIR:-$HOME/angle}"
if [ ! -d "$ANGLE_DIR/xcframeworks-tvos/libEGL.xcframework" ]; then
  echo "No Apple TV ANGLE in $ANGLE_DIR/xcframeworks-tvos: run tools/build_angle_apple.sh tvos-device tvos-simulator" >&2
  exit 1
fi
BUILD="build/tvos-$KIND"

cmake -S . -B "$BUILD" -G Ninja \
  -DCMAKE_SYSTEM_NAME=tvOS \
  -DCMAKE_OSX_SYSROOT="$SYSROOT" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DANGLE_DIR="$ANGLE_DIR"
cmake --build "$BUILD" --target gevr_ios
echo "library: $BUILD/libgevr_ios_all.a"

if [ "$APP" = 1 ]; then
  NAME=$(sed -n 's/.*versionName "\([^"]*\)".*/\1/p' android/app/build.gradle)
  export ANGLE_DIR GEVR_ROOT="$ROOT" GEVR_VERSION="${NAME%%-*}"
  # the Android versionCode, unless an App Store upload needs the next number
  export GEVR_BUILD="${GEVR_BUILD:-$(sed -n 's/.*versionCode \([0-9]*\).*/\1/p' android/app/build.gradle)}"
  export DEVELOPMENT_TEAM="${DEVELOPMENT_TEAM:-}"
  ( cd port/tvos && xcodegen generate --quiet )
  DEST="generic/platform=tvOS Simulator"
  [ "$KIND" = device ] && DEST="generic/platform=tvOS"
  xcodebuild -project port/tvos/GoldenEyeVR.xcodeproj -scheme GoldenEyeVR -configuration Release \
    -sdk "$SYSROOT" -destination "$DEST" -derivedDataPath "$BUILD/dd" \
    CONFIGURATION_BUILD_DIR="$ROOT/$BUILD/app" ${GEVR_SIGN:-CODE_SIGNING_ALLOWED=NO} build
  echo "app: $BUILD/app/GoldenEyeVR.app"
fi
