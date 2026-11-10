#!/usr/bin/env bash
# Builds the iOS game library and the Xcode app (docs/mobile-platforms.md).
#
#     tools/build_ios.sh [simulator|device] [--app]
#
# Needs: Xcode, cmake + ninja (Homebrew), xcodegen for --app, and ANGLE in
# $ANGLE_DIR (default ~/angle; tools/build_angle_apple.sh builds it once for
# every project on the Mac).
#
# Output: build/ios-<kind>/libgevr_ios_all.a (the game, SDL, opus, libjuice and
# the physics in one archive); with --app, port/ios/GoldenEyeVR.xcodeproj is
# generated and the app built into build/ios-<kind>/app.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
export PATH="/opt/homebrew/bin:$PATH"

KIND="${1:-simulator}"
APP=0
[ "${2:-}" = "--app" ] && APP=1
case "$KIND" in
  simulator) SYSROOT=iphonesimulator; SLICE=ios-simulator ;;
  device)    SYSROOT=iphoneos;        SLICE=ios-device ;;
  *) echo "usage: $0 [simulator|device] [--app]" >&2; exit 1 ;;
esac
ANGLE_DIR="${ANGLE_DIR:-$HOME/angle}"
BUILD="build/ios-$KIND"

cmake -S . -B "$BUILD" -G Ninja \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT="$SYSROOT" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DANGLE_DIR="$ANGLE_DIR"
cmake --build "$BUILD" --target gevr_ios
echo "library: $BUILD/libgevr_ios_all.a"

if [ "$APP" = 1 ]; then
  NAME=$(sed -n 's/.*versionName "\([^"]*\)".*/\1/p' android/app/build.gradle)
  export ANGLE_DIR GEVR_ROOT="$ROOT" GEVR_VERSION="${NAME%%-*}"
  export GEVR_BUILD=$(sed -n 's/.*versionCode \([0-9]*\).*/\1/p' android/app/build.gradle)
  export DEVELOPMENT_TEAM="${DEVELOPMENT_TEAM:-}"
  ( cd port/ios && xcodegen generate --quiet )
  DEST="generic/platform=iOS Simulator"
  [ "$KIND" = device ] && DEST="generic/platform=iOS"
  xcodebuild -project port/ios/GoldenEyeVR.xcodeproj -scheme GoldenEyeVR -configuration Release \
    -sdk "$SYSROOT" -destination "$DEST" -derivedDataPath "$BUILD/dd" \
    CONFIGURATION_BUILD_DIR="$ROOT/$BUILD/app" ${GEVR_SIGN:-CODE_SIGNING_ALLOWED=NO} build
  echo "app: $BUILD/app/GoldenEyeVR.app"
fi
