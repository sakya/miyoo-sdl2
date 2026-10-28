#!/usr/bin/env python3
"""Test the real screenshot code with mocked Linux framebuffer/MI_SYS calls.

Native tests validate byte layout, viewport selection, rotation and cleanup;
the mock UAPI structs do not validate the ARM ABI or device driver.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FB_HEADER = r'''
#include <stdint.h>
struct fb_bitfield { uint32_t offset,length,msb_right; };
struct fb_fix_screeninfo { unsigned long smem_start; uint32_t smem_len,type,visual,line_length; };
struct fb_var_screeninfo {
    uint32_t xres,yres,xoffset,yoffset,bits_per_pixel,nonstd,grayscale;
    struct fb_bitfield red,green,blue;
};
#define FBIOGET_FSCREENINFO 1
#define FBIOGET_VSCREENINFO 2
#define FB_TYPE_PACKED_PIXELS 0
#define FB_VISUAL_TRUECOLOR 2
'''
HARNESS = r'''
#define SDL_MAIN_HANDLED
#include "SDL.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include "linux/fb.h"
#include "mi_sys.h"
static Uint32 framebuffer[16];
static unsigned maps,unmaps,results,max_alloc;
static int fail_open,fail_ioctl,fail_map,fail_unmap,bad_layout,bad_offset;
static const char *setting;
static char error[256];
static int mock_error(const char *fmt,...)
{ va_list ap;va_start(ap,fmt);vsnprintf(error,sizeof(error),fmt,ap);va_end(ap);return -1; }
static void *mock_calloc(size_t n,size_t size)
{ if(n*size>max_alloc)max_alloc=(unsigned)(n*size);return calloc(n,size); }
static int mock_open(const char *path,int mode)
{ assert(strcmp(path,"/dev/fb0")==0 && mode==O_RDONLY);return fail_open?-1:42; }
static int mock_close(int fd) { assert(fd==42);return 0; }
static int mock_ioctl(int fd,unsigned long command,...)
{
    va_list ap;void *out;
    assert(fd==42);if(fail_ioctl)return -1;
    va_start(ap,command);out=va_arg(ap,void *);va_end(ap);
    if(command==FBIOGET_FSCREENINFO){
        struct fb_fix_screeninfo *v=(struct fb_fix_screeninfo *)out;
        memset(v,0,sizeof(*v));v->smem_start=4096;v->smem_len=64;
        v->line_length=16;v->visual=FB_VISUAL_TRUECOLOR;
    }else{
        struct fb_var_screeninfo *v=(struct fb_var_screeninfo *)out;
        assert(command==FBIOGET_VSCREENINFO);
        memset(v,0,sizeof(*v));v->xres=3;v->yres=2;v->yoffset=bad_offset?0xffffffffu:2;
        v->bits_per_pixel=bad_layout?16:32;
        v->red.offset=16;v->red.length=v->green.length=v->blue.length=8;
        v->green.offset=8;
    }
    return 0;
}
static int mock_mkdir(const char *path,unsigned mode)
{
    (void)mode;
#ifdef _WIN32
    return _mkdir(path);
#else
    (void)path;return 0;
#endif
}
static const char *mock_getenv(const char *name)
{ assert(strcmp(name,"MIYOO_SCREENSHOTS")==0);return setting; }
static void result(const char *name,int rc)
{ assert(strstr(name,"screenshots/scene-")==name && rc==0);++results; }
#undef SDL_OutOfMemory
#define SDL_OutOfMemory() mock_error("out of memory")
#define SDL_SetError mock_error
#define SDL_calloc mock_calloc
#define SDL_free free
#define SDL_memcpy memcpy
#define SDL_getenv mock_getenv
#define open mock_open
#define close mock_close
#define ioctl mock_ioctl
#define mkdir mock_mkdir
#ifndef __linux__
#define __linux__ 1
#endif
#include "miyoo-diagnostic/screenshot.h"
MI_S32 MI_SYS_Mmap(MI_U64 physical,MI_U32 size,void **out,MI_BOOL cached)
{ assert(physical==4096 && size==64 && cached==FALSE);++maps;if(fail_map)return -1;*out=framebuffer;return 0; }
MI_S32 MI_SYS_Munmap(void *p,MI_U32 size)
{ assert(p==framebuffer && size==64);++unmaps;return fail_unmap?-1:0; }
int main(void)
{
    unsigned i,before;
    ScreenshotView view;
    Uint32 swapped=0xff112233;
    for(i=0;i<6;++i){
        unsigned k=i+1;
        framebuffer[8+(i/3)*4+i%3]=0xff000000u|(k*10<<16)|(k*20<<8)|(k*30);
    }
    assert(screenshot_capture("displayed.bmp")==0 && maps==1 && unmaps==1);
    assert(max_alloc==12); /* One padded row, not an entire image. */
    before=maps;bad_offset=1;
    assert(screenshot_capture("bad-offset.bmp")<0 && maps==before);bad_offset=0;
    bad_layout=1;assert(screenshot_capture("bad-layout.bmp")<0 && maps==before);bad_layout=0;
    fail_open=1;assert(screenshot_capture("bad-open.bmp")<0);fail_open=0;
    fail_ioctl=1;assert(screenshot_capture("bad-ioctl.bmp")<0);fail_ioctl=0;
    fail_map=1;before=unmaps;assert(screenshot_capture("bad-map.bmp")<0 && unmaps==before);fail_map=0;
    before=unmaps;assert(screenshot_capture("missing/scene.bmp")<0 && unmaps==before+1);
    fail_unmap=1;assert(screenshot_capture("bad-unmap.bmp")<0);fail_unmap=0;
    memset(&view,0,sizeof(view));view.pixels=(const Uint8 *)&swapped;
    view.width=view.height=1;view.stride=4;
    view.red_length=view.green_length=view.blue_length=8;
    view.green_offset=8;view.blue_offset=16;
    assert(screenshot_write_bmp("swapped.bmp",&view)==0);
    view.red_length=0;assert(screenshot_write_bmp("bad-mask.bmp",&view)<0);
    view.red_length=8;view.width=4097;assert(screenshot_write_bmp("bad-size.bmp",&view)<0);
    setting="0";before=maps;screenshot_scene(0);assert(maps==before && results==0);
    setting=NULL;for(i=0;i<7;++i)screenshot_scene((int)i);
    assert(results==7 && max_alloc==12);
    puts("PASS: displayed framebuffer page, uncached reads, BMP layout/rotation/colors, row memory, errors/cleanup, seven scenes and disable option");
    return 0;
}
'''


def read_bmp(path):
    data = path.read_bytes()
    assert data[:2] == b'BM' and len(data) == struct.unpack_from('<I', data, 2)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    assert struct.unpack_from('<HH', data, 26) == (1, 24)
    stride = (width * 3 + 3) & ~3
    assert len(data) == 54 + stride * height
    pixels = []
    for y in reversed(range(height)):
        pixels.append([tuple(reversed(data[54+y*stride+x*3:54+y*stride+x*3+3])) for x in range(width)])
        assert data[54+y*stride+width*3:54+(y+1)*stride] == bytes(stride-width*3)
    return pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='mini-screenshot-') as directory:
        temp = Path(directory)
        (temp / 'linux').mkdir()
        (temp / 'sys').mkdir()
        (temp / 'screenshots').mkdir()
        (temp / 'linux/fb.h').write_text('#ifndef TEST_FB_H\n#define TEST_FB_H\n' + FB_HEADER + '\n#endif\n')
        (temp / 'sys/ioctl.h').write_text('int ioctl(int, unsigned long, ...);\n')
        (temp / 'sys/stat.h').write_text('int mkdir(const char *, unsigned);\n')
        source = temp / 'test.c'
        source.write_text(HARNESS)
        exe = temp / 'test.exe'
        subprocess.run([args.cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(temp), '-I' + str(ROOT),
                        '-I' + str(ROOT / 'sdl2-master/sdl2/include'),
                        '-I' + str(ROOT / 'sdl2-master/mini/inc'),
                        str(source), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], cwd=temp, check=True)
        # Also compile the complete diagnostic's Linux capture branch. System
        # framebuffer structs are still mocked, so this is not an ARM build.
        diagnostic = temp / 'diagnostic.c'
        diagnostic.write_text('#define SDL_MAIN_HANDLED\n#include "SDL.h"\n'
                              '#include <fcntl.h>\n#include <unistd.h>\n'
                              '#define mkdir test_linux_mkdir\n'
                              '#ifndef __linux__\n#define __linux__ 1\n#endif\n'
                              '#include "miyoo-diagnostic/diagnostic.c"\n')
        subprocess.run([args.cc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-fsyntax-only',
                        '-I' + str(temp), '-I' + str(ROOT),
                        '-I' + str(ROOT / 'sdl2-master/sdl2/include'),
                        '-I' + str(ROOT / 'sdl2-master/mini/inc'), str(diagnostic)], check=True)
        expected = [[(k*10, k*20, k*30) for k in (6,5,4)], [(k*10, k*20, k*30) for k in (3,2,1)]]
        assert read_bmp(temp / 'displayed.bmp') == expected
        assert read_bmp(temp / 'swapped.bmp') == [[(0x33,0x22,0x11)]]
        shots = list((temp / 'screenshots').glob('scene-*.bmp'))
        assert len(shots) == 7
        for shot in shots:
            assert read_bmp(shot) == expected
    print('PASS: BMP files decoded with expected dimensions, padding, colors and orientation')


if __name__ == '__main__':
    main()
