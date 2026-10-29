#!/bin/sh
# The dedicated server's release downloads (docs/dedicated-server-operators.md):
# one tarball per architecture, built on x86-64 Ubuntu 24.04 or WSL.
#
#   tools/gevr_package_server.sh VERSION [OUTDIR]
#
# Builds the server for this machine and, on x86-64, for ARM64 too
# (tools/gevr_build_server.sh --aarch64), then packs each as
# gevr-server-VERSION-linux-ARCH.tar.gz: the server without its debug info,
# the lobby bridge, the systemd unit, the operator guide and the licence. The
# debug info stays beside it as gevr-server-VERSION-linux-ARCH.debug, for
# reading a crash's addresses (keep it with the release's symbols; it is not
# published). No game data goes in: the operator supplies ge.z64.
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
		BUILD=$HOME/gevr-server-build
		sh "$SRC/tools/gevr_build_server.sh" "$BUILD"
	else
		BUILD=$HOME/gevr-server-build-aarch64
		sh "$SRC/tools/gevr_build_server.sh" --aarch64 "$BUILD"
	fi
	NAME=gevr-server-$VERSION-linux-$ARCH
	D=$OUT/$NAME
	rm -rf "$D"
	mkdir -p "$D"
	# objcopy's generic ELF handling strips an ARM64 binary on x86-64 too
	objcopy --only-keep-debug "$BUILD/gevr-server" "$OUT/$NAME.debug"
	objcopy --strip-debug --add-gnu-debuglink="$OUT/$NAME.debug" "$BUILD/gevr-server" "$D/gevr-server"
	chmod 755 "$D/gevr-server"
	cp "$SRC/tools/gevr_lobby_bridge.py" "$SRC/port/server/package/gevr-server.service" "$D/"
	chmod 755 "$D/gevr_lobby_bridge.py"
	cp "$SRC/docs/dedicated-server-operators.md" "$D/README.md"
	cp "$SRC/LICENSE" "$D/LICENSE"
	printf '%s %s (%s)\n' "$VERSION" "$GEVR_BUILD_HASH" "$ARCH" > "$D/VERSION"
	tar -C "$OUT" -czf "$OUT/$NAME.tar.gz" "$NAME"
	rm -rf "$D"
done
(cd "$OUT" && sha256sum gevr-server-"$VERSION"-linux-*.tar.gz)
