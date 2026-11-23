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
#
#     tools/build_visionos_native.sh device --archive <build> [--upload]
#
# makes the App Store archive and .ipa with build number <build> (above every
# build already sent for the app's visionOS platform, the Klepton build's
# included); --upload sends it to TestFlight. Signing as tools/build_visionos.sh
# (nothing about the account lives in this repository): GEVR_TEAM, GEVR_PROFILE,
# GEVR_BUNDLE_ID, GEVR_KEYCHAIN, GEVR_KEYCHAIN_PASS, and for the upload
# GEVR_ASC_KEY_DIR, GEVR_ASC_KEY_ID, GEVR_ASC_ISSUER.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
export PATH="/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin:$PATH"

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
  # iCloud sync (docs/icloud-sync.md): GEVR_ICLOUD=1 on a signed build adds
  # port/ios/gevr-icloud.entitlements (the App ID and profile must have iCloud)
  if [ "${GEVR_ICLOUD:-0}" = 1 ] && { [ -n "${GEVR_SIGN:-}" ] || [ -n "${ARCHIVE:-}" ]; }; then
    export GEVR_ICLOUD=YES GEVR_ENTITLEMENTS="$ROOT/port/ios/gevr-icloud.entitlements"
    GEVR_ICLOUD_EXPORT='<key>iCloudContainerEnvironment</key><string>Production</string>'
  else
    export GEVR_ICLOUD=NO GEVR_ENTITLEMENTS=""
    GEVR_ICLOUD_EXPORT=''
  fi
  ( cd port/visionos && xcodegen generate --quiet )
  if [ -z "$ARCHIVE" ]; then
    xcodebuild -project port/visionos/GoldenEyeVR.xcodeproj -scheme GoldenEyeVR -configuration Release \
      -sdk "$SYSROOT" -destination "$DEST" -derivedDataPath "$BUILD/dd" \
      CONFIGURATION_BUILD_DIR="$ROOT/$BUILD/app" ${GEVR_SIGN:-CODE_SIGNING_ALLOWED=NO} build
    echo "app: $BUILD/app/GoldenEyeVR.app"
    exit 0
  fi
  # the App Store archive and .ipa (tools/build_visionos.sh's recipe)
  OUT="$ROOT/$BUILD/testflight-$GEVR_VERSION-$ARCHIVE"
  rm -rf "$OUT"; mkdir -p "$OUT"
  security unlock-keychain -p "$(cat "$GEVR_KEYCHAIN_PASS")" "$GEVR_KEYCHAIN"
  xcodebuild archive -project port/visionos/GoldenEyeVR.xcodeproj -scheme GoldenEyeVR -configuration Release \
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
  IPA="$OUT/export/GoldenEyeVR.ipa"
  [ -f "$IPA" ] || { echo "!! export failed"; exit 1; }
  echo "ipa: $IPA"
  if [ "$UPLOAD" = --upload ]; then
    : "${GEVR_ASC_KEY_DIR:?}" "${GEVR_ASC_KEY_ID:?}" "${GEVR_ASC_ISSUER:?}"
    for step in --validate-app --upload-app; do
      API_PRIVATE_KEYS_DIR="$GEVR_ASC_KEY_DIR" xcrun altool $step -f "$IPA" -t visionos \
        --apiKey "$GEVR_ASC_KEY_ID" --apiIssuer "$(cat "$GEVR_ASC_ISSUER")" 2>&1 \
        | grep -E 'description|No errors|SUCCEEDED|Failed|ERROR' | sort -u
    done
    rm -rf "$OUT/export"   # the archive stays: its dSYMs symbolicate the build's crashes
  fi
fi
