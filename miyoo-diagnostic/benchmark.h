/* Benchmark helpers for diagnostic.c. Times measure command submission,
 * queue completion and presentation separately; they are not GPU timestamps. */
static void bench_check(const char *stage, int rc, int *errors)
{
    if (rc < 0) {
        if (*errors == 0) result(stage, rc);
        ++*errors;
    }
}

enum BenchEffect {
    BENCH_NORMAL, BENCH_FLIP_H, BENCH_FLIP_V, BENCH_COLOR,
    BENCH_ALPHA, BENCH_UPDATE, BENCH_LOCK
};

static void bench_case(const char *name, int kind, int count, int size,
                       enum BenchEffect effect, SDL_Texture *texture, int frames)
{
    static Uint32 upload[64 * 64];
    Uint64 frequency = SDL_GetPerformanceFrequency();
    Uint64 draw_ticks = 0, flush_ticks = 0, present_ticks = 0;
    Uint64 shortest = ~(Uint64)0, longest = 0;
    int errors = 0, frame, j;
    int xmax = W - size + 1, ymax = H - size + 1;
    Uint64 start;
    if (!frequency) {
        result("benchmark_clock", SDL_SetError("Performance timer unavailable"));
        return;
    }
    bench_check("benchmark_color", SDL_SetTextureColorMod(texture,
                effect == BENCH_COLOR ? 128 : 255, 255, 255), &errors);
    bench_check("benchmark_alpha", SDL_SetTextureAlphaMod(texture,
                effect == BENCH_ALPHA ? 128 : 255), &errors);
    bench_check("benchmark_blend", SDL_SetTextureBlendMode(texture,
                effect == BENCH_ALPHA ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE), &errors);
    start = SDL_GetPerformanceCounter();
    for (frame = 0; frame < frames; ++frame) {
        Uint64 begin = SDL_GetPerformanceCounter(), submitted, flushed, presented;
        bench_check("benchmark_blend", SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE), &errors);
        bench_check("benchmark_color", SDL_SetRenderDrawColor(r, 0, 0, 0, 255), &errors);
        bench_check("benchmark_clear", SDL_RenderClear(r), &errors);
        if (effect == BENCH_UPDATE || effect == BENCH_LOCK) {
            for (j = 0; j < 64 * 64; ++j)
                upload[j] = 0xff000000u | ((Uint32)(j + frame) & 0x00ffffffu);
            if (effect == BENCH_UPDATE) {
                bench_check("benchmark_update", SDL_UpdateTexture(texture, NULL, upload, 64 * 4), &errors);
            } else {
                void *pixels;
                int pitch;
                int rc = SDL_LockTexture(texture, NULL, &pixels, &pitch);
                bench_check("benchmark_lock", rc, &errors);
                if (rc == 0) {
                    for (j = 0; j < 64; ++j)
                        SDL_memcpy((Uint8 *)pixels + (size_t)j * pitch, upload + j * 64, 64 * 4);
                    SDL_UnlockTexture(texture);
                }
            }
        }
        bench_check("benchmark_blend", SDL_SetRenderDrawBlendMode(r,
                    kind == 2 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE), &errors);
        for (j = 0; j < count; ++j) {
            SDL_Rect dst = {(j * 17) % xmax, (j * 13) % ymax, size, size};
            if (kind == 1) {
                if (effect == BENCH_FLIP_H || effect == BENCH_FLIP_V) {
                    bench_check("benchmark_copy_ex", SDL_RenderCopyEx(r, texture, NULL, &dst,
                                0, NULL, effect == BENCH_FLIP_H ? SDL_FLIP_HORIZONTAL : SDL_FLIP_VERTICAL), &errors);
                } else {
                    bench_check("benchmark_copy", SDL_RenderCopy(r, texture, NULL, &dst), &errors);
                }
            } else {
                bench_check("benchmark_color", SDL_SetRenderDrawColor(r,
                            (Uint8)(j * 9), (Uint8)(j * 5), (Uint8)(j * 3), kind == 2 ? 128 : 255), &errors);
                bench_check("benchmark_fill", SDL_RenderFillRect(r, &dst), &errors);
            }
        }
        submitted = SDL_GetPerformanceCounter();
        bench_check("benchmark_flush", SDL_RenderFlush(r), &errors);
        flushed = SDL_GetPerformanceCounter();
        SDL_RenderPresent(r);
        presented = SDL_GetPerformanceCounter();
        draw_ticks += submitted - begin;
        flush_ticks += flushed - submitted;
        present_ticks += presented - flushed;
        if (presented - begin < shortest) shortest = presented - begin;
        if (presented - begin > longest) longest = presented - begin;
    }
    {
        Uint64 elapsed = SDL_GetPerformanceCounter() - start;
        double ms = 1000.0 / (double)frequency;
        fprintf(f, "benchmark,%s,frames=%d,elapsed_ms=%.3f,operations=%d,fps=%.2f,"
                   "sprites=%d,size=%d,submit_ms=%.3f,flush_ms=%.3f,present_ms=%.3f,"
                   "min_frame_ms=%.3f,max_frame_ms=%.3f,errors=%d\n",
                name, frames, (double)elapsed * ms, frames * count,
                elapsed ? (double)frames * frequency / elapsed : 0.0,
                count, size, draw_ticks * ms, flush_ticks * ms, present_ticks * ms,
                shortest * ms, longest * ms, errors);
        fflush(f);
    }
    bench_check("benchmark_reset_color", SDL_SetTextureColorMod(texture, 255, 255, 255), &errors);
    bench_check("benchmark_reset_alpha", SDL_SetTextureAlphaMod(texture, 255), &errors);
    bench_check("benchmark_reset_blend", SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE), &errors);
}

