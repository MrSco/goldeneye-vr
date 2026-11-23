#!/bin/bash
# iCloud sync's local parts (port/tests/icloud_native.mm), on the Mac: built for
# the iOS simulator and run in a booted one (no iCloud account needed).
#   bash port/tests/test_icloud.sh [simulator-udid]   (default: the booted one)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(mktemp -d)"
SDK="$(xcrun --sdk iphonesimulator --show-sdk-path)"
xcrun --sdk iphonesimulator clang++ -std=c++17 -fobjc-arc -O1 -target arm64-apple-ios17.0-simulator \
  -isysroot "$SDK" -DGEVR_IOS=1 -D_LANGUAGE_C=1 -DPLATFORM_64BIT=1 -I"$ROOT/port/include" -I"$ROOT/include" \
  -framework Foundation -framework CloudKit -framework UIKit \
  "$ROOT/port/tests/icloud_native.mm" -o "$OUT/icloud_native"
xcrun simctl spawn "${1:-booted}" "$OUT/icloud_native"
rm -rf "$OUT"
