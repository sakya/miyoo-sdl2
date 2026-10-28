# Miyoo Mini / Mini Plus — SDL2 diagnostic (phase 16)

This package contains **the test source code**, not a precompiled ARM executable.

## Building

Use the compiler from union-miyoomini-toolchain and `sdl2-config` from the **same modified SDL2 build** (phase 16). For example, after configuring the toolchain and installing SDL2 in the sysroot:

```sh
export CC=/percorso/toolchain/bin/arm-linux-gnueabihf-gcc
export SDL2_CONFIG=/percorso/sysroot/usr/bin/sdl2-config
sh build.sh
file miyoo_sdl2_diagnostic
```

These paths are placeholders: adjust them to your installed toolchain. If `sdl2-config` returns host paths instead of target paths, correct the compiler and linker options.

## Running

1. Copy `miyoo_sdl2_diagnostic`, `run.sh`, and the modified SDL2 library to a directory on the SD card. Place the library in `lib/` **only if needed**; verify that the executable uses the modified version rather than the system library.
2. Start `run.sh` from the launcher or a shell on the handheld. Do not run the test while another program is using the framebuffer.
3. Seven scenes are displayed for approximately 1.8 seconds each: RGB bars, rectangle blending, diagonal lines, textures/scaling/clipping, modulation, flips, and viewport/clipping. Each scene is automatically saved as a BMP in `screenshots/`; press `ESC` to stop.
4. The test then runs 15 benchmarks of 120 frames each: fills and copies with increasing workloads, large textures, flips, modulation, and streaming updates.
5. Provide `miyoo-results.csv`, `miyoo-stdout.log`, the `screenshots/` directory, the Miyoo model (Mini/Mini Plus), firmware, and SDL2 version.

## Screenshots

Seven files are saved, from `screenshots/scene-00-bars.bmp` to
`screenshots/scene-06-viewport.bmp`. The intermediate filenames identify blending,
lines, textures, modulation, and flips. Files are overwritten on each run.

Capture reads the page currently displayed by `/dev/fb0`, after the scene has
been flushed and presented. It uses an uncached MI_SYS mapping, interprets the
framebuffer's color channels, and corrects the MINI backend's 180-degree rotation.
Screenshots are saved before the benchmarks, outside the measured intervals.
No SDL_image, PNG support, or additional framebuffer buffers are required:
only one row buffer is used (1920 bytes for a 640×480 image). The seven BMPs
at 640×480 occupy approximately 6.2 MiB in total on the SD card.

`build.sh` also links `libmi_sys`, included in `sdl2-master/mini/lib`, and uses
the SDK headers in `sdl2-master/mini/inc`. This feature does not require rebuilding
SDL2, but you must rebuild and copy the diagnostic executable again.

Each save result appears in the CSV. A capture error produces `FAIL` and a
nonzero exit code without preventing the remaining tests from running.
To disable screenshots:

```sh
MIYOO_SCREENSHOTS=0 sh run.sh
```

## Interpreting the results

Benchmark version 2 retains the `opaque_fill`, `texture_copy`, and `alpha_fill`
cases with 24 operations per frame. It adds copies with 96 and 384 sprites,
opaque and alpha fills with 384 rectangles, horizontal/vertical flips and
color/alpha modulation with 96 sprites, updates through `SDL_UpdateTexture`
and lock/unlock, and a 256×256 texture drawn at 128×128 with 24 and 96 sprites.
Benchmark positions and colors have changed: use the same version 2 diagnostic
executable when comparing two library versions.

Each `benchmark` row also records:

- `submit_ms`: total time spent preparing and submitting commands, including
  streaming updates. This may include waits caused by automatic flushes.
- `flush_ms`: total time spent in `SDL_RenderFlush`, including waits for hardware
  operations queued by the MINI backend.
- `present_ms`: total time spent in `SDL_RenderPresent`.
- `min_frame_ms` and `max_frame_ms`: minimum and maximum observed frame duration.
- `errors`: errors reported by the checked SDL calls in the current case.

The three timings are totals for the entire case, not per-frame averages or GPU
timestamps. `SDL_RenderPresent` does not return an error code: also check the log
and visual output. A case with errors is not valid for performance comparisons.

For a shorter or longer run, set the frame count from a shell on the handheld:

```sh
MIYOO_BENCH_FRAMES=30 sh run.sh
```

The default is 120; values from 1 to 10000 are accepted. The diagnostic adds
approximately 528 KiB of data at peak: a 256 KiB large texture, a temporary
256 KiB CPU buffer, and a 16 KiB streaming buffer. The temporary buffer is freed
before the large-texture benchmarks. Backend batching requires no additional
pixel buffers: it reuses resident DMA textures and limits operations in flight
to eight, waiting for completion before modifications, release, and presentation.

`PASS` only means that the checked call succeeded: it **does not** guarantee correct pixels. FPS values are indicative and depend on presentation/vsync and workload. The program saves framebuffer pixels but does not automatically compare them with the software renderer. The new screenshot feature has not yet been verified on hardware.

Warning: this program interacts directly with the custom video driver; test with your SD card and data safely backed up.
