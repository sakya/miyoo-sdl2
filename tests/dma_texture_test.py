#!/usr/bin/env python3
"""Compile actual MINI texture functions with mocked MI_SYS/MI_GFX APIs.

Usage: python tests/dma_texture_test.py --cc gcc
Requires a native C compiler; does not validate the SigmaStar device or ABI.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
VIDEO = (ROOT / 'sdl2-master/sdl2/src/video/mini/SDL_video_mini.c').read_text()
RENDER = (ROOT / 'sdl2-master/sdl2/src/render/mini/SDL_render_mini.c').read_text()


def function(source, name):
    """Extract a definition, rather than substituting a model of its behavior."""
    import re
    match = re.search(r'^(?:static )?(?:int|void) ' + name + r'\(', source, re.M)
    if not match:
        raise AssertionError(name)
    start = match.start()
    brace = source.index('{', match.end())
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


PREFIX = r'''
#define SDL_MAIN_HANDLED
#include "SDL.h"
#include "mi_sys.h"
#include "mi_gfx.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define debug(...) (void)0
#undef SDL_OutOfMemory
#undef SDL_Unsupported
#define SDL_OutOfMemory() (-1)
#define SDL_Unsupported() (-1)
#define SDL_SetError(...) (-1)
#define SDL_calloc calloc
#define SDL_free free
#define SDL_memset memset
static unsigned copies, flushes, blits, frees, unmaps, waits;
static int fail_alloc, fail_map, fail_flush, fail_wait;
static unsigned pending, peak, sequence;
static struct { void *pixels; size_t size; unsigned char bytes[256]; } reads[8];
static void *allocation;
static void *copy_pixels(void *d, const void *s, size_t n)
{ ++copies; return memcpy(d, s, n); }
#define SDL_memcpy copy_pixels

struct SDL_Texture { Uint32 format; int w, h; void *driverdata; };
static struct {
    struct { void *virAddr; MI_PHY phyAddr; } tmp;
    struct {
        struct { MI_GFX_Surface_t surf; MI_GFX_Rect_t rt; } src, dst;
        MI_GFX_Opt_t opt;
    } hw;
} gfx;
static int FB_W=640, FB_H=480, TMP_SIZE=640*480*4;
static size_t mini_frame_stride=640*4;
int GFX_WaitPending(void);
static MI_PHY Mini_CurrentFBAddress(void) { return 999; }
MI_S32 MI_SYS_MMA_Alloc(MI_U8 *name, MI_U32 size, MI_PHY *physical)
{
    (void)name;
    if (fail_alloc) return -1;
    allocation=malloc(size); assert(allocation);
    *physical=0; /* A successful allocation may have physical address zero. */
    return MI_SUCCESS;
}
MI_S32 MI_SYS_Mmap(MI_U64 physical, MI_U32 size, void **out, MI_BOOL cache)
{
    (void)physical; (void)size; (void)cache;
    if (fail_map) { if (fail_map==2) *out=allocation; return -1; }
    *out=allocation; return MI_SUCCESS;
}
MI_S32 MI_SYS_Munmap(void *p, MI_U32 size)
{ (void)p; (void)size; assert(!pending); ++unmaps; return MI_SUCCESS; }
MI_S32 MI_SYS_MMA_Free(MI_PHY physical)
{ (void)physical; assert(!pending); free(allocation); allocation=NULL; ++frees; return MI_SUCCESS; }
MI_S32 MI_SYS_FlushInvCache(void *p, MI_U32 size)
{ (void)p; assert(size>0); ++flushes; return fail_flush ? -1 : MI_SUCCESS; }
MI_S32 MI_GFX_BitBlit(MI_GFX_Surface_t *src, MI_GFX_Rect_t *sr,
    MI_GFX_Surface_t *dst, MI_GFX_Rect_t *dr, MI_GFX_Opt_t *opt, MI_U16 *fence)
{
    (void)sr; (void)dst; (void)dr; (void)opt;
    assert(pending<8);
    reads[pending].pixels=src->phyAddr==gfx.tmp.phyAddr ? gfx.tmp.virAddr : allocation;
    reads[pending].size=src->u32Stride*src->u32Height;
    assert(reads[pending].size<=sizeof(reads[pending].bytes));
    memcpy(reads[pending].bytes,reads[pending].pixels,reads[pending].size);
    ++pending; if(pending>peak)peak=pending;
    ++blits; *fence=(MI_U16)++sequence; return MI_SUCCESS;
}
MI_S32 MI_GFX_WaitAllDone(MI_BOOL all, MI_U16 fence)
{
    unsigned i;
    (void)all; assert(fence==(MI_U16)sequence); ++waits;
    if(fail_wait)return -1;
    for(i=0;i<pending;++i)
        if(reads[i].pixels)assert(memcmp(reads[i].pixels,reads[i].bytes,reads[i].size)==0);
    pending=0; return MI_SUCCESS;
}
MI_S32 MI_GFX_QuickFill(MI_GFX_Surface_t *dst, MI_GFX_Rect_t *rect,
    MI_U32 color, MI_U16 *fence)
{
    (void)dst; (void)rect; (void)color;
    assert(pending<8); reads[pending].pixels=NULL;
    ++pending; if(pending>peak)peak=pending;
    *fence=(MI_U16)++sequence; return MI_SUCCESS;
}
'''

TEST = r'''
int main(void)
{
    SDL_Texture tex = {SDL_PIXELFORMAT_ARGB8888, 4, 4, NULL};
    Mini_TextureData *t;
    SDL_Rect full={0,0,4,4}, part={1,1,2,2}, dst={10,10,8,8};
    SDL_Color neutral={255,255,255,255}, mod={128,255,255,255};
    Uint32 pixels[16];
    unsigned i, before;
    void *locked;
    int pitch;
    gfx.tmp.virAddr=calloc(1,TMP_SIZE); gfx.tmp.phyAddr=100;
    assert(Mini_CreateTexture(NULL,&tex)==0);
    t=(Mini_TextureData *)tex.driverdata;
    assert(t->dma && t->dirty && t->physical==0);
    for (i=0;i<16;++i) pixels[i]=0xff804020u+i;
    assert(Mini_UpdateTexture(NULL,&tex,&full,pixels,16)==0);
    before=copies;
    assert(GFX_CopyTextureResident(t->data,4,4,t->pitch,t->fmt,part,dst,
        SDL_BLENDMODE_NONE,neutral,SDL_FLIP_NONE,t->physical,&t->dirty)==0);
    assert(copies==before && flushes==1 && blits==1 && !t->dirty);
    assert(waits==0 && pending==1);
    assert(gfx.hw.src.surf.phyAddr==t->physical);
    assert(gfx.hw.src.surf.u32Stride==t->pitch);
    assert(gfx.hw.src.surf.u32Width==4 && gfx.hw.src.surf.u32Height==4);
    assert(gfx.hw.src.rt.s32Xpos==1 && gfx.hw.src.rt.s32Ypos==1);
    assert(gfx.hw.src.rt.u32Width==2 && gfx.hw.src.rt.u32Height==2);
    for (i=0;i<100;++i)
        assert(GFX_CopyTextureResident(t->data,4,4,t->pitch,t->fmt,part,dst,
            SDL_BLENDMODE_BLEND,neutral,SDL_FLIP_NONE,t->physical,&t->dirty)==0);
    assert(copies==before && flushes==1 && blits==101);
    assert(waits==12 && pending==5 && peak==8);

    /* Partial writes and streaming locks must invalidate the resident cache. */
    assert(Mini_UpdateTexture(NULL,&tex,&part,pixels,16)==0 && t->dirty);
    assert(GFX_CopyTextureResident(t->data,4,4,t->pitch,t->fmt,part,dst,
        SDL_BLENDMODE_NONE,neutral,SDL_FLIP_NONE,t->physical,&t->dirty)==0);
    assert(flushes==2 && !t->dirty);
    assert(((Uint32 *)t->data)[5]==pixels[0]);
    assert(Mini_LockTexture(NULL,&tex,&part,&locked,&pitch)==0);
    *(Uint32 *)locked=0x12345678;
    Mini_UnlockTexture(NULL,&tex);
    assert(t->dirty && pitch==16 && ((Uint32 *)t->data)[5]==0x12345678);

    /* A failed cache sync must remain dirty and must not submit a blit. */
    before=blits; fail_flush=1;
    assert(GFX_CopyTextureResident(t->data,4,4,t->pitch,t->fmt,full,dst,
        SDL_BLENDMODE_NONE,neutral,SDL_FLIP_NONE,t->physical,&t->dirty)<0);
    assert(t->dirty && blits==before); fail_flush=0;
    assert(GFX_CopyTextureResident(t->data,4,4,t->pitch,t->fmt,full,dst,
        SDL_BLENDMODE_NONE,neutral,SDL_FLIP_NONE,t->physical,&t->dirty)==0);
    assert(!t->dirty && blits==before+1);

    /* Effects use staging without modifying resident pixels or clearing dirty. */
    assert(Mini_UpdateTexture(NULL,&tex,&full,pixels,16)==0);
    assert(GFX_CopyTextureResident(t->data,4,4,t->pitch,t->fmt,full,dst,
        SDL_BLENDMODE_NONE,neutral,SDL_FLIP_HORIZONTAL,t->physical,&t->dirty)==0);
    assert(gfx.hw.src.surf.phyAddr==gfx.tmp.phyAddr && t->dirty);
    assert(((Uint32 *)gfx.tmp.virAddr)[0]==pixels[3]);
    assert(GFX_CopyTextureResident(t->data,4,4,t->pitch,t->fmt,full,dst,
        SDL_BLENDMODE_NONE,mod,SDL_FLIP_NONE,t->physical,&t->dirty)==0);
    assert(((Uint32 *)gfx.tmp.virAddr)[0]==0xff404020u);
    assert(memcmp(t->data,pixels,sizeof(pixels))==0 && t->dirty);
    Mini_DestroyTexture(NULL,&tex);
    assert(!tex.driverdata && frees==1 && unmaps==1);

    /* Exhaustion and mapping failures preserve the old heap/staging path. */
    fail_alloc=1;
    assert(Mini_CreateTexture(NULL,&tex)==0);
    t=(Mini_TextureData *)tex.driverdata; assert(!t->dma);
    assert(Mini_UpdateTexture(NULL,&tex,&full,pixels,16)==0);
    assert(GFX_CopyTextureEx(t->data,4,4,t->pitch,t->fmt,full,dst,
        SDL_BLENDMODE_NONE,neutral,SDL_FLIP_NONE)==0);
    assert(memcmp(gfx.tmp.virAddr,pixels,sizeof(pixels))==0);
    Mini_DestroyTexture(NULL,&tex); assert(frees==1 && unmaps==1);
    fail_alloc=0; fail_map=1;
    assert(Mini_CreateTexture(NULL,&tex)==0);
    t=(Mini_TextureData *)tex.driverdata; assert(!t->dma && frees==2);
    Mini_DestroyTexture(NULL,&tex); fail_map=0;
    fail_map=2;
    assert(Mini_CreateTexture(NULL,&tex)==0);
    t=(Mini_TextureData *)tex.driverdata;
    assert(!t->dma && frees==3 && unmaps==2);
    Mini_DestroyTexture(NULL,&tex); fail_map=0;

    /* RGB565 also keeps its original stride and offsets for direct copies. */
    tex.format=SDL_PIXELFORMAT_RGB565;
    assert(Mini_CreateTexture(NULL,&tex)==0);
    t=(Mini_TextureData *)tex.driverdata;
    assert(GFX_CopyTextureResident(t->data,4,4,t->pitch,t->fmt,part,dst,
        SDL_BLENDMODE_NONE,neutral,SDL_FLIP_NONE,t->physical,&t->dirty)==0);
    assert(gfx.hw.src.surf.u32Stride==8);
    assert(gfx.hw.src.surf.eColorFmt==E_MI_GFX_FMT_RGB565);
    assert(gfx.hw.src.rt.s32Xpos==1);
    Mini_DestroyTexture(NULL,&tex);
    assert(!pending);

    /* Mixed fill/blit batches share one fence and have no new pixel buffers. */
    assert(Mini_CreateTexture(NULL,&tex)==0);
    t=(Mini_TextureData *)tex.driverdata;
    before=waits;
    assert(GFX_CopyTextureResident(t->data,4,4,t->pitch,t->fmt,full,dst,
        SDL_BLENDMODE_NONE,neutral,SDL_FLIP_NONE,t->physical,&t->dirty)==0);
    assert(GFX_FillRect(dst,0xff000000)==0);
    assert(GFX_CopyTextureResident(t->data,4,4,t->pitch,t->fmt,full,dst,
        SDL_BLENDMODE_NONE,neutral,SDL_FLIP_NONE,t->physical,&t->dirty)==0);
    assert(pending==3 && waits==before);
    assert(GFX_WaitPending()==0 && waits==before+1 && !pending);

    /* Failed waits prohibit CPU writes and DMA release until a retry succeeds. */
    assert(GFX_CopyTextureResident(t->data,4,4,t->pitch,t->fmt,full,dst,
        SDL_BLENDMODE_NONE,neutral,SDL_FLIP_NONE,t->physical,&t->dirty)==0);
    fail_wait=1;
    assert(Mini_UpdateTexture(NULL,&tex,&full,pixels,16)<0);
    assert(Mini_LockTexture(NULL,&tex,&full,&locked,&pitch)<0);
    Mini_DestroyTexture(NULL,&tex);
    assert(tex.driverdata && pending==1);
    fail_wait=0;
    Mini_DestroyTexture(NULL,&tex);
    assert(!pending && !tex.driverdata);

    /* A failed full-batch wait must not allow a ninth hardware submission. */
    fail_wait=1;
    for(i=0;i<7;++i)assert(GFX_FillRect(dst,0)==0);
    assert(GFX_FillRect(dst,0)<0 && pending==8);
    assert(GFX_FillRect(dst,0)<0 && pending==8);
    fail_wait=0;
    assert(GFX_WaitPending()==0 && !pending);
    free(gfx.tmp.virAddr);
    puts("PASS: resident reuse, dirty updates/locks, effects, DMA fallback, RGB565, bounded async batches, safe updates/free, wait failure retry");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    texture_struct = RENDER[RENDER.index('typedef struct Mini_TextureData'):]
    texture_struct = texture_struct[:texture_struct.index('} Mini_TextureData;') + len('} Mini_TextureData;')]
    render_functions = '\n'.join(function(RENDER, name) for name in (
        'Mini_CreateTexture', 'Mini_LockTexture', 'Mini_UpdateTexture',
        'Mini_UnlockTexture', 'Mini_DestroyTexture'))
    video_functions = '\n'.join(function(VIDEO, name) for name in (
        'GFX_FillRect', 'Mini_SetBlitOptions', 'Mini_CopyTexture', 'GFX_CopyTextureEx',
        'GFX_CopyTextureResident'))
    scheduler = VIDEO[VIDEO.index('#define MINI_GFX_BATCH_MAX'):VIDEO.index('static int Mini_QuitGFX')]
    with tempfile.TemporaryDirectory(prefix='mini-dma-') as temp:
        source = Path(temp) / 'test.c'
        exe = Path(temp) / 'test.exe'
        source.write_text(PREFIX + scheduler + texture_struct + render_functions + video_functions + TEST)
        subprocess.run([args.cc, '-std=c99', '-Wall', '-Wextra',
                        '-Wno-unused-parameter', '-Werror=declaration-after-statement',
                        '-I' + str(ROOT / 'sdl2-master/sdl2/include'),
                        '-I' + str(ROOT / 'sdl2-master/mini/inc'),
                        str(source), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
