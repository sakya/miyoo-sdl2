#!/usr/bin/env python3
"""Exercise the real benchmark suite with mocked SDL calls and a native GCC."""
import argparse
import csv
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#define SDL_MAIN_HANDLED
#include "SDL.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define W 320
#define H 240
static FILE *f;
static SDL_Renderer *r;
static SDL_Texture *t=(SDL_Texture *)1;
static int failures;
static unsigned fills,copies,flips,flushes,presents,updates,locks,unlocks;
static void result(const char *name,int rc)
{ if(rc<0){++failures;fprintf(f,"test,%s,FAIL\n",name);} }
static int state(void) { return 0; }
static int copy(void) { ++copies;return getenv("TEST_COPY_ERROR") ? -1 : 0; }
static int flip(void) { ++flips;return 0; }
static int fill(void) { ++fills;return 0; }
static int flush(void) { ++flushes;return 0; }
static void present(void) { ++presents; }
static int update(void) { ++updates;return 0; }
static void unlock(void) { ++unlocks; }
static int lock(void **pixels,int *pitch)
{ static Uint32 data[64*64];++locks;*pixels=data;*pitch=256;return 0; }
static Uint64 counter(void) { static Uint64 ticks;ticks+=100;return ticks; }
#define SDL_SetTextureColorMod(...) state()
#define SDL_SetTextureAlphaMod(...) state()
#define SDL_SetTextureBlendMode(...) state()
#define SDL_SetRenderDrawBlendMode(...) state()
#define SDL_SetRenderDrawColor(...) state()
#define SDL_RenderClear(...) state()
#define SDL_RenderCopy(...) copy()
#define SDL_RenderCopyEx(...) flip()
#define SDL_RenderFillRect(...) fill()
#define SDL_RenderFlush(...) flush()
#define SDL_RenderPresent(...) present()
#define SDL_UpdateTexture(...) update()
#define SDL_LockTexture(texture,rect,pixels,pitch) lock(pixels,pitch)
#define SDL_UnlockTexture(...) unlock()
#define SDL_CreateTexture(...) ((SDL_Texture *)2)
#define SDL_DestroyTexture(...) ((void)0)
#define SDL_GetPerformanceFrequency() ((Uint64)1000000)
#define SDL_GetPerformanceCounter() counter()
#define SDL_SetError(...) (-1)
#define SDL_malloc malloc
#define SDL_free free
#define SDL_memcpy memcpy
#define SDL_getenv getenv
#include "miyoo-diagnostic/benchmark.h"
int main(void)
{
    const char *value=getenv("MIYOO_BENCH_FRAMES");
    unsigned frames=value ? (unsigned)atoi(value) : 120;
    f=stdout;bench_suite();
    if(!failures){
        assert(fills==816*frames && copies==864*frames && flips==192*frames);
        assert(flushes==15*frames && presents==15*frames);
        assert(updates==frames+1 && locks==frames && unlocks==frames);
    }
    return failures ? 1 : 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='mini-bench-') as directory:
        source = Path(directory) / 'test.c'
        exe = Path(directory) / 'test.exe'
        source.write_text(HARNESS)
        subprocess.run([args.cc, '-std=c99', '-I' + str(ROOT),
                        '-I' + str(ROOT / 'sdl2-master/sdl2/include'),
                        str(source), '-o', str(exe)], check=True)
        base = os.environ.copy()
        base.pop('MIYOO_BENCH_FRAMES', None)
        base.pop('TEST_COPY_ERROR', None)
        for frames in (None, '2'):
            env = dict(base)
            if frames:
                env['MIYOO_BENCH_FRAMES'] = frames
            run = subprocess.run([str(exe)], env=env, capture_output=True, text=True, check=True)
            rows = [row for row in csv.reader(run.stdout.splitlines()) if row[0] == 'benchmark']
            assert len(rows) == 15
            for row in rows:
                values = dict(field.split('=', 1) for field in row[2:])
                assert int(values['frames']) == int(frames or '120')
                assert int(values['operations']) == int(values['frames']) * int(values['sprites'])
                assert values['errors'] == '0'
                assert float(values['elapsed_ms']) > 0 and float(values['fps']) > 0
                assert float(values['submit_ms']) > 0 and float(values['flush_ms']) > 0
                assert float(values['present_ms']) > 0
                assert float(values['min_frame_ms']) <= float(values['max_frame_ms'])
        for invalid in ('0', '-1', '10001', 'abc', '2x'):
            env = dict(base, MIYOO_BENCH_FRAMES=invalid)
            run = subprocess.run([str(exe)], env=env, capture_output=True, text=True)
            assert run.returncode == 1 and 'benchmark_frames,FAIL' in run.stdout
        run = subprocess.run([str(exe)], env=dict(base, MIYOO_BENCH_FRAMES='1', TEST_COPY_ERROR='1'),
                             capture_output=True, text=True)
        assert run.returncode == 1 and 'benchmark_copy,FAIL' in run.stdout
        assert 'errors=384' in run.stdout
    print('PASS: 15 workloads, operation counts, timings, frame configuration, error reporting')


if __name__ == '__main__':
    main()
