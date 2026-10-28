#!/usr/bin/env python3
"""Compile the real MINI rectangle mapping and check physical/display coordinates."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#define SDL_MAIN_HANDLED
#include "SDL.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#define SDL_floor floor
struct SDL_Window { int w,h; };
static SDL_Window window={320,240};
static SDL_Window *vid_win=&window;
static int FB_W=640,FB_H=480;
'''
TEST = r'''
static void check(SDL_Rect logical, SDL_Rect expected)
{
    SDL_Rect actual=Mini_PhysicalRect(logical);
    if(actual.x!=expected.x || actual.y!=expected.y || actual.w!=expected.w || actual.h!=expected.h){
        fprintf(stderr,"FAIL logical (%d,%d,%d,%d): physical (%d,%d,%d,%d), expected (%d,%d,%d,%d)\n",
            logical.x,logical.y,logical.w,logical.h,actual.x,actual.y,actual.w,actual.h,
            expected.x,expected.y,expected.w,expected.h);
        exit(1);
    }
}
int main(void)
{
    SDL_Rect physical,previous,current;
    int y;
    check((SDL_Rect){0,0,320,240},(SDL_Rect){0,0,640,480});
    check((SDL_Rect){20,20,180,140},(SDL_Rect){240,160,360,280});
    check((SDL_Rect){100,70,180,140},(SDL_Rect){80,60,360,280});
    /* Viewport (40,30) plus clip (15,15,160,120): screenshot y=90..329. */
    check((SDL_Rect){55,45,160,120},(SDL_Rect){210,150,320,240});
    physical=Mini_PhysicalRect((SDL_Rect){55,45,160,120});
    assert(FB_W-physical.x-physical.w==110);
    assert(FB_H-physical.y-physical.h==90);
    /* Texture placements include a logical destination clipped at x=0. */
    check((SDL_Rect){0,0,64,64},(SDL_Rect){512,352,128,128});
    check((SDL_Rect){180,80,120,120},(SDL_Rect){40,80,240,240});
    check((SDL_Rect){0,100,80,100},(SDL_Rect){480,80,160,200});
    check((SDL_Rect){0,0,1,1},(SDL_Rect){638,478,2,2});
    check((SDL_Rect){319,239,1,1},(SDL_Rect){0,0,2,2});
    window.w=window.h=320;
    check((SDL_Rect){0,0,320,320},(SDL_Rect){80,0,480,480});
    window.w=640;window.h=240;
    check((SDL_Rect){0,0,640,240},(SDL_Rect){0,120,640,240});
    /* Fractional scaling must leave no seams between adjacent logical rows. */
    window.w=333;window.h=251;
    previous=Mini_PhysicalRect((SDL_Rect){0,0,333,1});
    for(y=1;y<251;++y){
        current=Mini_PhysicalRect((SDL_Rect){0,y,333,1});
        assert(previous.y==current.y+current.h);
        assert(current.y>=0 && current.y+current.h<=FB_H);
        previous=current;
    }
    puts("PASS: 180-degree rectangle mapping, viewport screenshot coordinates, clipped textures, corners, letterboxing and fractional row seams");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    source = (ROOT / 'sdl2-master/sdl2/src/render/mini/SDL_render_mini.c').read_text()
    start = source.index('static SDL_Rect Mini_PhysicalRect(')
    end = source.index('\n}', start) + 2
    with tempfile.TemporaryDirectory(prefix='mini-rect-') as directory:
        test = Path(directory) / 'test.c'
        exe = Path(directory) / 'test.exe'
        test.write_text(HARNESS + source[start:end] + TEST)
        subprocess.run([args.cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-Werror=declaration-after-statement',
                        '-I' + str(ROOT / 'sdl2-master/sdl2/include'),
                        str(test), '-lm', '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
