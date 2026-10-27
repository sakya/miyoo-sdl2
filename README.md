# SDL2 Miyoo Mini

SDL2 for the Miyoo Mini, base on https://github.com/steward-fu/sdl2 and optimized by ChatGPT

## Build

In a union-miyoomini-toolchain  shell:

```sh
export CROSS_COMPILE=arm-linux-gnueabihf-  
./build-union.sh 2>&1 | tee build.log
```

The build results are in `output/`

To build you need autoconf, automake, libtool, make e ARM toolchain.
