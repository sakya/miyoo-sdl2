# SDL2 Miyoo Mini

SDL2 for the Miyoo Mini, based on https://github.com/steward-fu/sdl2 completed and optimized by ChatGPT

## Build
In a [union-miyoomini-toolchain](https://github.com/shauninman/union-miyoomini-toolchain) shell:

```sh
git clone https://github.com/sakya/miyoo-sdl2.git
apt-get update && apt-get install -y autoconf automake libtool pkg-config cmake zip
chmod +x build-union.sh
chmod +x sdl2-master/sdl2/autogen.sh

export CROSS_COMPILE=arm-linux-gnueabihf-
./build-union.sh 2>&1 | tee build.log
```

The build results are in `output/`

