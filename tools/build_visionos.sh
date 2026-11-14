#!/usr/bin/env bash
# GoldenEye VR for Apple Vision Pro (docs/mobile-platforms.md, "visionOS
# build"): the Quest APK run by Klepton (MrSco/Klepton, branch goldeneye-vr),
# archived as a visionOS app and, with --upload, sent to TestFlight.
#
#     tools/build_visionos.sh <quest-release.apk> <version> <build> [--upload]
#
# Run on the Mac. Needs a Klepton checkout with its ANGLE frameworks built
# ($KLEPTON_DIR, default ~/src/klepton), apktool, and the signing setup in
# the environment (nothing about the account lives in this repository):
#   GEVR_TEAM            the Apple team id
#   GEVR_PROFILE         the App Store provisioning profile's name
#   GEVR_BUNDLE_ID       the App Store Connect app's bundle id
#   GEVR_KEYCHAIN        a keychain holding the Apple Distribution identity
#   GEVR_KEYCHAIN_PASS   a file with that keychain's password (it is unlocked here)
#   GEVR_ASC_KEY_DIR     the folder holding AuthKey_<id>.p8 (App Manager role)
#   GEVR_ASC_KEY_ID      that key's id
#   GEVR_ASC_ISSUER      a file with the key's issuer id
set -euo pipefail
[ $# -ge 3 ] || { sed -n '2,20p' "$0"; exit 1; }
APK="$1"; VERSION="$2"; BUILD="$3"; UPLOAD="${4:-}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KLEPTON="${KLEPTON_DIR:-$HOME/src/klepton}"
: "${GEVR_TEAM:?}" "${GEVR_PROFILE:?}" "${GEVR_BUNDLE_ID:?}" "${GEVR_KEYCHAIN:?}" "${GEVR_KEYCHAIN_PASS:?}"
export PATH="/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin"   # not GNU coreutils: run.sh's stat is BSD's

# The guest: the APK's libraries and assets where Klepton's gevr target reads them
rm -rf "$KLEPTON/gevr"
cmp -s "$APK" "$KLEPTON/gevr.apk" || cp "$APK" "$KLEPTON/gevr.apk"
apktool d -f -o "$KLEPTON/gevr" "$KLEPTON/gevr.apk" >/dev/null
# The layered app icon (from docs/art/app_icon.jpg), which Klepton's catalog leaves to each target
rm -rf "$KLEPTON/visionos/Assets.xcassets/AppIcon-gevr.solidimagestack"
cp -R "$ROOT/port/visionos/AppIcon-gevr.solidimagestack" "$KLEPTON/visionos/Assets.xcassets/"

security unlock-keychain -p "$(cat "$GEVR_KEYCHAIN_PASS")" "$GEVR_KEYCHAIN"
export KLEPTON_TARGET=gevr KLEPTON_TEAM="$GEVR_TEAM" KLEPTON_BUNDLE_ID="$GEVR_BUNDLE_ID"
export KLEPTON_LOW_LATENCY=0 XROS_PLATFORMS=xros   # no Steam Link entitlement; the device slice only
cd "$KLEPTON"
make -s build/klepton-ld xros XROS_PLATFORMS=xros
cd visionos
./mkguest.sh | tail -1; ./mkangle.sh | tail -1; ./mkmvk.sh | tail -1
python3 gen_xcodeproj.py | head -1
OUT="build/testflight-$VERSION-$BUILD"
rm -rf "$OUT"; mkdir -p "$OUT"
xcodebuild archive -project KleptonGoldenEyeVR.xcodeproj -scheme KleptonGoldenEyeVR \
  -configuration Release -destination generic/platform=visionOS \
  -archivePath "$OUT/GoldenEyeVR.xcarchive" -derivedDataPath build/dd-testflight-gevr \
  CODE_SIGN_STYLE=Manual DEVELOPMENT_TEAM="$GEVR_TEAM" CODE_SIGN_IDENTITY="Apple Distribution" \
  PROVISIONING_PROFILE_SPECIFIER="$GEVR_PROFILE" \
  MARKETING_VERSION="$VERSION" CURRENT_PROJECT_VERSION="$BUILD" \
  INFOPLIST_KEY_ITSAppUsesNonExemptEncryption=NO \
  OTHER_CODE_SIGN_FLAGS="--keychain $GEVR_KEYCHAIN" 2>&1 | grep -E "error:|\*\* ARCHIVE" || true
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
</dict></plist>
PL
xcodebuild -exportArchive -archivePath "$OUT/GoldenEyeVR.xcarchive" -exportPath "$OUT/export" \
  -exportOptionsPlist "$OUT/ExportOptions.plist" 2>&1 | grep -E "error|EXPORT" || true
IPA="$OUT/export/KleptonGoldenEyeVR.ipa"
[ -f "$IPA" ] || { echo "!! export failed"; exit 1; }
echo "ipa: $KLEPTON/visionos/$IPA"

if [ "$UPLOAD" = --upload ]; then
  : "${GEVR_ASC_KEY_DIR:?}" "${GEVR_ASC_KEY_ID:?}" "${GEVR_ASC_ISSUER:?}"
  for step in --validate-app --upload-app; do
    API_PRIVATE_KEYS_DIR="$GEVR_ASC_KEY_DIR" xcrun altool $step -f "$IPA" -t visionos \
      --apiKey "$GEVR_ASC_KEY_ID" --apiIssuer "$(cat "$GEVR_ASC_ISSUER")" 2>&1 \
      | grep -E "description|No errors|SUCCEEDED|Failed" | sort -u
  done
  rm -rf "$OUT/export"   # the archive stays: its dSYMs symbolicate the build's crashes
fi
