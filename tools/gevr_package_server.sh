#!/bin/sh
# The dedicated server's release downloads (docs/dedicated-server-operators.md):
# one tarball per architecture, built on x86-64 Ubuntu 24.04 or WSL.
#
#   tools/gevr_package_server.sh VERSION [OUTDIR]
#
# Builds the server for this machine and, on x86-64, for ARM64 too
# (tools/gevr_build_server.sh --aarch64), then packs each as
# gevr-server-VERSION-linux-ARCH.tar.gz: the server without its debug info,
# the lobby bridge, the setup script, the systemd unit, the operator guide
# and the licence. The
# debug info stays beside it as gevr-server-VERSION-linux-ARCH.debug, for
# reading a crash's addresses (keep it with the release's symbols; it is not
# published). No game data goes in: the operator supplies ge.z64.
#
# GEVR_SERVER_BUILD and GEVR_SERVER_BUILD_ARM name other build folders (a
# folder's CMake cache keeps the checkout it was configured for).
#
# The build is labelled with this checkout's commit; where git cannot read it
# (a Windows worktree from WSL), set GEVR_BUILD_HASH (and GEVR_BUILD_BRANCH).
set -e

SRC=$(cd "$(dirname "$0")/.." && pwd)
VERSION=${1:?usage: gevr_package_server.sh VERSION [OUTDIR]}
OUT=${2:-$SRC/build/server-dist}
if [ -z "$GEVR_BUILD_HASH" ]; then
	GEVR_BUILD_HASH=$(git -C "$SRC" rev-parse --short HEAD 2>/dev/null || echo nogit)
	GEVR_BUILD_BRANCH=$(git -C "$SRC" rev-parse --abbrev-ref HEAD 2>/dev/null || echo main)
fi
export GEVR_BUILD_HASH GEVR_BUILD_BRANCH

NATIVE=$(uname -m)
ARCHES=$NATIVE
[ "$NATIVE" = x86_64 ] && ARCHES="x86_64 aarch64"
mkdir -p "$OUT"

for ARCH in $ARCHES; do
	if [ "$ARCH" = "$NATIVE" ]; then
		BUILD=${GEVR_SERVER_BUILD:-$HOME/gevr-server-build}
		sh "$SRC/tools/gevr_build_server.sh" "$BUILD"
		OBJCOPY=objcopy
	else
		BUILD=${GEVR_SERVER_BUILD_ARM:-$HOME/gevr-server-build-aarch64}
		sh "$SRC/tools/gevr_build_server.sh" --aarch64 "$BUILD"
		# the build machine's binutils don't read ARM64; the cross ones do
		X=$HOME/gevr-arm64/root
		OBJCOPY="env LD_LIBRARY_PATH=$X/usr/lib/x86_64-linux-gnu $X/usr/bin/aarch64-linux-gnu-objcopy"
	fi
	NAME=gevr-server-$VERSION-linux-$ARCH
	D=$OUT/$NAME
	rm -rf "$D"
	mkdir -p "$D"
	$OBJCOPY --only-keep-debug "$BUILD/gevr-server" "$OUT/$NAME.debug"
	$OBJCOPY --strip-debug --add-gnu-debuglink="$OUT/$NAME.debug" "$BUILD/gevr-server" "$D/gevr-server"
	chmod 755 "$D/gevr-server"
	cp "$SRC/tools/gevr_lobby_bridge.py" "$SRC/tools/gevr-server-setup.sh" "$SRC/port/server/package/gevr-server.service" "$D/"
	chmod 755 "$D/gevr_lobby_bridge.py" "$D/gevr-server-setup.sh"
	cp "$SRC/docs/dedicated-server-operators.md" "$D/README.md"
	cp "$SRC/LICENSE" "$D/LICENSE"
	printf '%s %s (%s)\n' "$VERSION" "$GEVR_BUILD_HASH" "$ARCH" > "$D/VERSION"
	chmod 644 "$D/README.md" "$D/LICENSE" "$D/gevr-server.service" "$D/VERSION"   # copies from a Windows drive come out executable
	tar -C "$OUT" -czf "$OUT/$NAME.tar.gz" "$NAME"
	rm -rf "$D"
done
(cd "$OUT" && sha256sum gevr-server-"$VERSION"-linux-*.tar.gz)
