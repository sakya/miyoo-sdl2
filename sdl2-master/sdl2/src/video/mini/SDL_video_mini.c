// LGPL-2.1 License
// (C) 2025 Steward Fu <steward.fu@gmail.com>

#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_MINI

#include <time.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>

#include "../../events/SDL_events_c.h"
#include "../SDL_sysvideo.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"

#include "SDL_version.h"
#include "SDL_syswm.h"
#include "SDL_loadso.h"
#include "SDL_events.h"
#include "SDL_video.h"
#include "SDL_mouse.h"
#include "SDL_video_mini.h"
#include "SDL_event_mini.h"
#include "SDL_gles_mini.h"
#include "SDL_fb_mini.h"

GFX gfx = { 0 };

int FB_W = 0;
int FB_H = 0;
int FB_SIZE = 0;
int TMP_SIZE = 0;
SDL_Window *vid_win = NULL;

static int Mini_VideoInit(_THIS);
static int Mini_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode);
static void Mini_VideoQuit(_THIS);

/* Resource ownership is tracked independently: zero physical addresses are not
   used as allocation-success sentinels. */
static SDL_bool mini_sys_ready = SDL_FALSE;
static SDL_bool mini_gfx_ready = SDL_FALSE;
static SDL_bool mini_tmp_allocated = SDL_FALSE;
static SDL_bool mini_overlay_allocated = SDL_FALSE;
static size_t mini_fb_map_len = 0;
static struct fb_var_screeninfo mini_original_vinfo;
static SDL_bool mini_original_vinfo_valid = SDL_FALSE;
static size_t mini_frame_stride = 0;

/* Only QuickFill commands are deferred. Texture blits continue to wait for
 * completion because they reuse one DMA staging allocation. A bounded batch
 * prevents an unbounded hardware queue. MI_GFX fences are ordered within the
 * same engine; waiting for the latest submitted fill completes earlier fills. */
#define MINI_FILL_BATCH_MAX 8
static MI_U16 mini_pending_fill_fence = 0;
static unsigned mini_pending_fill_count = 0;

static int Mini_WaitPendingFills(void)
{
    MI_S32 rc;
    if (!mini_pending_fill_count) return 0;
    rc = MI_GFX_WaitAllDone(TRUE, mini_pending_fill_fence);
    mini_pending_fill_count = 0;
    mini_pending_fill_fence = 0;
    if (rc != MI_SUCCESS)
        return SDL_SetError("MI_GFX fill batch wait failed: %d", rc);
    return 0;
}


static int Mini_QuitGFX(void)
{
    /* Drain queued GPU writes before unmapping framebuffer or closing GFX. */
    if (mini_gfx_ready) Mini_WaitPendingFills();
    if (gfx.overlay.virAddr) {
        MI_SYS_Munmap(gfx.overlay.virAddr, (MI_U32)TMP_SIZE);
        gfx.overlay.virAddr = NULL;
    }
    if (mini_overlay_allocated) {
        MI_SYS_MMA_Free(gfx.overlay.phyAddr);
        mini_overlay_allocated = SDL_FALSE;
    }
    gfx.overlay.phyAddr = 0;
    if (gfx.tmp.virAddr) {
        MI_SYS_Munmap(gfx.tmp.virAddr, (MI_U32)TMP_SIZE);
        gfx.tmp.virAddr = NULL;
    }
    if (mini_tmp_allocated) {
        MI_SYS_MMA_Free(gfx.tmp.phyAddr);
        mini_tmp_allocated = SDL_FALSE;
    }
    gfx.tmp.phyAddr = 0;
    if (gfx.fb.virAddr) {
        MI_SYS_Munmap(gfx.fb.virAddr, (MI_U32)mini_fb_map_len);
        gfx.fb.virAddr = NULL;
    }
    mini_fb_map_len = 0;
    if (gfx.fb_dev >= 0) {
        if (mini_original_vinfo_valid)
            ioctl(gfx.fb_dev, FBIOPUT_VSCREENINFO, &mini_original_vinfo);
        close(gfx.fb_dev);
        gfx.fb_dev = -1;
    }
    mini_original_vinfo_valid = SDL_FALSE;
    if (mini_gfx_ready) {
        MI_GFX_Close();
        mini_gfx_ready = SDL_FALSE;
    }
    if (mini_sys_ready) {
        MI_SYS_Exit();
        mini_sys_ready = SDL_FALSE;
    }
    mini_frame_stride = 0;
    return 0;
}

