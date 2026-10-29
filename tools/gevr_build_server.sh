#!/bin/sh
# Build the dedicated server (docs/dedicated-server.md) on Linux or WSL.
#
#   tools/gevr_build_server.sh [--aarch64] [build-dir]
#
# Needs cmake, ninja, binutils, libc and zlib headers. The game needs clang
# (CPPLib.h relies on its token pasting), so if none is installed this unpacks
# Ubuntu's clang 18 into ~/clang18 with apt-get download: no root needed.
#
# On an ARM64 machine (an Oracle A1, a Raspberry Pi) the plain build makes an
# ARM64 server. --aarch64 makes one on x86-64 Ubuntu 24.04 instead (how the
# release's is built): Ubuntu's arm64 cross libraries and lld are unpacked into
# ~/gevr-arm64 the same way, and zlib is built from its source. Its
# usr/bin/qemu-aarch64-static runs the result for a test:
#   ~/gevr-arm64/root/usr/bin/qemu-aarch64-static -L ~/gevr-arm64/root/usr/aarch64-linux-gnu gevr-server ...
set -e

SRC=$(cd "$(dirname "$0")/.." && pwd)
ARCH=native
if [ "$1" = "--aarch64" ]; then
	ARCH=aarch64
	shift
fi
if [ $ARCH = aarch64 ]; then
	BUILD=${1:-$HOME/gevr-server-build-aarch64}
else
	BUILD=${1:-$HOME/gevr-server-build}
fi

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

CROSS=
if [ $ARCH = aarch64 ]; then
	X=$HOME/gevr-arm64/root
	if [ ! -d "$X/usr/aarch64-linux-gnu/lib" ] || [ ! -x "$X/usr/lib/llvm-18/bin/ld.lld" ]; then
		mkdir -p "$HOME/gevr-arm64/debs"
		(cd "$HOME/gevr-arm64/debs" && apt-get download libc6-dev-arm64-cross libc6-arm64-cross \
			linux-libc-dev-arm64-cross libgcc-13-dev-arm64-cross libgcc-s1-arm64-cross \
			libstdc++-13-dev-arm64-cross libstdc++6-arm64-cross lld-18 qemu-user-static)
		for d in "$HOME"/gevr-arm64/debs/*.deb; do dpkg -x "$d" "$X"; done
	fi
	# clang for the target, its flags in two wrappers as CMake runs them (lld
	# uses clang 18's libLLVM, which the clang wrappers above put on the path)
	FLAGS="--target=aarch64-linux-gnu --sysroot=$X --gcc-install-dir=$X/usr/lib/gcc-cross/aarch64-linux-gnu/13 --ld-path=$X/usr/lib/llvm-18/bin/ld.lld"
	printf '#!/bin/sh\nexec %s %s "$@"\n' "$CC" "$FLAGS" > "$HOME/gevr-arm64/clang"
	printf '#!/bin/sh\nexec %s %s "$@"\n' "$CXX" "$FLAGS" > "$HOME/gevr-arm64/clang++"
	chmod +x "$HOME/gevr-arm64/clang" "$HOME/gevr-arm64/clang++"
	CC=$HOME/gevr-arm64/clang
	CXX=$HOME/gevr-arm64/clang++
	# the target's libraries only, never the build machine's (its zlib)
	CROSS="-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_FIND_ROOT_PATH=$X/usr/aarch64-linux-gnu
		-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
		-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY"
fi

mkdir -p "$BUILD"
cd "$BUILD"
if [ ! -f CMakeCache.txt ]; then
	# shellcheck disable=SC2086
	CC=$CC CXX=$CXX cmake -G Ninja -DGEVR_HEADLESS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo $CROSS "$SRC"
fi
ninja gevr-server
echo "built $BUILD/gevr-server"
