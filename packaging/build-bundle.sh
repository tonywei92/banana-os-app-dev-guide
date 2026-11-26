#!/bin/bash
# Builds the app for the RG DS / RG DS Plus (aarch64) in an arm64 Debian 12 container, as a folder for the card and a
# zip for a GitHub release (which the BananaOS Store installs from).
#
#   packaging/build-bundle.sh            the bundle and the zip
#   packaging/build-bundle.sh --quick    the bundle only (for tools/push-handheld.sh): no tests, no zip
#   packaging/build-bundle.sh --clean    everything from scratch (the image's cache stays: delete the image for that)
#
# Why Debian 12: its glibc (2.36) is older than BananaOS's, and a binary runs on a glibc at least as new as the one it
# was linked against, never older. Why Docker with --platform linux/arm64: a native arm64 build (emulated on an Intel
# host, native on Apple silicon) needs no cross toolchain or sysroot.
#
# Fast again and again:
#   - the build tools are baked into an image, built once and tagged by a hash of its Dockerfile below, so a new
#     package makes a new image and nothing else does;
#   - the build folder (.cache/build) is kept between runs: an incremental build recompiles what changed;
#   - ccache (.cache/ccache) makes a --clean, or a second checkout of the same sources, a lookup instead of a compile;
#   - the commit goes into one generated file (version.cpp, CMakeLists.txt): a new commit recompiles that file only.
#
# Out: dist/ports/MyApp/ (bin, lib, share, launch.sh, VERSION), dist/ports/My App.sh, dist/ports/media/box/My App.png;
# dist/MyApp-<version>-aarch64.zip with all three; dist/symbols/my-app-<version>, the unstripped binary for crash.log.
set -euo pipefail

APP_DIR=MyApp	# the folder (the Store's package.root)
APP_BIN=my-app	# the binary in bin/
APP_ID=myapp	# share/<id>, the state folder's name, the Store id
APP_TITLE="My App"	# the Ports shelf's entry and picture

HERE="$(cd "$(dirname "$0")/.." && pwd)"

read -r -d '' BUILDER <<'DOCKERFILE' || true
FROM debian:bookworm
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
	build-essential pkg-config cmake ninja-build ccache patchelf file zip python3 ca-certificates \
	libsdl2-dev libsdl2-ttf-dev >/dev/null \
	&& rm -rf /var/lib/apt/lists/*
DOCKERFILE

if [ "${1:-}" != "--inside" ]; then
	mkdir -p "$HERE/dist" "$HERE/.cache/build" "$HERE/.cache/ccache"
	TAG="app-builder:$(printf '%s' "$BUILDER" | shasum -a 256 | cut -c1-12)"
	if ! docker image inspect "$TAG" >/dev/null 2>&1; then
		echo "== the build tools image ($TAG), once"
		printf '%s\n' "$BUILDER" | docker build --platform linux/arm64 -q -t "$TAG" - >/dev/null
	fi
	# git runs here, on the host: the container has no git and the repo may be a worktree
	exec docker run --rm --platform linux/arm64 \
		-e APP_COMMIT="$(git -C "$HERE" describe --always --dirty --exclude='*' 2>/dev/null || echo unknown)" \
		-v "$HERE":/src -v "$HERE/.cache/build":/build -v "$HERE/.cache/ccache":/ccache \
		"$TAG" bash /src/packaging/build-bundle.sh --inside "$@"
fi
shift
QUICK=0; CLEAN=0
for a in "$@"; do case "$a" in --quick) QUICK=1 ;; --clean) CLEAN=1 ;; esac; done

# ---- inside the container -----------------------------------------------------------------------------------------
[ "$CLEAN" = 1 ] && rm -rf /build/* && echo "== clean build"
export CCACHE_DIR=/ccache CCACHE_MAXSIZE=2G CCACHE_BASEDIR=/src CCACHE_NOHASHDIR=1
# -g: dist/symbols keeps the unstripped copy for addr2line. The rpath: bin/../lib, so bundled libraries are found
# even when started without launch.sh's LD_LIBRARY_PATH
cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-g -DAPP_COMMIT="$APP_COMMIT" \
	-DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib' -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON >/dev/null
TARGET=; [ "$QUICK" = 1 ] && TARGET="--target $APP_BIN"
# a failed compile stops here, loudly: never a bundle with the last good binary in it
if ! cmake --build /build $TARGET > /build/build.log 2>&1; then
	grep -E "error|FAILED" /build/build.log | head -40
	echo "build failed: no bundle made" >&2
	exit 1
fi
grep -E "warning" /build/build.log || true
# the tests on arm64 too: glibc and the Mac differ (uninitialised memory, headers, the allocator)
[ "$QUICK" = 1 ] || /build/app-test

echo "== bundle"
VERSION=$(sed -n 's/^project([^ ]* VERSION \([0-9.]*\).*/\1/p' /src/CMakeLists.txt)
OUT=/src/dist/ports
B="$OUT/$APP_DIR"
rm -rf "$OUT" && mkdir -p "$B/bin" "$B/lib" "$B/share/$APP_ID"
cp /build/$APP_BIN "$B/bin/"
# BananaOS has no fonts at all: the app's own go with it, with their licences
cp -r /src/assets/fonts "$B/share/$APP_ID/"
cp /src/packaging/launch.sh "$B/launch.sh"
echo "$VERSION" > "$B/VERSION"	# the Store's package.version_file
# the Store copies every app's tile picture into one shared folder by file name alone: a plain icon.png would replace
# another app's. A name of its own
cp /src/packaging/icon.png "$B/share/$APP_ID/$APP_ID-tile.png"