int Mini_InitGFX(void)
{
    MI_S32 rc;
    size_t required;
    gfx.fb_dev = -1;
    if (FB_W <= 0 || FB_H <= 0 || TMP_SIZE <= 0)
        return SDL_SetError("MINI invalid framebuffer dimensions");
    rc = MI_SYS_Init();
    if (rc != MI_SUCCESS) return SDL_SetError("MI_SYS_Init failed: %d", rc);
    mini_sys_ready = SDL_TRUE;
    rc = MI_GFX_Open();
    if (rc != MI_SUCCESS) {
        SDL_SetError("MI_GFX_Open failed: %d", rc);
        goto fail;
    }
    mini_gfx_ready = SDL_TRUE;
    gfx.fb_dev = open("/dev/fb0", O_RDWR);
    if (gfx.fb_dev < 0) {
        SDL_SetError("Cannot open /dev/fb0");
        goto fail;
    }
    if (ioctl(gfx.fb_dev, FBIOGET_FSCREENINFO, &gfx.finfo) < 0 ||
        ioctl(gfx.fb_dev, FBIOGET_VSCREENINFO, &gfx.vinfo) < 0) {
        SDL_SetError("Cannot query framebuffer geometry");
        goto fail;
    }
    mini_original_vinfo = gfx.vinfo;
    mini_original_vinfo_valid = SDL_TRUE;
    if (gfx.vinfo.bits_per_pixel != FB_BPP * 8 ||
        gfx.vinfo.xres < (unsigned)FB_W || gfx.vinfo.yres < (unsigned)FB_H ||
        gfx.finfo.line_length < (unsigned)FB_W * FB_BPP) {
        SDL_SetError("MINI framebuffer geometry or format unsupported");
        goto fail;
    }
    gfx.vinfo.yoffset = 0;
    gfx.vinfo.yres_virtual = gfx.vinfo.yres * 2;
    if (ioctl(gfx.fb_dev, FBIOPUT_VSCREENINFO, &gfx.vinfo) < 0 ||
        ioctl(gfx.fb_dev, FBIOGET_VSCREENINFO, &gfx.vinfo) < 0 ||
        ioctl(gfx.fb_dev, FBIOGET_FSCREENINFO, &gfx.finfo) < 0) {
        SDL_SetError("MINI double-buffer framebuffer setup failed");
        goto fail;
    }
    mini_frame_stride = (size_t)gfx.finfo.line_length;
    required = mini_frame_stride * (size_t)gfx.vinfo.yres_virtual;
    if (gfx.vinfo.yres_virtual < (unsigned)(FB_H * 2) ||
        required > gfx.finfo.smem_len || required > UINT32_MAX ||
        mini_frame_stride > UINT32_MAX) {
        SDL_SetError("MINI framebuffer memory insufficient for two frames");
        goto fail;
    }
    gfx.fb.phyAddr = gfx.finfo.smem_start;
    if ((size_t)gfx.finfo.smem_len > UINT32_MAX) {
        SDL_SetError("MINI framebuffer mapping exceeds MI_SYS limit");
        goto fail;
    }
    mini_fb_map_len = gfx.finfo.smem_len;
    rc = MI_SYS_Mmap(gfx.fb.phyAddr, (MI_U32)mini_fb_map_len,
                     &gfx.fb.virAddr, TRUE);
    if (rc != MI_SUCCESS || !gfx.fb.virAddr) {
        SDL_SetError("MI_SYS_Mmap framebuffer failed: %d", rc);
        goto fail;
    }
    rc = MI_SYS_MemsetPa(gfx.fb.phyAddr, 0, (MI_U32)required);
    if (rc != MI_SUCCESS) {
        SDL_SetError("MI_SYS_MemsetPa framebuffer failed: %d", rc);
        goto fail;
    }
    rc = MI_SYS_MMA_Alloc(NULL, (MI_U32)TMP_SIZE, &gfx.tmp.phyAddr);
    if (rc != MI_SUCCESS) {
        SDL_SetError("MINI staging allocation failed: %d", rc);
        goto fail;
    }
    mini_tmp_allocated = SDL_TRUE;
    rc = MI_SYS_Mmap(gfx.tmp.phyAddr, (MI_U32)TMP_SIZE, &gfx.tmp.virAddr, TRUE);
    if (rc != MI_SUCCESS || !gfx.tmp.virAddr) {
        SDL_SetError("MINI staging mapping failed: %d", rc);
        goto fail;
    }
    rc = MI_SYS_MMA_Alloc(NULL, (MI_U32)TMP_SIZE, &gfx.overlay.phyAddr);
    if (rc != MI_SUCCESS) {
        SDL_SetError("MINI overlay allocation failed: %d", rc);
        goto fail;
    }
    mini_overlay_allocated = SDL_TRUE;
    rc = MI_SYS_Mmap(gfx.overlay.phyAddr, (MI_U32)TMP_SIZE,
                     &gfx.overlay.virAddr, TRUE);
    if (rc != MI_SUCCESS || !gfx.overlay.virAddr) {
        SDL_SetError("MINI overlay mapping failed: %d", rc);
        goto fail;
    }
    SDL_zero(gfx.hw.opt);
    return 0;
fail:
    Mini_QuitGFX();
    return -1;
}

