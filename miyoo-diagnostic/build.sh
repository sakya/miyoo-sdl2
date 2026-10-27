#!/bin/sh
set -eu
# Build using the same compiler/sysroot used to build the custom SDL2.
: "${CC:=arm-linux-gnueabihf-gcc}"
: "${SDL2_CONFIG:=sdl2-config}"
"$CC" -O2 -std=c99 -Wall -Wextra diagnostic.c -o miyoo_sdl2_diagnostic $("$SDL2_CONFIG" --cflags --libs)
