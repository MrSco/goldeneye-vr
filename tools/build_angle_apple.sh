#!/usr/bin/env bash
# Builds ANGLE (OpenGL ES on Metal) for the Apple builds: iOS device, iOS
# simulator, Apple TV device and simulator, and macOS arm64, release, Metal
# backend only.
#
#     tools/build_angle_apple.sh [ios-device|ios-simulator|tvos-device|tvos-simulator|mac ...]
#
# With no slices named it builds the iOS pair; the Apple TV pair is asked for
# by name (tools/build_tvos.sh) and lands in $ANGLE_DIR/xcframeworks-tvos.
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

# Older Apple GPU compatibility patches for ANGLE's Metal backend: Apple TV 4K
# Gen 1 (A10X Fusion, MTLGPUFamilyApple3) and Apple TV HD (A8, Apple2)
# (idempotent; applied before gn gen / autoninja):
#  1. ES 3.0 from Apple2 up: ANGLE 09a869c245 gates it on Apple4, for the
#     twelve uniform blocks ES 3 promises; the game binds one. The Apple TV HD
#     failed at startup without it ("no ES 3 config", 2026-11-20). What Apple2
#     lacks besides (base vertex draws) ANGLE already works around.
#  2. Cap maxVertexOutputComponents/maxFragmentInputComponents at 60 on < Apple4
#     per Apple's Metal Feature Set Tables (keeping 16384 max2DTextureSize on Apple3+).
#  3. Guard hasShaderStencilOutput ([[stencil]]) on Apple5+ / Mac2+.
python3 - "$SRC/src/libANGLE/renderer/metal/DisplayMtl.mm" <<'PY'
import pathlib, sys

path = pathlib.Path(sys.argv[1])
src = path.read_text(encoding="utf-8")

patches = [
    (
        "getMaxSupportedESVersion Apple2 gate",
        ("""#else
    if (supportsEitherGPUFamily(4, 1))
    {
        return mtl::kMaxSupportedGLVersion;
    }
    return gl::Version(2, 0);
#endif""",
         # this script's earlier Apple3 gate, on a checkout it already patched
         """#else
    // GoldenEye VR: revert ANGLE 09a869c245 (Apple4-only ES3 gate) back to
    // Apple3 so Apple TV 4K Gen 1 (A10X Fusion, MTLGPUFamilyApple3) gets
    // OpenGL ES 3.0. GoldenEye VR's flat/tvOS shaders use at most 1 UBO
    // (well within Apple3's 31 buffer-entry/stage limit).
    if (supportsEitherGPUFamily(3, 1))
    {
        return mtl::kMaxSupportedGLVersion;
    }
    return gl::Version(2, 0);
#endif"""),
        """#else
    // GoldenEye VR: ES 3.0 from Apple2 up (Apple TV HD's A8; Apple TV 4K Gen
    // 1's A10X is Apple3), where ANGLE 09a869c245 asks Apple4 for the twelve
    // uniform blocks ES 3 promises. GoldenEye VR binds at most 1 UBO (well
    // within the 31 buffer entries a stage these families have).
    if (supportsEitherGPUFamily(2, 1))
    {
        return mtl::kMaxSupportedGLVersion;
    }
    return gl::Version(2, 0);
#endif""",
    ),
    (
        "maxFragmentInputComponents Apple3 cap",
        """    if (supportsAppleGPUFamily(3))
    {
        mNativeCaps.max2DTextureSize          = 16384;
        mNativeCaps.maxVertexOutputComponents = mNativeCaps.maxFragmentInputComponents = 124;
        mNativeCaps.maxVaryingVectors = mNativeCaps.maxVertexOutputComponents / 4;
    }
    else
    {
        mNativeCaps.max2DTextureSize          = 8192;
        mNativeCaps.maxVertexOutputComponents = mNativeCaps.maxFragmentInputComponents = 60;
        mNativeCaps.maxVaryingVectors = mNativeCaps.maxVertexOutputComponents / 4;
    }""",
        """    // GoldenEye VR: Metal Feature Set Tables cap Apple1-Apple3 at 60 fragment
    // input components (15 vec4s); 124 begins at Apple4. Keep 16384 max 2D
    // texture size on Apple3+.
    mNativeCaps.max2DTextureSize = supportsAppleGPUFamily(3) ? 16384 : 8192;
    mNativeCaps.maxVertexOutputComponents = mNativeCaps.maxFragmentInputComponents =
        supportsAppleGPUFamily(4) ? 124 : 60;
    mNativeCaps.maxVaryingVectors = mNativeCaps.maxVertexOutputComponents / 4;""",
    ),
    (
        "hasShaderStencilOutput Apple5 guard",
        """    ANGLE_FEATURE_CONDITION((&mFeatures), hasShaderStencilOutput, !isIntel() && !isNVIDIA());""",
        """    // GoldenEye VR: Metal [[stencil]] shader output requires Apple5+ (A12+) or Mac2+.
    ANGLE_FEATURE_CONDITION((&mFeatures), hasShaderStencilOutput,
                            !isIntel() && !isNVIDIA() && supportsEitherGPUFamily(5, 2));""",
    ),
]