# The libraries the binary needs that BananaOS doesn't have, found by walking NEEDED entries (not ldd's whole tree: a
# system library's own needs are the system's). The system's must NOT be bundled: its SDL2 carries BananaOS's patches
# (SDL_KMSDRM_FIRST), its Mesa drives the GPU and needs the system's libstdc++, and glibc must match the loader.
# With only SDL2 and SDL2_ttf this bundles nothing; add a library (libmpv, FFmpeg...) and it comes along by itself.
SYSTEM='^(linux-vdso|ld-linux|libc\.|libm\.|libdl\.|libpthread\.|librt\.|libresolv\.|libstdc\+\+|libgcc_s|libSDL2|libasound|libfreetype|libz\.|libpng|libdrm|libEGL|libGLES|libGL\.|libgbm|libGLdispatch|libGLX|libX|libxcb|libwayland|libbz2|libbrotli|libcrypto|libssl|libcurl)'
needed() {
	local f=$1
	readelf -d "$f" | awk -F'[][]' '/NEEDED/ {print $2}' | while read -r name; do
		echo "$name $(ldd "$f" | awk -v n="$name" '$1 == n {print $3}')"
	done
}
declare -A seen
queue=("$B/bin/$APP_BIN")
while [ ${#queue[@]} -gt 0 ]; do
	f=${queue[0]}; queue=("${queue[@]:1}")
	while read -r name path; do
		[[ "$name" =~ $SYSTEM ]] && continue
		[ -n "${seen[$name]:-}" ] && continue
		seen[$name]=1
		cp -L "$path" "$B/lib/$name"
		queue+=("$B/lib/$name")
	done < <(needed "$f")
done
for l in "$B"/lib/*.so*; do [ -e "$l" ] && patchelf --set-rpath '$ORIGIN' "$l"; done
mkdir -p /src/dist/symbols && cp "$B/bin/$APP_BIN" "/src/dist/symbols/$APP_BIN-$VERSION"
strip --strip-unneeded "$B/bin/$APP_BIN" "$B"/lib/*.so* 2>/dev/null || true

cp /src/packaging/port.sh "$OUT/$APP_TITLE.sh"
chmod +x "$OUT/$APP_TITLE.sh" "$B/launch.sh" "$B/bin/$APP_BIN"
if [ "$QUICK" = 1 ]; then du -sh "$B"; echo "done (quick): dist/ports/$APP_DIR"; exit 0; fi
# the Ports shelf's picture: media/box/<entry's name>.png beside the entries
mkdir -p "$OUT/media/box" && cp /src/packaging/icon.png "$OUT/media/box/$APP_TITLE.png"

echo "bundled libraries:"; ls "$B/lib" | sed 's/^/  /'
du -sh "$B"
ZIP="/src/dist/$APP_DIR-$VERSION-aarch64.zip"	# the Store's release.asset: "MyApp-{version}-aarch64.zip"
cd "$OUT" && rm -f "$ZIP" && zip -qr "$ZIP" "$APP_TITLE.sh" "$APP_DIR" media
echo "done: dist/ports/ and dist/$(basename "$ZIP")"
