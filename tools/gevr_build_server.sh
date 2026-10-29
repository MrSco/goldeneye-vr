#!/bin/sh
# Build the dedicated server (docs/dedicated-server.md) on Linux or WSL.
#
#   tools/gevr_build_server.sh [build-dir]
#
# Needs cmake, ninja, binutils, libc and zlib headers. The game needs clang
# (CPPLib.h relies on its token pasting), so if none is installed this unpacks
# Ubuntu's clang 18 into ~/clang18 with apt-get download: no root needed.
set -e

SRC=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-$HOME/gevr-server-build}

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

mkdir -p "$BUILD"
cd "$BUILD"
if [ ! -f CMakeCache.txt ]; then
	CC=$CC CXX=$CXX cmake -G Ninja -DGEVR_HEADLESS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo "$SRC"
fi
ninja gevr-server
echo "built $BUILD/gevr-server"