changed = False
for label, olds, new in patches:
    if new in src:
        continue
    old = next((o for o in ((olds,) if isinstance(olds, str) else olds) if o in src), None)
    if old is None:
        raise SystemExit(f"build_angle_apple.sh: patch '{label}' did not match {path}")
    if src.count(old) != 1:
        raise SystemExit(f"build_angle_apple.sh: patch '{label}' matched {src.count(old)} times in {path}")
    src = src.replace(old, new, 1)
    changed = True

if changed:
    path.write_text(src, encoding="utf-8")
    print(f"patched {path} for Apple GPU Families 2 and 3 (A8, A10X)")
PY

# 4. Frame pacing on iOS and tvOS: eglSwapInterval 2 or more holds each frame
#    that many refreshes (presentDrawable:afterMinimumDuration:, Metal's frame
#    pacing). ANGLE ignored the interval there and offered only 1. The game
#    asks for 2, a steady 30 instead of an uneven 40, on an A8 with Bean on in
#    a level (port/ios/ios_angle.mm, 2026-11-20).
python3 - "$SRC/src/libANGLE/renderer/metal" <<'PY'
import pathlib, sys

base = pathlib.Path(sys.argv[1])
patches = {
    "DisplayMtl.mm": [(
        """#else
    config.minSwapInterval = 1;
    config.maxSwapInterval = 1;
#endif""",
        """#else
    // GoldenEye VR: 2 = every other refresh (WindowSurfaceMtl::setSwapInterval)
    config.minSwapInterval = 1;
    config.maxSwapInterval = 2;
#endif""")],
    "SurfaceMtl.h": [(
        "    angle::ObjCPtr<CAMetalLayer> mMetalLayer = nil;\n",
        "    angle::ObjCPtr<CAMetalLayer> mMetalLayer = nil;\n"
        "    // GoldenEye VR: a swap interval of 2 or more on iOS and tvOS (setSwapInterval)\n"
        "    double mPresentMinDuration = 0.0;\n")],
    "SurfaceMtl.mm": [(
        """void WindowSurfaceMtl::setSwapInterval(const egl::Display *display, EGLint interval)
{
#if TARGET_OS_OSX || TARGET_OS_MACCATALYST
    mMetalLayer.get().displaySyncEnabled = interval != 0;
#endif
}""",
        """void WindowSurfaceMtl::setSwapInterval(const egl::Display *display, EGLint interval)
{
#if TARGET_OS_OSX || TARGET_OS_MACCATALYST
    mMetalLayer.get().displaySyncEnabled = interval != 0;
#else
    // GoldenEye VR: each frame held `interval` refreshes of a 60 Hz screen,
    // 2 ms short so the refresh it lands on counts (a 50 Hz screen holds it
    // two of its own)
    mPresentMinDuration = interval >= 2 ? interval / 60.0 - 0.002 : 0.0;
#endif
}"""), (
        "        contextMtl->present(context, mCurrentDrawable);\n",
        "        contextMtl->present(context, mCurrentDrawable, mPresentMinDuration);\n")],
    "ContextMtl.h": [(
        "    void present(const gl::Context *context, id<CAMetalDrawable> presentationDrawable);\n",
        "    void present(const gl::Context *context,\n"
        "                 id<CAMetalDrawable> presentationDrawable,\n"
        "                 double minDuration = 0.0);   // GoldenEye VR: frame pacing\n")],
    "ContextMtl.mm": [(
        "void ContextMtl::present(const gl::Context *context, id<CAMetalDrawable> presentationDrawable)\n",
        "void ContextMtl::present(const gl::Context *context,\n"
        "                         id<CAMetalDrawable> presentationDrawable,\n"
        "                         double minDuration)\n"), (
        "    mCmdBuffer.present(presentationDrawable);\n",
        "    mCmdBuffer.present(presentationDrawable, minDuration);\n")],
    "mtl_command_buffer.h": [(
        "    void present(id<CAMetalDrawable> presentationDrawable);\n",
        "    void present(id<CAMetalDrawable> presentationDrawable, double minDuration = 0.0);   // GoldenEye VR\n")],
    "mtl_command_buffer.mm": [(
        """void CommandBuffer::present(id<CAMetalDrawable> presentationDrawable)
{
    [get() presentDrawable:presentationDrawable];
}""",
        """void CommandBuffer::present(id<CAMetalDrawable> presentationDrawable, double minDuration)
{
    // GoldenEye VR: frame pacing (WindowSurfaceMtl::setSwapInterval)
    if (minDuration > 0.0)
    {
        [get() presentDrawable:presentationDrawable afterMinimumDuration:minDuration];
        return;
    }
    [get() presentDrawable:presentationDrawable];
}""")],
}
for name, pairs in patches.items():
    path = base / name
    src = path.read_text(encoding="utf-8")
    changed = False
    for old, new in pairs:
        if new in src:
            continue
        if src.count(old) != 1:
            raise SystemExit(f"build_angle_apple.sh: frame pacing patch did not match {path} ({src.count(old)})")
        src = src.replace(old, new, 1)
        changed = True
    if changed:
        path.write_text(src, encoding="utf-8")
        print(f"patched {path} for frame pacing")
