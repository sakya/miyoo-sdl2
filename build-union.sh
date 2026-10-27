#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/sdl2-master/sdl2"
: "${CROSS_COMPILE:?Impostare CROSS_COMPILE, ad esempio arm-linux-gnueabihf-}"
for x in autoconf automake libtoolize make "${CROSS_COMPILE}gcc" "${CROSS_COMPILE}readelf"; do command -v "$x" >/dev/null || { echo "Manca $x" >&2; exit 1; }; done
export CC="${CROSS_COMPILE}gcc" CXX="${CROSS_COMPILE}g++" AR="${CROSS_COMPILE}ar" RANLIB="${CROSS_COMPILE}ranlib" STRIP="${CROSS_COMPILE}strip"
export CFLAGS="${CFLAGS:--O2 -marm -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard}"
export LDFLAGS="-L$ROOT/sdl2-master/mini/lib ${LDFLAGS:-}"
(cd "$SRC" && sh ./autogen.sh)
mkdir -p "$ROOT/build-arm" "$ROOT/output"
cd "$ROOT/build-arm"
"$SRC/configure" --host=arm-linux-gnueabihf --prefix="$ROOT/output" --disable-static --enable-shared --enable-video-mini --enable-audio-mini --disable-video-x11 --disable-video-wayland --disable-video-kmsdrm --disable-video-vulkan --disable-video-opengl --disable-video-opengles --disable-video-opengles2 --disable-pulseaudio --disable-pipewire --disable-alsa --disable-oss --disable-dbus --disable-libudev --disable-hidapi
make -j"${JOBS:-4}" V=1
make install
find "$ROOT/output/lib" -name 'libSDL2-2.0.so*' -print
