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
#
#     tools/build_tvos.sh device --archive <build> [--upload]
#
# makes the App Store archive and .ipa with build number <build> (above every
# build already sent for the app's tvOS platform); --upload sends it to
# TestFlight. Signing as tools/build_visionos_native.sh (nothing about the
# account lives in this repository): GEVR_TEAM, GEVR_PROFILE (an App Store
# profile for tvOS: the iPhone and Vision Pro one does not cover Apple TV),
# GEVR_BUNDLE_ID, GEVR_KEYCHAIN, GEVR_KEYCHAIN_PASS, and for the upload
# GEVR_ASC_KEY_DIR, GEVR_ASC_KEY_ID, GEVR_ASC_ISSUER.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
export PATH="/opt/homebrew/bin:$PATH"

KIND="${1:-simulator}"
APP=0
ARCHIVE=""
UPLOAD=""
[ "${2:-}" = "--app" ] && APP=1
if [ "${2:-}" = "--archive" ]; then
  [ "$KIND" = device ] && [ -n "${3:-}" ] || { echo "usage: $0 device --archive <build> [--upload]" >&2; exit 1; }
  APP=1; ARCHIVE="$3"; UPLOAD="${4:-}"
  : "${GEVR_TEAM:?}" "${GEVR_PROFILE:?}" "${GEVR_BUNDLE_ID:?}" "${GEVR_KEYCHAIN:?}" "${GEVR_KEYCHAIN_PASS:?}"
  export GEVR_BUILD="$ARCHIVE" DEVELOPMENT_TEAM="$GEVR_TEAM"
fi
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
  # iCloud sync (docs/icloud-sync.md): GEVR_ICLOUD=1 on a signed build adds
  # port/ios/gevr-icloud.entitlements (the App ID and profile must have iCloud)
  if [ "${GEVR_ICLOUD:-0}" = 1 ] && { [ -n "${GEVR_SIGN:-}" ] || [ -n "${ARCHIVE:-}" ]; }; then
    export GEVR_ICLOUD=YES GEVR_ENTITLEMENTS="$ROOT/port/ios/gevr-icloud.entitlements"
    GEVR_ICLOUD_EXPORT='<key>iCloudContainerEnvironment</key><string>Production</string>'
  else
    export GEVR_ICLOUD=NO GEVR_ENTITLEMENTS=""
    GEVR_ICLOUD_EXPORT=''
  fi
  ( cd port/tvos && xcodegen generate --quiet )
  DEST="generic/platform=tvOS Simulator"
  [ "$KIND" = device ] && DEST="generic/platform=tvOS"
  # one paired Apple TV (GEVR_DEST=id=<udid>): a development build for it, so
  # -allowProvisioningDeviceRegistration can add it to the team's profile
  DEST="${GEVR_DEST:-$DEST}"
  if [ -z "$ARCHIVE" ]; then
    xcodebuild -project port/tvos/GoldenEyeVR.xcodeproj -scheme GoldenEyeVR -configuration Release \
      -sdk "$SYSROOT" -destination "$DEST" -derivedDataPath "$BUILD/dd" \
      CONFIGURATION_BUILD_DIR="$ROOT/$BUILD/app" ${GEVR_SIGN:-CODE_SIGNING_ALLOWED=NO} build
    echo "app: $BUILD/app/GoldenEyeVR.app"
    exit 0
  fi
  # the App Store archive and .ipa (tools/build_visionos_native.sh's recipe)
  OUT="$ROOT/$BUILD/testflight-$GEVR_VERSION-$ARCHIVE"
  rm -rf "$OUT"; mkdir -p "$OUT"
  security unlock-keychain -p "$(cat "$GEVR_KEYCHAIN_PASS")" "$GEVR_KEYCHAIN"
  xcodebuild archive -project port/tvos/GoldenEyeVR.xcodeproj -scheme GoldenEyeVR -configuration Release \
    -destination "$DEST" -archivePath "$OUT/GoldenEyeVR.xcarchive" -derivedDataPath "$BUILD/dd-archive" \
    CODE_SIGN_STYLE=Manual DEVELOPMENT_TEAM="$GEVR_TEAM" CODE_SIGN_IDENTITY="Apple Distribution" \
    PROVISIONING_PROFILE_SPECIFIER="$GEVR_PROFILE" \
    OTHER_CODE_SIGN_FLAGS="--keychain $GEVR_KEYCHAIN" 2>&1 | grep -E 'error:|\*\* ARCHIVE' || true
  [ -d "$OUT/GoldenEyeVR.xcarchive" ] || { echo "!! archive failed"; exit 1; }
  cat > "$OUT/ExportOptions.plist" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>method</key><string>app-store-connect</string>
  <key>teamID</key><string>$GEVR_TEAM</string>
  <key>signingStyle</key><string>manual</string>
  <key>signingCertificate</key><string>Apple Distribution</string>
  <key>provisioningProfiles</key><dict><key>$GEVR_BUNDLE_ID</key><string>$GEVR_PROFILE</string></dict>
  <key>uploadSymbols</key><false/>
  ${GEVR_ICLOUD_EXPORT}
</dict></plist>
PL
  xcodebuild -exportArchive -archivePath "$OUT/GoldenEyeVR.xcarchive" -exportPath "$OUT/export" \
    -exportOptionsPlist "$OUT/ExportOptions.plist" 2>&1 | grep -E 'error|EXPORT' || true
  # named for the app's display name ("GoldenEye VR.ipa"), not its product
  IPA="$(ls "$OUT/export"/*.ipa 2>/dev/null | head -1)"
  [ -f "$IPA" ] || { echo "!! export failed"; exit 1; }
  echo "ipa: $IPA"
  if [ "$UPLOAD" = --upload ]; then
    : "${GEVR_ASC_KEY_DIR:?}" "${GEVR_ASC_KEY_ID:?}" "${GEVR_ASC_ISSUER:?}"
    for step in --validate-app --upload-app; do
      API_PRIVATE_KEYS_DIR="$GEVR_ASC_KEY_DIR" xcrun altool $step -f "$IPA" -t appletvos \
        --apiKey "$GEVR_ASC_KEY_ID" --apiIssuer "$(cat "$GEVR_ASC_ISSUER")" 2>&1 \
        | grep -E 'description|No errors|SUCCEEDED|Failed|ERROR' | sort -u
    done
    rm -rf "$OUT/export"   # the archive stays: its dSYMs symbolicate the build's crashes
  fi
fi