void GFX_Init(void)
{
    Mini_InitGFX(); /* Legacy entry point; VideoInit uses the return value directly. */
}

void GFX_Quit(void)
{
    if (mini_gfx_ready && gfx.fb.virAddr) GFX_Clear();
    Mini_QuitGFX();
}

void GFX_Clear(void)
{
    if (Mini_WaitPendingFills() < 0) return;
    if (mini_sys_ready && gfx.fb.virAddr && mini_frame_stride) {
        size_t bytes = mini_frame_stride * (size_t)gfx.vinfo.yres_virtual;
        if (bytes <= UINT32_MAX)
            MI_SYS_MemsetPa(gfx.fb.phyAddr, 0, (MI_U32)bytes);
    }
    if (mini_sys_ready && gfx.tmp.virAddr)
        MI_SYS_MemsetPa(gfx.tmp.phyAddr, 0, (MI_U32)TMP_SIZE);
}

static MI_PHY Mini_CurrentFBAddress(void)
{
    return gfx.fb.phyAddr + (MI_PHY)((size_t)gfx.vinfo.yoffset * mini_frame_stride);
}

/* Synchronous fill: no staging buffer is needed. */
int GFX_FillRect(SDL_Rect rect, Uint32 argb)
{
    MI_GFX_Surface_t surface;
    MI_GFX_Rect_t region;
    MI_U16 fence = 0;
    MI_S32 result;
    if (rect.x < 0 || rect.y < 0 || rect.w <= 0 || rect.h <= 0 ||
        rect.x > FB_W || rect.w > FB_W - rect.x ||
        rect.y > FB_H || rect.h > FB_H - rect.y)
        return SDL_SetError("Invalid MINI fill rectangle");
    SDL_zero(surface);
    SDL_zero(region);
    surface.phyAddr = Mini_CurrentFBAddress();
    surface.eColorFmt = E_MI_GFX_FMT_ARGB8888;
    surface.u32Width = FB_W;
    surface.u32Height = FB_H;
    surface.u32Stride = (MI_U32)mini_frame_stride;
    region.s32Xpos = rect.x;
    region.s32Ypos = rect.y;
    region.u32Width = rect.w;
    region.u32Height = rect.h;
    result = MI_GFX_QuickFill(&surface, &region, argb, &fence);
    if (result != MI_SUCCESS) return SDL_SetError("MI_GFX_QuickFill failed: %d", result);
    mini_pending_fill_fence = fence;
    ++mini_pending_fill_count;
    if (mini_pending_fill_count >= MINI_FILL_BATCH_MAX)
        return Mini_WaitPendingFills();
    return 0;
}

/* Bounded, explicit-format texture upload. Keep the legacy GFX_Copy for GFX_CB. */
/* Build a fresh options structure for every submission.  Never share mutable
   blend state between the SDL renderer and the legacy GLES callback. */
static void Mini_SetBlitOptions(MI_GFX_Opt_t *opt, SDL_BlendMode blend,
                                MI_GFX_Rotate_e rotate)
{
    SDL_zero(*opt);
    opt->eRotate = rotate;
    opt->eMirror = E_MI_GFX_MIRROR_NONE;
    opt->eSrcDfbBldOp = (blend == SDL_BLENDMODE_BLEND)
        ? E_MI_GFX_DFB_BLD_SRCALPHA : E_MI_GFX_DFB_BLD_ONE;
    opt->eDstDfbBldOp = (blend == SDL_BLENDMODE_BLEND)
        ? E_MI_GFX_DFB_BLD_INVSRCALPHA : E_MI_GFX_DFB_BLD_ZERO;
    opt->eDFBBlendFlag = (blend == SDL_BLENDMODE_BLEND)
        ? E_MI_GFX_DFB_BLEND_ALPHACHANNEL : E_MI_GFX_DFB_BLEND_NOFX;
}

