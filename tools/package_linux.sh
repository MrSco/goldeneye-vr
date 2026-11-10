#!/bin/sh
# The Linux game's release download (docs/pc-platforms.md), as
# tools/package_pc.py makes the Windows one.
#
#   tools/package_linux.sh [VERSION] [--out DIR] [--no-build]
#
# Run on Linux or in WSL. VERSION defaults to android/app/build.gradle's
# versionName (a leading v is dropped: the tag is vX.Y.Z, the zip says
# X.Y.Z). Builds with tools/build_linux.sh and packs
# GoldenEyeVR-VERSION-linux-x64.zip:
#
# - GoldenEyeVR (flat) and GoldenEyeVR-VR (PC VR, OpenXR over GLX), each
#   without its debug info, found beside them through rpath $ORIGIN with
#   Steam Audio's libphonon.so and the OpenXR loader (libopenxr_loader.so.1);
# - play-vr.sh (the active OpenXR runtime) and play-steamvr.sh (SteamVR for
#   that launch, through XR_RUNTIME_JSON), the Windows package's Play *.cmd;
# - README.txt (port/pc/package/README-linux.txt), the icon and logo the
#   window and the launcher read, the licences and a VERSION file.
#
# The zip keeps the Unix modes (the binaries and scripts run after a plain
# unzip); the in-game updater (port/pc/pc_updater.cpp) looks for exactly this
# name, unpacks it over the folder and sets the modes again. The debug info
# stays beside the zip as GoldenEyeVR[-VR]-VERSION-linux-x64.debug, for the
# reports repo's symbols release. No game data goes in.
set -e

SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=$SRC/build/linux-dist
BUILD=$HOME/gevr-pc-linux
VERSION=
NOBUILD=0
while [ $# -gt 0 ]; do
	case "$1" in
		--out) OUT=$2; shift 2 ;;
		--no-build) NOBUILD=1; shift ;;
		--build-dir) BUILD=$2; shift 2 ;;
		*) VERSION=$1; shift ;;
	esac
done
if [ -z "$VERSION" ]; then
	VERSION=$(sed -n 's/.*versionName "\([^"]*\)".*/\1/p' "$SRC/android/app/build.gradle")
