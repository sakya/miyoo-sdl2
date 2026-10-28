# SDL2 Miyoo Mini

SDL2 for the Miyoo Mini, base on https://github.com/steward-fu/sdl2 and optimized by ChatGPT

## Build

In a union-miyoomini-toolchain  shell:

```sh
git clone https://github.com/sakya/miyoo-sdl2.git
apt-get update && apt-get install -y autoconf automake libtool pkg-config cmake zip
chmod +x build-union.sh
chmod +x sdl2-master/sdl2/autogen.sh

export CROSS_COMPILE=arm-linux-gnueabihf-
./build-union.sh 2>&1 | tee build.log
```

The build results are in `output/`

## Texture performance

Resident DMA textures avoid repeated uploads for plain copies. Color/alpha
modulation and flips reuse the last transformed source rectangle in the existing
staging buffer. Repeated identical transformations can be batched; another
texture, source rectangle or transformation replaces that entry. Texture writes,
locks, destruction and other staging-buffer users invalidate it.

New transformations combine copying and modulation in one pass. Aligned
horizontal flips use whole-pixel loads/stores; unaligned sources keep a safe
copy path. No extra pixel buffers are allocated for this cache, only a few dozen
bytes of metadata. Compare results using the same version 2 diagnostic executable
against each library build; hardware performance must be measured on the device.

To build you need autoconf, automake, libtool, make e ARM toolchain.

## Original precompiled libraries

The original steward-fu binaries are included in `sdl2-master/prebuilt/320x240/`
and `sdl2-master/prebuilt/640x480/`. Each directory contains:

- `libSDL2-2.0.so.0`
- `libEGL.so`
- `libGLESv2.so`
- `libjson-c.so.5` (copied from the original examples)

Copy the libraries from one resolution directory next to your executable on the
Miyoo Mini, then launch it from that directory:

```sh
LD_LIBRARY_PATH=.:/config/lib:/customer/lib ./your-program
```

These are upstream precompiled binaries; they do not contain this project's
source changes. Build with `./build-union.sh` to obtain SDL2 with those changes
in `output/lib/`. Do not overwrite that build with the upstream SDL2 binary.

Upstream source and build instructions: https://github.com/steward-fu/sdl2