PY

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
    # Apple TV: this revision's gn builds tvOS itself (target_platform), and
    # asks for use_blink with it (build/config/apple/mobile_config.gni), which
    # only raises the default deployment target: 17.0 is set here instead.
    tvos-device)   TARGET='target_os = "ios"
target_platform = "tvos"
target_environment = "device"
use_blink = true
ios_deployment_target = "17.0"
ios_enable_code_signing = false' ;;
    tvos-simulator) TARGET='target_os = "ios"
target_platform = "tvos"
target_environment = "simulator"
use_blink = true
ios_deployment_target = "17.0"
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

# A device and simulator pair as xcframeworks, so Xcode picks device or
# simulator itself (port/ios/project.yml); a plain .framework path cannot.
xcframeworks() {   # <device slice> <simulator slice> <output folder>
  [ -d "$ANGLE_DIR/out/$1/libEGL.framework" ] && [ -d "$ANGLE_DIR/out/$2/libEGL.framework" ] || return 0
  mkdir -p "$ANGLE_DIR/$3"
  for lib in libEGL libGLESv2; do
    # App Store validation wants a version in every embedded framework, and
    # ANGLE's gn output has none.
    for slice in "$1" "$2"; do
      plist="$ANGLE_DIR/out/$slice/$lib.framework/Info.plist"
      /usr/libexec/PlistBuddy -c "Delete :CFBundleShortVersionString" "$plist" 2>/dev/null || true
      /usr/libexec/PlistBuddy -c "Add :CFBundleShortVersionString string 1.0" "$plist"
      /usr/libexec/PlistBuddy -c "Print :CFBundleVersion" "$plist" >/dev/null 2>&1 ||
        /usr/libexec/PlistBuddy -c "Add :CFBundleVersion string 1" "$plist"
    done
    rm -rf "$ANGLE_DIR/$3/$lib.xcframework"
    xcodebuild -create-xcframework -framework "$ANGLE_DIR/out/$1/$lib.framework" \
      -framework "$ANGLE_DIR/out/$2/$lib.framework" -output "$ANGLE_DIR/$3/$lib.xcframework"
  done
}
xcframeworks ios-device ios-simulator xcframeworks
xcframeworks tvos-device tvos-simulator xcframeworks-tvos   # Apple TV (port/tvos/project.yml)
# Vision Pro (docs/visionos-native.md): ANGLE's gn has no visionOS target, so
# the iOS slices are retargeted, as Klepton does (tools/angle_retarget.sh,
# MIT): the Mach-O's build version set to visionOS with vtool, the install name
# @rpath-relative, and the framework's Info.plist naming the visionOS platform
# (vtool leaves the bundle alone, and an iPhoneOS plist fails the install's
# bundle check before dyld runs). AMFI takes the result on the device.
retarget() {   # <ios slice> <visionos slice> <vtool platform> <plist platform>
  local src="$ANGLE_DIR/out/$1" dst="$ANGLE_DIR/out/$2"
  for lib in libEGL libGLESv2; do
    rm -rf "$dst/$lib.framework"
    mkdir -p "$dst/$lib.framework"
    xcrun vtool -arch arm64 -set-build-version "$3" 26.0 26.0 -replace \
      -output "$dst/$lib.framework/$lib" "$src/$lib.framework/$lib"
    install_name_tool -id "@rpath/$lib.framework/$lib" "$dst/$lib.framework/$lib"
    cat > "$dst/$lib.framework/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>$lib</string>
  <key>CFBundleIdentifier</key><string>com.google.angle.$lib</string>
  <key>CFBundleName</key><string>$lib</string>
  <key>CFBundlePackageType</key><string>FMWK</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>CFBundleSupportedPlatforms</key><array><string>$4</string></array>
  <key>MinimumOSVersion</key><string>26.0</string>
</dict></plist>
EOF
  done
}
if [ -f "$ANGLE_DIR/out/ios-device/libEGL.framework/libEGL" ] && [ -f "$ANGLE_DIR/out/ios-simulator/libEGL.framework/libEGL" ]; then
  retarget ios-device xros visionos XROS
  retarget ios-simulator xrsimulator visionossim XRSimulator
  mkdir -p "$ANGLE_DIR/xcframeworks-visionos"
  for lib in libEGL libGLESv2; do
    rm -rf "$ANGLE_DIR/xcframeworks-visionos/$lib.xcframework"
    xcodebuild -create-xcframework -framework "$ANGLE_DIR/out/xros/$lib.framework" \
      -framework "$ANGLE_DIR/out/xrsimulator/$lib.framework" \
      -output "$ANGLE_DIR/xcframeworks-visionos/$lib.xcframework"
  done
fi
echo "ANGLE ready in $ANGLE_DIR (set ANGLE_DIR for the iOS build if you moved it)"