int GFX_CopyTextureEx(const void *pixels, int width, int height, int pitch,
                    Uint32 format, SDL_Rect src, SDL_Rect dst, SDL_BlendMode blend, SDL_Color mod, SDL_RendererFlip flip)
{
    MI_U16 fence = 0;
    MI_GFX_ColorFmt_e hwfmt;
    int bpp, row;
    size_t bytes;
    MI_S32 result;
    if (!pixels || width <= 0 || height <= 0 || pitch <= 0 ||
        src.x < 0 || src.y < 0 || src.w <= 0 || src.h <= 0 ||
        src.x > width || src.w > width - src.x ||
        src.y > height || src.h > height - src.y ||
        dst.w <= 0 || dst.h <= 0 ||
        dst.x < 0 || dst.y < 0 || dst.x > FB_W || dst.w > FB_W - dst.x ||
        dst.y > FB_H || dst.h > FB_H - dst.y)
        return SDL_SetError("Invalid MINI blit rectangle");
    if (format == SDL_PIXELFORMAT_RGB565) {
        bpp = 2; hwfmt = E_MI_GFX_FMT_RGB565;
    } else if (format == SDL_PIXELFORMAT_ARGB8888) {
        bpp = 4; hwfmt = E_MI_GFX_FMT_ARGB8888;
    } else return SDL_SetError("Unsupported MINI texture pixel format");
    if (!gfx.tmp.virAddr || !gfx.tmp.phyAddr || TMP_SIZE <= 0 ||
        width > pitch / bpp || src.h > TMP_SIZE / bpp / src.w) {
        return SDL_SetError("MINI texture exceeds staging buffer");
    }
    if (blend != SDL_BLENDMODE_NONE && blend != SDL_BLENDMODE_BLEND)
        return SDL_Unsupported();
    if (blend == SDL_BLENDMODE_BLEND && format != SDL_PIXELFORMAT_ARGB8888)
        return SDL_Unsupported();
    if (flip & ~(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL))
        return SDL_Unsupported();
    bytes = (size_t)src.w * (size_t)bpp * (size_t)src.h;
    for (row = 0; row < src.h; ++row) {
        const int source_y = src.y + ((flip & SDL_FLIP_VERTICAL) ? (src.h - 1 - row) : row);
        const Uint8 *source = (const Uint8 *)pixels + (size_t)source_y * pitch + (size_t)src.x * bpp;
        Uint8 *dest = (Uint8 *)gfx.tmp.virAddr + (size_t)row * src.w * bpp;
        if (flip & SDL_FLIP_HORIZONTAL) {
            int col;
            for (col = 0; col < src.w; ++col)
                SDL_memcpy(dest + (size_t)col * bpp,
                           source + (size_t)(src.w - 1 - col) * bpp, (size_t)bpp);
        } else {
            SDL_memcpy(dest, source, (size_t)src.w * bpp);
        }
    }
    /* SDL modulation uses straight alpha and multiplies each channel by
     * its modulation value / 255. Modify staging memory only: queued
     * draws must never alter the texture's owned pixel storage. */
    if (mod.r != 255 || mod.g != 255 || mod.b != 255 || mod.a != 255) {
        size_t i, count = (size_t)src.w * (size_t)src.h;
        if (format == SDL_PIXELFORMAT_ARGB8888) {
            Uint32 *p = (Uint32 *)gfx.tmp.virAddr;
            for (i = 0; i < count; ++i) {
                Uint32 c = p[i];
                Uint32 a = ((c >> 24) & 255) * mod.a / 255;
                Uint32 r = ((c >> 16) & 255) * mod.r / 255;
                Uint32 g = ((c >> 8) & 255) * mod.g / 255;
                Uint32 b = (c & 255) * mod.b / 255;
                p[i] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        } else {
            Uint16 *p = (Uint16 *)gfx.tmp.virAddr;
            for (i = 0; i < count; ++i) {
                Uint16 c = p[i];
                /* Expand 5/6-bit channels before applying SDL's 8-bit mod. */
                Uint32 r = ((c >> 11) & 31) * 255 / 31;
                Uint32 g = ((c >> 5) & 63) * 255 / 63;
                Uint32 b = (c & 31) * 255 / 31;
                r = (r * mod.r / 255) * 31 / 255;
                g = (g * mod.g / 255) * 63 / 255;
                b = (b * mod.b / 255) * 31 / 255;
                p[i] = (Uint16)((r << 11) | (g << 5) | b);
            }
            /* RGB565 has no alpha channel; NONE ignores alpha modulation.
             * BLEND on RGB565 is rejected above. */
        }
    }
    /* Do not let a preceding blit's blend state leak into this command.
       The MI_GFX flags are configured explicitly for each submission.
       SDL_BLENDMODE_BLEND assumes straight (non-premultiplied) alpha.
       Hardware output-alpha semantics still require device validation. */
    Mini_SetBlitOptions(&gfx.hw.opt, blend, E_MI_GFX_ROTATE_180);
    gfx.hw.src.rt.s32Xpos = 0;
    gfx.hw.src.rt.s32Ypos = 0;
    gfx.hw.src.rt.u32Width = src.w;
    gfx.hw.src.rt.u32Height = src.h;
    gfx.hw.src.surf.u32Width = src.w;
    gfx.hw.src.surf.u32Height = src.h;
    gfx.hw.src.surf.u32Stride = src.w * bpp;
    gfx.hw.src.surf.eColorFmt = hwfmt;
    gfx.hw.src.surf.phyAddr = gfx.tmp.phyAddr;
    gfx.hw.dst.rt.s32Xpos = dst.x;
    gfx.hw.dst.rt.s32Ypos = dst.y;
    gfx.hw.dst.rt.u32Width = dst.w;
    gfx.hw.dst.rt.u32Height = dst.h;
    gfx.hw.dst.surf.u32Width = FB_W;
    gfx.hw.dst.surf.u32Height = FB_H;
    gfx.hw.dst.surf.u32Stride = (MI_U32)mini_frame_stride;
    gfx.hw.dst.surf.eColorFmt = E_MI_GFX_FMT_ARGB8888;
    gfx.hw.dst.surf.phyAddr = Mini_CurrentFBAddress();
    if (Mini_WaitPendingFills() < 0) return -1;
    result = MI_SYS_FlushInvCache(gfx.tmp.virAddr, bytes);
    if (result != MI_SUCCESS) return SDL_SetError("MINI staging cache flush failed: %d", result);
    result = MI_GFX_BitBlit(&gfx.hw.src.surf, &gfx.hw.src.rt,
                            &gfx.hw.dst.surf, &gfx.hw.dst.rt, &gfx.hw.opt, &fence);
    if (result != MI_SUCCESS) return SDL_SetError("MI_GFX_BitBlit failed: %d", result);
    result = MI_GFX_WaitAllDone(TRUE, fence);
    if (result != MI_SUCCESS) return SDL_SetError("MI_GFX_WaitAllDone failed: %d", result);
    return 0;
}

int GFX_CopyTexture(const void *pixels, int width, int height, int pitch,
                    Uint32 format, SDL_Rect src, SDL_Rect dst, SDL_BlendMode blend, SDL_Color mod)
{
    return GFX_CopyTextureEx(pixels, width, height, pitch, format, src, dst, blend, mod, SDL_FLIP_NONE);
}

int GFX_Copy(const void *pixels, SDL_Rect srt, SDL_Rect drt, int pitch, int alpha, int rotate)
{
    MI_U16 u16Fence = 0;
    int rgb565 = (pitch / srt.w) == 2 ? 1 : 0;

    debug("%s, pixels=%p, rgb565=%d\n", __func__, pixels, rgb565);
    /* The GLES callback may pass gfx.tmp.virAddr as source.
       memcpy with identical/overlapping ranges has undefined behavior. */
    if (!pixels || !gfx.tmp.virAddr || pitch <= 0 || srt.h <= 0 ||
        (size_t)srt.h > (size_t)TMP_SIZE / (size_t)pitch)
        return SDL_SetError("Invalid MINI legacy copy buffer");
    if (pixels != gfx.tmp.virAddr)
        SDL_memmove(gfx.tmp.virAddr, pixels, (size_t)srt.h * (size_t)pitch);

    Mini_SetBlitOptions(&gfx.hw.opt, SDL_BLENDMODE_NONE,
                        (MI_GFX_Rotate_e)rotate);
    gfx.hw.src.rt.s32Xpos = srt.x;
    gfx.hw.src.rt.s32Ypos = srt.y;
    gfx.hw.src.rt.u32Width = srt.w;
    gfx.hw.src.rt.u32Height = srt.h;
    gfx.hw.src.surf.u32Width = srt.w;
    gfx.hw.src.surf.u32Height = srt.h;
    gfx.hw.src.surf.u32Stride = pitch;
    gfx.hw.src.surf.eColorFmt = rgb565 ? E_MI_GFX_FMT_RGB565 : E_MI_GFX_FMT_ARGB8888;
    gfx.hw.src.surf.phyAddr = gfx.tmp.phyAddr;

    gfx.hw.dst.rt.s32Xpos = drt.x;
    gfx.hw.dst.rt.s32Ypos = drt.y;
    gfx.hw.dst.rt.u32Width = drt.w;
    gfx.hw.dst.rt.u32Height = drt.h;
    gfx.hw.dst.surf.u32Width = FB_W;
    gfx.hw.dst.surf.u32Height = FB_H;
    gfx.hw.dst.surf.u32Stride = (MI_U32)mini_frame_stride;
    gfx.hw.dst.surf.eColorFmt = E_MI_GFX_FMT_ARGB8888;
    gfx.hw.dst.surf.phyAddr = Mini_CurrentFBAddress();

    if (Mini_WaitPendingFills() < 0) return -1;
    if (MI_SYS_FlushInvCache(gfx.tmp.virAddr, (size_t)pitch * (size_t)srt.h) != MI_SUCCESS)
        return SDL_SetError("MINI legacy cache flush failed");
    if (MI_GFX_BitBlit(&gfx.hw.src.surf, &gfx.hw.src.rt, &gfx.hw.dst.surf, &gfx.hw.dst.rt, &gfx.hw.opt, &u16Fence) != MI_SUCCESS)
        return SDL_SetError("MINI legacy blit failed");
    if (MI_GFX_WaitAllDone(TRUE, u16Fence) != MI_SUCCESS)
        return SDL_SetError("MINI legacy blit wait failed");
    return 0;
}

void GFX_Flip(void)
{
    if (Mini_WaitPendingFills() < 0) return;
    debug("%s\n", __func__);
    if (gfx.fb_dev >= 0 && ioctl(gfx.fb_dev, FBIOPAN_DISPLAY, &gfx.vinfo) == 0)
        gfx.vinfo.yoffset = (gfx.vinfo.yoffset == 0) ? gfx.vinfo.yres : 0;
}

void* GFX_CB(void)
{
    SDL_Rect srt = { 0, 0, vid_win->w, vid_win->h };
    SDL_Rect drt = { 0, 0, FB_W, FB_H };

    debug("%s src(%d,%d,%d,%d) dst(%d,%d,%d,%d)\n", __func__, srt.x, srt.y, srt.w, srt.h, drt.x, drt.y, drt.w, drt.h);
    if (GFX_Copy(gfx.tmp.virAddr, srt, drt, srt.w * FB_BPP, 0, E_MI_GFX_ROTATE_180) == 0)
        GFX_Flip();
    return gfx.tmp.virAddr;
}

static void Mini_DeleteDevice(SDL_VideoDevice *device)
{
    debug("%s\n", __func__);
    SDL_free(device);
}

void Mini_DestroyWindow(_THIS, SDL_Window *window)
{
    debug("%s\n", __func__);
    GFX_Quit();
}

int Mini_CreateWindow(_THIS, SDL_Window *window)
{
    vid_win = window;
    SDL_SetMouseFocus(window);
    glUpdateBufferSettings(GFX_CB);
    debug("%s, win=%p, w=%d, h=%d\n", __func__, window, window->w, window->h);
    return 0;
}

int Mini_CreateWindowFrom(_THIS, SDL_Window *window, const void *data)
{
    debug("%s\n", __func__);
    return SDL_Unsupported();
}

static SDL_VideoDevice *Mini_CreateDevice(int devindex)
{
    SDL_VideoDevice *device = NULL;
    SDL_GLDriverData *gldata = NULL;

    debug("%s\n", __func__);

    device = (SDL_VideoDevice *) SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        SDL_OutOfMemory();
        return NULL;
    }

    device->is_dummy = SDL_TRUE;
    device->VideoInit = Mini_VideoInit;
    device->VideoQuit = Mini_VideoQuit;
    device->SetDisplayMode = Mini_SetDisplayMode;
    device->PumpEvents = Mini_PumpEvents;
    device->CreateSDLWindow = Mini_CreateWindow;
    device->CreateSDLWindowFrom = Mini_CreateWindowFrom;
    device->CreateWindowFramebuffer = Mini_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = Mini_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = Mini_DestroyWindowFramebuffer;
    device->DestroyWindow = Mini_DestroyWindow;

    device->GL_LoadLibrary = glLoadLibrary;
    device->GL_GetProcAddress = glGetProcAddress;
    device->GL_CreateContext = glCreateContext;
    device->GL_SetSwapInterval = glSetSwapInterval;
    device->GL_SwapWindow = glSwapWindow;
    device->GL_MakeCurrent = glMakeCurrent;
    device->GL_DeleteContext = glDeleteContext;
    device->GL_UnloadLibrary = glUnloadLibrary;
    
    gldata = (SDL_GLDriverData*)SDL_calloc(1, sizeof(SDL_GLDriverData));
    if (gldata == NULL) {
        SDL_OutOfMemory();
        SDL_free(device);
        return NULL;
    }

    device->gl_data = gldata;
    device->free = Mini_DeleteDevice;
    return device;
}

