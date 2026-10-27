// LGPL-2.1 License
// (C) 2025 Steward Fu <steward.fu@gmail.com>

#ifndef __SDL_VIDEO_Mini_H__
#define __SDL_VIDEO_Mini_H__

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"

#include <stdint.h>
#include <stdbool.h>
#include <linux/fb.h>

#include "mi_sys.h"
#include "mi_gfx.h"

#ifndef MAX_PATH
    #define MAX_PATH 128
int GFX_CopyTextureEx(const void *pixels, int width, int height, int pitch, Uint32 format, SDL_Rect srcrect, SDL_Rect dstrect, SDL_BlendMode blend, SDL_Color mod, SDL_RendererFlip flip);

#endif

#define DEF_FB_W 640
#define DEF_FB_H 480
#define FB_BPP   4

#if 0
    #define debug(...) printf(__VA_ARGS__)
#else
    #define debug(...) (void)0
int GFX_CopyTextureEx(const void *pixels, int width, int height, int pitch, Uint32 format, SDL_Rect srcrect, SDL_Rect dstrect, SDL_BlendMode blend, SDL_Color mod, SDL_RendererFlip flip);

#endif

typedef struct _GFX {
    int fb_dev;

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    struct _DMA {
        void *virAddr;
        MI_PHY phyAddr;
    } fb, tmp, overlay;

    struct _HW {
        struct _BUF {
            MI_GFX_Surface_t surf;
            MI_GFX_Rect_t rt;
        } src, dst, overlay;
        MI_GFX_Opt_t opt;
    } hw;
} GFX;

void GFX_Clear(void);
int GFX_FillRect(SDL_Rect rect, Uint32 argb);
void GFX_Flip(void);
int GFX_CopyTexture(const void *pixels, int width, int height, int pitch, Uint32 format, SDL_Rect srcrect, SDL_Rect dstrect, SDL_BlendMode blend, SDL_Color mod);
int GFX_Copy(const void *pixels, SDL_Rect srcrect, SDL_Rect dstrect, int pitch, int alpha, int rotate);

int GFX_CopyTextureEx(const void *pixels, int width, int height, int pitch, Uint32 format, SDL_Rect srcrect, SDL_Rect dstrect, SDL_BlendMode blend, SDL_Color mod, SDL_RendererFlip flip);

#endif

