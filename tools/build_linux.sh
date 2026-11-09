#!/bin/sh
# Build the PC game on Linux or WSL (docs/pc-platforms.md): the flat game
# (GoldenEyeVR) and PC VR (GoldenEyeVR-VR, OpenXR over GLX), from the same
# sources as the Windows build (tools/build_pc.py).
#
#   tools/build_linux.sh [build-dir]
#
# No root needed: the headers and link libraries the build wants (libcurl,
# GL/GLX, X11 and what SDL's X11 and PulseAudio drivers use, the OpenXR
# loader) are fetched with apt-get download and unpacked into
# ~/gevr-linux-deps, and clang comes from ~/clang18 as for the dedicated
# server (tools/gevr_build_server.sh). The libraries themselves are the
# system's at run time; the OpenXR loader and Steam Audio's libphonon.so are
# copied beside the binaries.
set -e

SRC=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-$HOME/gevr-pc-linux}
DEPS=$HOME/gevr-linux-deps
D=$DEPS/root
LIB=$D/usr/lib/x86_64-linux-gnu

# ---- clang, as the server's ----
if command -v clang >/dev/null 2>&1 && command -v clang++ >/dev/null 2>&1; then
	CC=clang
	CXX=clang++
else
	R=$HOME/clang18/root
	if [ ! -x "$R/usr/lib/llvm-18/bin/clang" ]; then
		mkdir -p "$HOME/clang18/debs"
		(cd "$HOME/clang18/debs" && apt-get download clang-18 libclang-cpp18 libllvm18 \
			libclang-common-18-dev libclang1-18 llvm-18-linker-tools libobjc-13-dev)
		for d in "$HOME"/clang18/debs/*.deb; do dpkg -x "$d" "$R"; done
	fi
	for t in clang clang++; do
		printf '#!/bin/sh\nexport LD_LIBRARY_PATH=%s/usr/lib/x86_64-linux-gnu:%s/usr/lib/llvm-18/lib\nexec %s/usr/lib/llvm-18/bin/%s "$@"\n' \
			"$R" "$R" "$R" "$t" > "$HOME/clang18/$t"
		chmod +x "$HOME/clang18/$t"
	done
	CC=$HOME/clang18/clang
	CXX=$HOME/clang18/clang++
fi

# ---- headers and link libraries (no root) ----
PKGS="libcurl4-openssl-dev libgl-dev libglx-dev libegl-dev libgles-dev libopengl-dev
	libx11-dev x11proto-dev libxau-dev libxdmcp-dev libxcb1-dev libxext-dev libxrandr-dev
	libxrender-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxinerama-dev
	libxkbcommon-dev libpulse-dev libasound2-dev libopenxr-dev libopenxr-loader1
	pkgconf-bin libpkgconf3"
# WSL puts Windows' folders on PATH: a pkg-config there (Strawberry Perl's) is
# not this system's, and SDL finds PulseAudio and PipeWire through pkg-config
PATH=$(printf '%s' "$PATH" | tr ':' '\n' | grep -v '^/mnt/' | paste -sd: -)
export PATH
if [ ! -f "$DEPS/done-2" ]; then
	mkdir -p "$DEPS/debs" "$D"
	# shellcheck disable=SC2086
	(cd "$DEPS/debs" && apt-get download $PKGS)
	for d in "$DEPS"/debs/*.deb; do dpkg -x "$d" "$D"; done
	# a dev package's libX.so points at the runtime's libX.so.N, which is the
	# system's (installed with the desktop): point the dangling ones there
	for l in "$LIB"/*.so; do
		[ -L "$l" ] || continue
		[ -e "$l" ] && continue
		t=$(readlink "$l")
		if [ -e "/usr/lib/x86_64-linux-gnu/$t" ]; then
			ln -sf "/usr/lib/x86_64-linux-gnu/$t" "$l"
		fi
	done
	touch "$DEPS/done-2"
fi
# pkg-config over the unpacked .pc files (their /usr under $D)
cat > "$DEPS/pkg-config" <<EOF
#!/bin/sh
export LD_LIBRARY_PATH=$LIB
export PKG_CONFIG_PATH=$LIB/pkgconfig:$D/usr/share/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=$D
exec $D/usr/bin/pkgconf "\$@"
EOF
chmod +x "$DEPS/pkg-config"
export PKG_CONFIG="$DEPS/pkg-config"

# ---- Steam Audio's Linux libphonon.so (the Quest's 4.8.1, as build_pc.py's) ----
SA=$DEPS/steamaudio-4.8.1
SA_SHA=$(sed -n "s/^STEAM_AUDIO_SHA256 = '\(.*\)'/\1/p" "$SRC/tools/build_pc.py")
if [ ! -f "$SA/libphonon.so" ]; then
	mkdir -p "$SA"
	curl -fsSL -o "$DEPS/steamaudio_4.8.1.zip" \
		https://github.com/ValveSoftware/steam-audio/releases/download/v4.8.1/steamaudio_4.8.1.zip
	echo "$SA_SHA  $DEPS/steamaudio_4.8.1.zip" | sha256sum -c -
	python3 - "$DEPS/steamaudio_4.8.1.zip" "$SA" <<'EOF'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
for n in z.namelist():
    if n.endswith('/lib/linux-x64/libphonon.so'):
        open(sys.argv[2] + '/libphonon.so', 'wb').write(z.read(n))
        break
else:
    sys.exit('no lib/linux-x64/libphonon.so in the Steam Audio zip')
EOF
fi

INC="-I$D/usr/include -I$D/usr/include/x86_64-linux-gnu"
mkdir -p "$BUILD"
cmake -S "$SRC" -B "$BUILD" -G Ninja \
	-DGEVR_PLATFORM=pc -DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
	-DCMAKE_C_FLAGS="$INC" -DCMAKE_CXX_FLAGS="$INC" \
	-DCMAKE_EXE_LINKER_FLAGS="-L$LIB -Wl,-rpath-link,$LIB" \
	-DCMAKE_PREFIX_PATH="$D/usr" \
	-DPKG_CONFIG_EXECUTABLE="$DEPS/pkg-config" \
	-DGEVR_STEAMAUDIO_LINUX="$SA" \
	-DGEVR_OPENXR_LINUX="$D/usr"
cmake --build "$BUILD" --target GoldenEyeVR GoldenEyeVR-VR
# the OpenXR loader beside the VR binary (rpath $ORIGIN): not every distro ships one
for f in "$LIB"/libopenxr_loader.so.1*; do
	[ -e "$f" ] && cp -L "$f" "$BUILD/" 2>/dev/null || true
done
echo "$BUILD/GoldenEyeVR"
echo "$BUILD/GoldenEyeVR-VR"