static void bench_suite(void)
{
    SDL_Texture *large;
    Uint32 *pixels;
    int i, frames = 120;
    const char *setting = SDL_getenv("MIYOO_BENCH_FRAMES");
    if (setting && *setting) {
        char *end;
        long value = strtol(setting, &end, 10);
        if (*end || value < 1 || value > 10000) {
            result("benchmark_frames", SDL_SetError("MIYOO_BENCH_FRAMES must be 1..10000"));
            return;
        }
        frames = (int)value;
    }
    fprintf(f, "info,benchmark_version,2\n");
    bench_case("opaque_fill", 0, 24, 32, BENCH_NORMAL, t, frames);
    bench_case("texture_copy", 1, 24, 32, BENCH_NORMAL, t, frames);
    bench_case("alpha_fill", 2, 24, 32, BENCH_NORMAL, t, frames);
    bench_case("texture_copy_96", 1, 96, 32, BENCH_NORMAL, t, frames);
    bench_case("texture_copy_384", 1, 384, 32, BENCH_NORMAL, t, frames);
    bench_case("opaque_fill_384", 0, 384, 32, BENCH_NORMAL, t, frames);
    bench_case("alpha_fill_384", 2, 384, 32, BENCH_NORMAL, t, frames);
    bench_case("flip_horizontal_96", 1, 96, 32, BENCH_FLIP_H, t, frames);
    bench_case("flip_vertical_96", 1, 96, 32, BENCH_FLIP_V, t, frames);
    bench_case("color_mod_96", 1, 96, 32, BENCH_COLOR, t, frames);
    bench_case("alpha_mod_96", 1, 96, 32, BENCH_ALPHA, t, frames);
    bench_case("stream_update_24", 1, 24, 32, BENCH_UPDATE, t, frames);
    bench_case("stream_lock_24", 1, 24, 32, BENCH_LOCK, t, frames);
    large = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 256, 256);
    if (!large) { result("benchmark_large_texture", -1); return; }
    pixels = (Uint32 *)SDL_malloc(256 * 256 * sizeof(*pixels));
    if (!pixels) {
        result("benchmark_large_pixels", SDL_SetError("Cannot allocate large benchmark pixels"));
        SDL_DestroyTexture(large);
        return;
    }
    for (i = 0; i < 256 * 256; ++i) pixels[i] = 0xff000000u | (Uint32)i;
    i = SDL_UpdateTexture(large, NULL, pixels, 256 * 4);
    SDL_free(pixels);
    if (i < 0) {
        result("benchmark_large_update", i);
    } else {
        bench_case("large_texture_24", 1, 24, 128, BENCH_NORMAL, large, frames);
        bench_case("large_texture_96", 1, 96, 128, BENCH_NORMAL, large, frames);
    }
    SDL_DestroyTexture(large);
}
