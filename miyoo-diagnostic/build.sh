#!/bin/sh
set -eu
# Copy library
mkdir -p lib
cp -L ../output/lib/libSDL2-2.0.so.0 lib/
cp ../sdl2-master/prebuilt/640x480/libEGL.so lib/
cp ../sdl2-master/prebuilt/640x480/libGLESv2.so lib/

# Build using the same compiler/sysroot used to build the custom SDL2.
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
: "${CC:=arm-linux-gnueabihf-gcc}"
: "${SDL2_CONFIG:=$ROOT/output/bin/sdl2-config}"
# Resolve dependencies of the shared SDL2 library at link time. These paths
# are deliberately not embedded as runtime rpaths on the handheld.
SDL_FLAGS=$("$SDL2_CONFIG" --cflags --libs)
"$CC" -O2 -std=c99 -Wall -Wextra "$HERE/diagnostic.c" \
    -o "$HERE/miyoo_sdl2_diagnostic" \
    "-Wl,-rpath-link,$ROOT/output/lib" \
    "-Wl,-rpath-link,$ROOT/sdl2-master/prebuilt/640x480" \
    "-Wl,-rpath-link,$ROOT/sdl2-master/mini/lib" \
    $SDL_FLAGS