fi
VERSION=${VERSION#v}
VERSION=${VERSION#V}
[ -n "$VERSION" ] || { echo "no version" >&2; exit 1; }

if [ "$NOBUILD" = 0 ]; then
	sh "$SRC/tools/build_linux.sh" "$BUILD"
fi
for f in GoldenEyeVR GoldenEyeVR-VR libphonon.so; do
	[ -f "$BUILD/$f" ] || { echo "$BUILD has no $f (tools/build_linux.sh builds both binaries and copies the libraries beside them)" >&2; exit 1; }
done

OBJCOPY=$(command -v llvm-objcopy || command -v objcopy || true)
if [ -z "$OBJCOPY" ]; then
	# none installed: LLVM's, beside the clang tools/build_linux.sh fetches (no root)
	R=$HOME/clang18/root
	if [ ! -x "$R/usr/lib/llvm-18/bin/llvm-objcopy" ]; then
		mkdir -p "$HOME/clang18/debs"
		(cd "$HOME/clang18/debs" && apt-get download llvm-18 libllvm18)
		for d in "$HOME"/clang18/debs/llvm-18_*.deb "$HOME"/clang18/debs/libllvm18_*.deb; do dpkg -x "$d" "$R"; done
	fi
	printf '#!/bin/sh\nexport LD_LIBRARY_PATH=%s/usr/lib/x86_64-linux-gnu:%s/usr/lib/llvm-18/lib\nexec %s/usr/lib/llvm-18/bin/llvm-objcopy "$@"\n' \
		"$R" "$R" "$R" > "$HOME/clang18/llvm-objcopy"
	chmod +x "$HOME/clang18/llvm-objcopy"
	OBJCOPY=$HOME/clang18/llvm-objcopy
fi
NAME=GoldenEyeVR-$VERSION-linux-x64
STAGE=$OUT/$NAME
rm -rf "$STAGE"
mkdir -p "$STAGE/licenses"

for b in GoldenEyeVR GoldenEyeVR-VR; do
	DEBUG=$OUT/$b-$VERSION-linux-x64.debug
	"$OBJCOPY" --only-keep-debug "$BUILD/$b" "$DEBUG"
	"$OBJCOPY" --strip-debug --add-gnu-debuglink="$DEBUG" "$BUILD/$b" "$STAGE/$b"
	chmod 755 "$STAGE/$b"
done
cp "$BUILD/libphonon.so" "$STAGE/"
# the loader under the name the binary asks for (its soname), once
[ -e "$BUILD/libopenxr_loader.so.1" ] && cp -L "$BUILD/libopenxr_loader.so.1" "$STAGE/"
chmod 755 "$STAGE"/*.so*

cat > "$STAGE/play-vr.sh" <<'EOF'
#!/bin/sh
# PC VR through the active OpenXR runtime (Monado, WiVRn, ALVR, SteamVR...)
cd "$(dirname "$0")" && exec ./GoldenEyeVR-VR "$@"
EOF
cat > "$STAGE/play-steamvr.sh" <<'EOF'
#!/bin/sh
# PC VR through SteamVR for this launch only, whatever runtime is active
cd "$(dirname "$0")" || exit 1
for lib in "$HOME/.steam/steam" "$HOME/.local/share/Steam" "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; do
	json="$lib/steamapps/common/SteamVR/steamxr_linux64.json"
	if [ -f "$json" ]; then
		XR_RUNTIME_JSON=$json exec ./GoldenEyeVR-VR "$@"
	fi
done
echo "SteamVR was not found in the usual Steam libraries. Start your VR software and use play-vr.sh," >&2
echo "or set XR_RUNTIME_JSON to SteamVR's steamxr_linux64.json." >&2
exit 1
EOF
chmod 755 "$STAGE/play-vr.sh" "$STAGE/play-steamvr.sh"

cp "$SRC/port/pc/package/README-linux.txt" "$STAGE/README.txt"
cp "$SRC/port/pc/package/gevr_icon.rgba" "$SRC/port/pc/package/gevr_logo.rgba" "$STAGE/"
cp "$SRC/LICENSE" "$STAGE/LICENSE.txt"
cp "$SRC/CREDITS.md" "$STAGE/"
cp "$SRC"/port/licenses/* "$STAGE/licenses/"
cp "$SRC/port/external/steamaudio/LICENSE.md" "$STAGE/licenses/steam-audio.md"
printf '%s\n' "$VERSION" > "$STAGE/VERSION"

# zip with the Unix modes in each entry (python3's zipfile, which every distro
# has), set here rather than read: a folder on a Windows drive (WSL's /mnt/c)
# reports every file as 0777
python3 - "$OUT" "$NAME" <<'EOF'
import os, sys, zipfile, hashlib
out, name = sys.argv[1], sys.argv[2]
stage = os.path.join(out, name)
zpath = os.path.join(out, name + '.zip')
with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for base, dirs, files in os.walk(stage):
        dirs.sort()
        for fn in sorted(files):
            full = os.path.join(base, fn)
            info = zipfile.ZipInfo.from_file(full, os.path.join(name, os.path.relpath(full, stage)))
            info.compress_type = zipfile.ZIP_DEFLATED
            runs = fn in ('GoldenEyeVR', 'GoldenEyeVR-VR') or fn.endswith('.sh') or '.so' in fn
            info.external_attr = ((0o100755 if runs else 0o100644) & 0xFFFF) << 16
            with open(full, 'rb') as f:
                z.writestr(info, f.read(), compresslevel=9)
h = hashlib.sha256(open(zpath, 'rb').read()).hexdigest()
print(f'{zpath}\n  {os.path.getsize(zpath) // 1024} KB, SHA-256 {h}')
EOF
for b in GoldenEyeVR GoldenEyeVR-VR; do
	echo "$OUT/$b-$VERSION-linux-x64.debug (to the reports repo's symbols release)"
done