VideoBootStrap Mini_VideoDriver = { "Mini", "Miyoo Mini Video Driver", Mini_CreateDevice };

int Mini_VideoInit(_THIS)
{
    FILE *fd = NULL;
    char buf[MAX_PATH] = {0};
    SDL_DisplayMode mode = {0};
    SDL_VideoDisplay display = {0};

    debug("%s\n", __func__);

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB565;
    mode.w = 640;
    mode.h = 480;
    mode.refresh_rate = 60;
    SDL_AddDisplayMode(&display, &mode);

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_ARGB8888;
    mode.w = 640;
    mode.h = 480;
    mode.refresh_rate = 60;
    SDL_AddDisplayMode(&display, &mode);

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB565;
    mode.w = 800;
    mode.h = 480;
    mode.refresh_rate = 60;
    SDL_AddDisplayMode(&display, &mode);

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_ARGB8888;
    mode.w = 800;
    mode.h = 480;
    mode.refresh_rate = 60;
    SDL_AddDisplayMode(&display, &mode);
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB565;
    mode.w = 800;
    mode.h = 600;
    mode.refresh_rate = 60;
    SDL_AddDisplayMode(&display, &mode);

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_ARGB8888;
    mode.w = 800;
    mode.h = 600;
    mode.refresh_rate = 60;
    SDL_AddDisplayMode(&display, &mode);

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB565;
    mode.w = 320;
    mode.h = 240;
    mode.refresh_rate = 60;
    SDL_AddDisplayMode(&display, &mode);

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_ARGB8888;
    mode.w = 320;
    mode.h = 240;
    mode.refresh_rate = 60;
    SDL_AddDisplayMode(&display, &mode);

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB565;
    mode.w = 480;
    mode.h = 272;
    mode.refresh_rate = 60;
    SDL_AddDisplayMode(&display, &mode);

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_ARGB8888;
    mode.w = 480;
    mode.h = 272;
    mode.refresh_rate = 60;
    SDL_AddDisplayMode(&display, &mode);
    SDL_AddVideoDisplay(&display, SDL_FALSE);

    FB_W = DEF_FB_W;
    FB_H = DEF_FB_H;
    FB_SIZE = (FB_W * FB_H * FB_BPP * 2);
    TMP_SIZE = (FB_W * FB_H * FB_BPP);
    fd = popen("fbset | grep \"mode \"", "r");
    if (fd) {
        fgets(buf, sizeof(buf), fd);
        pclose(fd);

        if (strstr(buf, "752")) {
            FB_W = 752;
            FB_H = 560;
            FB_SIZE = (FB_W * FB_H * FB_BPP * 2);
            TMP_SIZE = (FB_W * FB_H * FB_BPP);
        }
    }

    debug("%s, screen=%dx%d\n", __func__, FB_W, FB_H);
    if (Mini_InitGFX() < 0) return -1;
    Mini_EventInit();
    return 0;
}

static int Mini_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode)
{
    debug("%s\n", __func__);
    return 0;
}

void Mini_VideoQuit(_THIS)
{
    debug("%s\n", __func__);
    Mini_EventQuit();
}

#endif

