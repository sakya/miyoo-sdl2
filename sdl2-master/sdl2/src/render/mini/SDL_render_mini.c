// LGPL-2.1 License
// (C) 2025 Steward Fu <steward.fu@gmail.com>

#include "../../SDL_internal.h"

#if SDL_VIDEO_RENDER_MINI

#include <unistd.h>
#include <stdbool.h>

#include "SDL_hints.h"
#include "../SDL_sysrender.h"
#include "../../video/mini/SDL_video_mini.h"

typedef struct Mini_TextureData {
    void *data;
    uint32_t w;
    uint32_t h;
    uint32_t fmt;
    uint32_t pitch;
    MI_PHY physical;
    SDL_bool dma;
    SDL_bool dirty;
} Mini_TextureData;

typedef struct {
    SDL_bool is_init;
} Mini_RenderData;

extern int FB_W;
extern int FB_H;
extern SDL_Window *vid_win;

/* Pixel storage is owned by each SDL_Texture, never by the caller. */

static void Mini_WindowEvent(SDL_Renderer *renderer, const SDL_WindowEvent *event)
{
    debug("%s\n", __func__);
}

static int Mini_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    Mini_TextureData *t = (Mini_TextureData *)SDL_calloc(1, sizeof(Mini_TextureData));

    debug("%s, texture=%p\n", __func__, texture);
    if (!t) {
        debug("%s, failed to create texture\n", __func__);
        return SDL_OutOfMemory();
    }

    /* GFX_CopyTexture supports only these exact hardware formats. */
    if (texture->format != SDL_PIXELFORMAT_RGB565 &&
        texture->format != SDL_PIXELFORMAT_ARGB8888) {
        SDL_free(t);
        return SDL_SetError("Unsupported MINI texture format");
    }
    if (texture->w <= 0 || texture->h <= 0 ||
        texture->w > 640 || texture->h > 480) {
        SDL_free(t);
        return SDL_SetError("Invalid MINI texture dimensions");
    }
    t->w = texture->w;
    t->h = texture->h;
    t->fmt = texture->format;
    t->pitch = t->w * SDL_BYTESPERPIXEL(t->fmt);
    if ((size_t)t->h > SIZE_MAX / (size_t)t->pitch) {
        SDL_free(t);
        return SDL_OutOfMemory();
    }
    /* Prefer texture-owned DMA storage. Keep the heap/staging path available
     * when the device's contiguous memory pool is exhausted. */
    if (MI_SYS_MMA_Alloc(NULL, (MI_U32)(t->h * t->pitch), &t->physical) == MI_SUCCESS) {
        if (MI_SYS_Mmap(t->physical, (MI_U32)(t->h * t->pitch), &t->data, TRUE) == MI_SUCCESS && t->data) {
            t->dma = SDL_TRUE;
            SDL_memset(t->data, 0, (size_t)t->h * t->pitch);
        } else {
            if (t->data) MI_SYS_Munmap(t->data, (MI_U32)(t->h * t->pitch));
            MI_SYS_MMA_Free(t->physical);
            t->data = NULL;
        }
    }
    if (!t->dma) t->data = SDL_calloc((size_t)t->h, (size_t)t->pitch);
    t->dirty = SDL_TRUE;
    if (!t->data) {
        debug("%s, failed to create texture data\n", __func__);
        SDL_free(t);
        return SDL_OutOfMemory();
    }

    texture->driverdata = t;
    return 0;
}

static int Mini_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *rect, void **pixels, int *pitch)
{
    Mini_TextureData *t = (Mini_TextureData *)texture->driverdata;

    debug("%s\n", __func__);
    if (!t || !rect || !pixels || !pitch || !t->data ||
        rect->x < 0 || rect->y < 0 || rect->w <= 0 || rect->h <= 0 ||
        rect->x > (int)t->w || rect->w > (int)t->w - rect->x ||
        rect->y > (int)t->h || rect->h > (int)t->h - rect->y) {
        return SDL_SetError("Invalid MINI texture lock rectangle");
    }
    if (t->dma && GFX_WaitPending() < 0) return -1;
    GFX_InvalidateTexture(t->data);
    *pixels = (Uint8 *)t->data + rect->y * t->pitch +
              rect->x * SDL_BYTESPERPIXEL(t->fmt);
    *pitch = t->pitch;
    return 0;
}

static int Mini_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch)
{
    Mini_TextureData *t = (Mini_TextureData *)texture->driverdata;
    const int bpp = t ? SDL_BYTESPERPIXEL(t->fmt) : 0;
    int y;
    if (!t || !t->data || !rect || !pixels || bpp <= 0 ||
        rect->x < 0 || rect->y < 0 || rect->w <= 0 || rect->h <= 0 ||
        rect->x > (int)t->w || rect->w > (int)t->w - rect->x ||
        rect->y > (int)t->h || rect->h > (int)t->h - rect->y ||
        pitch < rect->w * bpp) {
        return SDL_SetError("Invalid MINI texture update");
    }
    if (t->dma && GFX_WaitPending() < 0) return -1;
    GFX_InvalidateTexture(t->data);
    for (y = 0; y < rect->h; ++y) {
        SDL_memcpy((Uint8 *)t->data + (rect->y + y) * t->pitch + rect->x * bpp,
                   (const Uint8 *)pixels + y * pitch, rect->w * bpp);
    }
    t->dirty = SDL_TRUE;
    return 0;
}

static void Mini_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    Mini_TextureData *t = (Mini_TextureData *)texture->driverdata;
    debug("%s\n", __func__);
    /* Lock writes directly to texture-owned storage. */
    if (t) {
        GFX_InvalidateTexture(t->data);
        t->dirty = SDL_TRUE;
    }
}

static void Mini_SetTextureScaleMode(SDL_Renderer *renderer, SDL_Texture *texture, SDL_ScaleMode scaleMode)
{
    debug("%s\n", __func__);
}

static int Mini_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    debug("%s\n", __func__);
    return SDL_Unsupported();
}

static int Mini_QueueSetViewport(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    debug("%s\n", __func__);
    return 0;
}

/* Queue a private copy: SDL may release the caller's point array immediately. */
static int Mini_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FPoint *points, int count)
{
    size_t offset;
    SDL_FPoint *out;
    if (count < 0 || (count && !points) || (size_t)count > SIZE_MAX / sizeof(*out))
        return SDL_SetError("Invalid MINI point count");
    if (!count) { cmd->data.draw.count = 0; return 0; }
    out = (SDL_FPoint *)SDL_AllocateRenderVertices(renderer, (size_t)count * sizeof(*out),
                                                  sizeof(void *), &offset);
    if (!out) return SDL_OutOfMemory();
    SDL_memcpy(out, points, (size_t)count * sizeof(*out));
    cmd->data.draw.first = offset;
    cmd->data.draw.count = count;
    return 0;
}

static int Mini_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
    const float *xy, int xy_stride, const SDL_Color *color, int color_stride, const float *uv, int uv_stride,
    int num_vertices, const void *indices, int num_indices, int size_indices,
    float scale_x, float scale_y)
{
    debug("%s\n", __func__);
    return SDL_Unsupported();
}

static int Mini_QueueFillRects(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FRect *rects, int count)
{
    size_t offset;
    SDL_FRect *out;
    if (count < 0 || (count && !rects) || (size_t)count > SIZE_MAX / sizeof(*out))
        return SDL_SetError("Invalid MINI fill rectangles");
    if (!count) { cmd->data.draw.count = 0; return 0; }
    out = (SDL_FRect *)SDL_AllocateRenderVertices(renderer, (size_t)count * sizeof(*out), sizeof(void *), &offset);
    if (!out) return SDL_OutOfMemory();
    SDL_memcpy(out, rects, (size_t)count * sizeof(*out));
    cmd->data.draw.first = offset;
    cmd->data.draw.count = count;
    return 0;
}

typedef struct Mini_RectCommand {
    SDL_FRect rect;
} Mini_RectCommand;

typedef struct Mini_CopyCommand {
    SDL_Rect src;
    SDL_Rect dst;
    SDL_BlendMode blend;
    SDL_Color color;
    SDL_RendererFlip flip;
} Mini_CopyCommand;

static int Mini_QueueCopy(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture, const SDL_Rect *srcrect, const SDL_FRect *dstrect)
{
    Mini_CopyCommand *copy;
    size_t offset;
    /* Reject unsupported blend modes; modulation is applied to staging pixels. */
    if (texture && ((texture->blendMode != SDL_BLENDMODE_NONE &&
         texture->blendMode != SDL_BLENDMODE_BLEND) ||
        (texture->blendMode == SDL_BLENDMODE_BLEND &&
         texture->format != SDL_PIXELFORMAT_ARGB8888)))
        return SDL_Unsupported();
    if (!texture || !texture->driverdata || !srcrect || !dstrect ||
        !vid_win || vid_win->w <= 0 || vid_win->h <= 0 ||
        FB_W <= 0 || FB_H <= 0) {
        return SDL_SetError("Invalid MINI copy state");
    }
    if (!(dstrect->x == dstrect->x) || !(dstrect->y == dstrect->y) ||
        !(dstrect->w == dstrect->w) || !(dstrect->h == dstrect->h) ||
        dstrect->x < -1000000.0f || dstrect->y < -1000000.0f ||
        dstrect->x > 1000000.0f || dstrect->y > 1000000.0f ||
        dstrect->w > 1000000.0f || dstrect->h > 1000000.0f)
        return SDL_SetError("Invalid MINI floating-point destination");
    if (srcrect->x < 0 || srcrect->y < 0 ||
        srcrect->x > texture->w || srcrect->y > texture->h ||
        srcrect->w > texture->w - srcrect->x ||
        srcrect->h > texture->h - srcrect->y)
        return SDL_SetError("MINI copy source outside texture");
    if (srcrect->w <= 0 || srcrect->h <= 0 || dstrect->w <= 0 || dstrect->h <= 0) {
        return SDL_SetError("Invalid MINI copy rectangle");
    }
    copy = (Mini_CopyCommand *)SDL_AllocateRenderVertices(renderer, sizeof(*copy),
                                                        sizeof(void *), &offset);
    if (!copy) return SDL_OutOfMemory();
    copy->flip = SDL_FLIP_NONE;
    copy->blend = texture->blendMode;
    copy->color = texture->color;
    copy->src = *srcrect;
    copy->dst.x = (int)dstrect->x;
    copy->dst.y = (int)dstrect->y;
    copy->dst.w = (int)dstrect->w;
    copy->dst.h = (int)dstrect->h;
    if (copy->dst.w <= 0 || copy->dst.h <= 0) return SDL_SetError("MINI destination too small");
    cmd->data.draw.first = offset;
    cmd->data.draw.count = 1;
    cmd->data.draw.texture = texture;
    return 0;
}

/* MI_GFX exposes no general arbitrary-angle texture rotation here.
 * Support zero-angle SDL flips via the DMA staging buffer; fail explicitly
 * for nonzero angles rather than silently rendering the wrong image. */
static int Mini_QueueCopyEx(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
    const SDL_Rect *srcrect, const SDL_FRect *dstrect, const double angle, const SDL_FPoint *center, const SDL_RendererFlip flip)
{
    int result;
    Mini_CopyCommand *copy;
    if (!(angle == 0.0) || (flip & ~(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL)))
        return SDL_Unsupported();
    result = Mini_QueueCopy(renderer, cmd, texture, srcrect, dstrect);
    if (result < 0) return result;
    copy = (Mini_CopyCommand *)((Uint8 *)renderer->vertex_data + cmd->data.draw.first);
    copy->flip = flip;
    (void)center; /* A zero-angle rotation has no pivot dependence. */
    return 0;
}

/* Map logical rectangle edges independently. This prevents gaps between
 * adjacent rectangles when the window-to-framebuffer scale is fractional.
 * The framebuffer is mounted 180 degrees relative to SDL's X axis. */
static SDL_Rect Mini_PhysicalRect(SDL_Rect dst)
{
    const double sx = (double)FB_W / (double)vid_win->w;
    const double sy = (double)FB_H / (double)vid_win->h;
    const double scale = sx < sy ? sx : sy;
    const double ox = ((double)FB_W - (double)vid_win->w * scale) * 0.5;
    const double oy = ((double)FB_H - (double)vid_win->h * scale) * 0.5;
    const double x0 = ox + ((double)vid_win->w - ((double)dst.x + dst.w)) * scale;
    const double x1 = ox + ((double)vid_win->w - (double)dst.x) * scale;
    const double y0 = oy + (double)dst.y * scale;
    const double y1 = oy + ((double)dst.y + dst.h) * scale;
    SDL_Rect p;
    p.x = (int)SDL_floor(x0);
    p.y = (int)SDL_floor(y0);
    p.w = (int)SDL_floor(x1) - p.x;
    p.h = (int)SDL_floor(y1) - p.y;
    return p;
}

/* Map the destination clipping back to source with 64-bit arithmetic.
 * Using floor for the leading edge and ceil for the trailing edge preserves
 * the entire covered source interval. */
static SDL_bool Mini_ClipCopy(SDL_Rect *src, SDL_Rect *dst, const SDL_Rect *bounds)
{
    SDL_Rect clipped, original = *dst, original_src = *src;
    Sint64 left, top, right, bottom;
    if (original.w <= 0 || original.h <= 0 ||
        original_src.w <= 0 || original_src.h <= 0 ||
        !SDL_IntersectRect(&original, bounds, &clipped)) return SDL_FALSE;
    left = (Sint64)(clipped.x - original.x) * original_src.w / original.w;
    top = (Sint64)(clipped.y - original.y) * original_src.h / original.h;
    right = ((Sint64)(clipped.x - original.x + clipped.w) * original_src.w + original.w - 1) / original.w;
    bottom = ((Sint64)(clipped.y - original.y + clipped.h) * original_src.h + original.h - 1) / original.h;
    src->x = original_src.x + (int)left;
    src->y = original_src.y + (int)top;
    src->w = (int)(right - left);
    src->h = (int)(bottom - top);
    *dst = clipped;
    return src->w > 0 && src->h > 0;
}

/* Uniform-color primitives: opaque fills use QuickFill; translucent fills
 * use the same ARGB8888 source-over path as texture copies.  This is a
 * correctness-first synchronous implementation, not a batching optimization.
 * Alpha zero under BLEND is a no-op; under NONE it must still overwrite. */
static int Mini_FillPrimitive(SDL_Rect physical, Uint32 color, SDL_BlendMode blend)
{
    SDL_Rect source = {0, 0, 1, 1};
    Uint32 pixel = color;
    SDL_Rect screen = {0, 0, FB_W, FB_H}, clipped;
    SDL_Color mod = {255, 255, 255, 255};
    if (physical.w <= 0 || physical.h <= 0 ||
        !SDL_IntersectRect(&screen, &physical, &clipped)) return 0;
    physical = clipped;
    if (blend == SDL_BLENDMODE_NONE ||
        (blend == SDL_BLENDMODE_BLEND && (color >> 24) == 255))
        return GFX_FillRect(physical, color);
    if (blend != SDL_BLENDMODE_BLEND)
        return SDL_SetError("Unsupported MINI primitive blend mode");
    if ((color >> 24) == 0) return 0;
    return GFX_CopyTexture(&pixel, 1, 1, sizeof(pixel),
                           SDL_PIXELFORMAT_ARGB8888, source, physical,
                           SDL_BLENDMODE_BLEND, mod);
}

/* Draw clipped 1-pixel SDL primitives with the existing synchronous fill path.
 * The rectangle bounds are logical coordinates, before display rotation. */
static int Mini_DrawPixel(int x, int y, const SDL_Rect *bounds, Uint32 color, SDL_BlendMode blend)
{
    SDL_Rect logical, physical;
    if (x < bounds->x || y < bounds->y ||
        (Sint64)x >= (Sint64)bounds->x + bounds->w ||
        (Sint64)y >= (Sint64)bounds->y + bounds->h)
        return 0;
    logical.x = x; logical.y = y; logical.w = 1; logical.h = 1;
    physical = Mini_PhysicalRect(logical);
    if (physical.w <= 0 || physical.h <= 0) return 0;
    return Mini_FillPrimitive(physical, color, blend);
}

/* Bound iteration to the visible region by clipping the segment first.
 * SDL_IntersectRectAndLine uses inclusive line endpoints. */
static int Mini_DrawLine(int x0, int y0, int x1, int y1,
                         const SDL_Rect *bounds, Uint32 color, SDL_BlendMode blend)
{
    Sint64 dx, dy, err, step_x, step_y, e2;
    if (!SDL_IntersectRectAndLine(bounds, &x0, &y0, &x1, &y1)) return 0;
    /* Axis-aligned segments need one hardware fill, not one per pixel.
     * Clip first, then transform the whole inclusive segment. */
    if (y0 == y1 || x0 == x1) {
        SDL_Rect logical, physical;
        logical.x = x0 < x1 ? x0 : x1;
        logical.y = y0 < y1 ? y0 : y1;
        logical.w = (x0 < x1 ? x1 - x0 : x0 - x1) + 1;
        logical.h = (y0 < y1 ? y1 - y0 : y0 - y1) + 1;
        physical = Mini_PhysicalRect(logical);
        if (physical.w <= 0 || physical.h <= 0) return 0;
        return Mini_FillPrimitive(physical, color, blend);
    }
    /* Bresenham's pixels are grouped into maximal horizontal or vertical
     * runs. This preserves the rasterized pixel set while avoiding a GFX
     * submission for each pixel. The final run is flushed after the loop. */
    dx = (Sint64)x1 - x0;
    if (dx < 0) dx = -dx;
    dy = (Sint64)y1 - y0;
    if (dy < 0) dy = -dy;
    {
        SDL_bool horizontal = dx >= dy;
        int run_x = x0, run_y = y0, last_x = x0, last_y = y0;
        int next_x, next_y;
        dy = -dy;
        step_x = x0 < x1 ? 1 : -1;
        step_y = y0 < y1 ? 1 : -1;
        err = dx + dy;
        for (;;) {
            if (x0 == x1 && y0 == y1) break;
            e2 = 2 * err;
            next_x = x0;
            next_y = y0;
            if (e2 >= dy) { err += dy; next_x += (int)step_x; }
            if (e2 <= dx) { err += dx; next_y += (int)step_y; }
            /* A change in the minor axis starts a new run. */
            if ((horizontal && next_y != last_y) ||
                (!horizontal && next_x != last_x)) {
                SDL_Rect logical, physical;
                logical.x = run_x < last_x ? run_x : last_x;
                logical.y = run_y < last_y ? run_y : last_y;
                logical.w = (int)((Sint64)(run_x > last_x ? run_x : last_x) - logical.x + 1);
                logical.h = (int)((Sint64)(run_y > last_y ? run_y : last_y) - logical.y + 1);
                physical = Mini_PhysicalRect(logical);
                if (physical.w > 0 && physical.h > 0 &&
                    Mini_FillPrimitive(physical, color, blend) < 0) return -1;
                run_x = next_x;
                run_y = next_y;
            }
            x0 = next_x;
            y0 = next_y;
            last_x = x0;
            last_y = y0;
        }
        {
            SDL_Rect logical, physical;
            logical.x = run_x < last_x ? run_x : last_x;
            logical.y = run_y < last_y ? run_y : last_y;
            logical.w = (int)((Sint64)(run_x > last_x ? run_x : last_x) - logical.x + 1);
            logical.h = (int)((Sint64)(run_y > last_y ? run_y : last_y) - logical.y + 1);
            physical = Mini_PhysicalRect(logical);
            if (physical.w > 0 && physical.h > 0 &&
                Mini_FillPrimitive(physical, color, blend) < 0) return -1;
        }
    }
    return 0;
}

static int Mini_RunCommandQueue(SDL_Renderer *renderer, SDL_RenderCommand *cmd, void *vertices, size_t vertsize)
{
    SDL_Rect viewport = {0, 0, vid_win ? vid_win->w : 0, vid_win ? vid_win->h : 0};
    SDL_Rect cliprect = {0, 0, 0, 0};
    SDL_bool clipping = SDL_FALSE;
    if (GFX_WaitPending() < 0) return -1;
    for (; cmd; cmd = cmd->next) {
        switch (cmd->command) {
        case SDL_RENDERCMD_NO_OP:
        case SDL_RENDERCMD_SETDRAWCOLOR:
            /* Each draw command already carries its color and blend mode. */
            break;
        case SDL_RENDERCMD_SETVIEWPORT:
            viewport = cmd->data.viewport.rect;
            break;
        case SDL_RENDERCMD_SETCLIPRECT:
            clipping = cmd->data.cliprect.enabled;
            cliprect = cmd->data.cliprect.rect;
            break;
        case SDL_RENDERCMD_CLEAR: {
            SDL_Rect full = {0, 0, FB_W, FB_H};
            Uint32 color = ((Uint32)cmd->data.color.a << 24) |
                           ((Uint32)cmd->data.color.r << 16) |
                           ((Uint32)cmd->data.color.g << 8) | cmd->data.color.b;
            if (GFX_FillRect(full, color) < 0) return -1;
            break;
        }
        case SDL_RENDERCMD_FILL_RECTS: {
            size_t offset = cmd->data.draw.first;
            size_t count = cmd->data.draw.count;
            const SDL_FRect *rects;
            SDL_Rect bounds = viewport;
            Uint32 color = ((Uint32)cmd->data.draw.a << 24) |
                           ((Uint32)cmd->data.draw.r << 16) |
                           ((Uint32)cmd->data.draw.g << 8) | cmd->data.draw.b;
            size_t i;
            if (!vid_win || vid_win->w <= 0 || vid_win->h <= 0 ||
                offset > vertsize || count > (vertsize - offset) / sizeof(*rects))
                return SDL_SetError("Invalid MINI queued fill");
            if (cmd->data.draw.blend != SDL_BLENDMODE_NONE &&
                cmd->data.draw.blend != SDL_BLENDMODE_BLEND)
                return SDL_SetError("Unsupported MINI fill blend mode");
            rects = (const SDL_FRect *)((const Uint8 *)vertices + offset);
            if (clipping) {
                SDL_Rect relative = cliprect;
                relative.x += viewport.x;
                relative.y += viewport.y;
                if (!SDL_IntersectRect(&bounds, &relative, &bounds)) break;
            }
            for (i = 0; i < count; ++i) {
                SDL_Rect dst, clipped, physical;
                dst.x = (int)rects[i].x + viewport.x;
                dst.y = (int)rects[i].y + viewport.y;
                dst.w = (int)rects[i].w;
                dst.h = (int)rects[i].h;
                if (dst.w <= 0 || dst.h <= 0 || !SDL_IntersectRect(&bounds, &dst, &clipped)) continue;
                physical = Mini_PhysicalRect(clipped);
                if (physical.w > 0 && physical.h > 0 && Mini_FillPrimitive(physical, color, cmd->data.draw.blend) < 0) return -1;
            }
            break;
        }
        case SDL_RENDERCMD_DRAW_POINTS:
        case SDL_RENDERCMD_DRAW_LINES: {
            size_t offset = cmd->data.draw.first;
            size_t count = cmd->data.draw.count;
            const SDL_FPoint *points;
            SDL_Rect bounds = viewport;
            Uint32 color = ((Uint32)cmd->data.draw.a << 24) |
                           ((Uint32)cmd->data.draw.r << 16) |
                           ((Uint32)cmd->data.draw.g << 8) | cmd->data.draw.b;
            size_t i;
            if (!vid_win || vid_win->w <= 0 || vid_win->h <= 0 ||
                offset > vertsize || count > (vertsize - offset) / sizeof(*points))
                return SDL_SetError("Invalid MINI queued points");
            if (cmd->data.draw.blend != SDL_BLENDMODE_NONE &&
                cmd->data.draw.blend != SDL_BLENDMODE_BLEND)
                return SDL_SetError("Unsupported MINI primitive blend mode");
            points = (const SDL_FPoint *)((const Uint8 *)vertices + offset);
            if (clipping) {
                SDL_Rect relative = cliprect;
                relative.x += viewport.x;
                relative.y += viewport.y;
                if (!SDL_IntersectRect(&bounds, &relative, &bounds)) break;
            }
            if (cmd->command == SDL_RENDERCMD_DRAW_POINTS) {
                for (i = 0; i < count; ++i) {
                    if (Mini_DrawPixel((int)points[i].x + viewport.x,
                                       (int)points[i].y + viewport.y, &bounds, color, cmd->data.draw.blend) < 0) return -1;
                }
            } else {
                for (i = 1; i < count; ++i) {
                    if (Mini_DrawLine((int)points[i - 1].x + viewport.x,
                                      (int)points[i - 1].y + viewport.y,
                                      (int)points[i].x + viewport.x,
                                      (int)points[i].y + viewport.y, &bounds, color, cmd->data.draw.blend) < 0) return -1;
                }
            }
            break;
        }
        case SDL_RENDERCMD_COPY:
        case SDL_RENDERCMD_COPY_EX: {
            Mini_CopyCommand *copy;
            SDL_Texture *texture = cmd->data.draw.texture;
            Mini_TextureData *t = texture ? (Mini_TextureData *)texture->driverdata : NULL;
            size_t offset = cmd->data.draw.first;
            SDL_Rect src, dst, bounds, physical;
            int result;
            if (!t || !vid_win || vid_win->w <= 0 || vid_win->h <= 0 ||
                offset > vertsize || sizeof(*copy) > vertsize - offset) {
                return SDL_SetError("Invalid MINI queued copy");
            }
            copy = (Mini_CopyCommand *)((Uint8 *)vertices + offset);
            src = copy->src;
            dst = copy->dst;
            dst.x += viewport.x;
            dst.y += viewport.y;
            bounds = viewport;
            if (clipping) {
                SDL_Rect relative = cliprect;
                relative.x += viewport.x;
                relative.y += viewport.y;
                if (!SDL_IntersectRect(&bounds, &relative, &bounds)) break;
            }
            if (!Mini_ClipCopy(&src, &dst, &bounds)) break;
            /* Flipped clipped copies require reversing the clipping side in
             * texture space; reject until that mapping is implemented. */
            if (copy->flip != SDL_FLIP_NONE &&
                (src.x != copy->src.x || src.y != copy->src.y ||
                 src.w != copy->src.w || src.h != copy->src.h ||
                 dst.w != copy->dst.w || dst.h != copy->dst.h))
                return SDL_Unsupported();
            /* Reject invalid texture rectangles before touching staging RAM. */
            if (src.x < 0 || src.y < 0 || src.w > (int)t->w - src.x ||
                src.h > (int)t->h - src.y || src.x > (int)t->w || src.y > (int)t->h)
                return SDL_SetError("MINI copy source exceeds texture");
            physical = Mini_PhysicalRect(dst);
            /* Physical bounds must be checked before submitting to MI_GFX.
             * A partially visible physical rectangle is not safely blitted
             * without a second source adjustment; reject rather than write OOB. */
            if (physical.w <= 0 || physical.h <= 0) break;
            if (physical.x < 0 || physical.y < 0 ||
                physical.x > FB_W || physical.w > FB_W - physical.x ||
                physical.y > FB_H || physical.h > FB_H - physical.y)
                return SDL_SetError("MINI copy exceeds physical framebuffer");
            if (t->dma) {
                result = GFX_CopyTextureResident(t->data, (int)t->w, (int)t->h, (int)t->pitch,
                                                t->fmt, src, physical, copy->blend, copy->color,
                                                copy->flip, t->physical, &t->dirty);
            } else {
                result = GFX_CopyTextureEx(t->data, (int)t->w, (int)t->h, (int)t->pitch,
                                         t->fmt, src, physical, copy->blend, copy->color, copy->flip);
            }
            if (result < 0) return result;
            break;
        }
        default:
            return SDL_SetError("Unsupported MINI render command: %d", (int)cmd->command);
        }
    }
    return GFX_WaitPending();
}

static int Mini_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect, Uint32 pixel_format, void *pixels, int pitch)
{
    debug("%s\n", __func__);
    return SDL_Unsupported();
}

static void Mini_RenderPresent(SDL_Renderer *renderer)
{
    debug("%s\n", __func__);
    GFX_Flip();
}

static void Mini_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    Mini_TextureData *t = (Mini_TextureData *)texture->driverdata;

    debug("%s\n", __func__);
    if (t) {
        GFX_InvalidateTexture(t->data);
        if (t->data) {
            if (t->dma) {
                /* Retire asynchronous GPU reads before releasing DMA storage.
                 * If the wait fails, retain storage rather than free it early. */
                if (GFX_WaitPending() < 0) return;
                MI_SYS_Munmap(t->data, (MI_U32)(t->h * t->pitch));
                MI_SYS_MMA_Free(t->physical);
            } else {
                SDL_free(t->data);
            }
        }
        SDL_free(t);
        texture->driverdata = NULL;
    }
}

static void Mini_DestroyRenderer(SDL_Renderer *renderer)
{
    Mini_RenderData *data = (Mini_RenderData *)renderer->driverdata;

    debug("%s\n", __func__);
    if (data) SDL_free(data);
    SDL_free(renderer);
}

static int Mini_SetVSync(SDL_Renderer *renderer, const int vsync)
{
    debug("%s\n", __func__);
    return vsync ? SDL_Unsupported() : 0;
}

SDL_Renderer *Mini_CreateRenderer(SDL_Window *window, Uint32 flags)
{
    SDL_Renderer *renderer = NULL;
    Mini_RenderData *data = NULL;

    debug("%s\n", __func__);
    renderer = (SDL_Renderer *) SDL_calloc(1, sizeof(SDL_Renderer));
    if (!renderer) {
        debug("%s, failed to create render\n", __func__);
        SDL_OutOfMemory();
        return NULL;
    }

    data = (Mini_RenderData *) SDL_calloc(1, sizeof(Mini_RenderData));
    if (!data) {
        debug("%s, failed to create render data\n", __func__);
        Mini_DestroyRenderer(renderer);
        SDL_OutOfMemory();
        return NULL;
    }

    renderer->WindowEvent = Mini_WindowEvent;
    renderer->CreateTexture = Mini_CreateTexture;
    renderer->UpdateTexture = Mini_UpdateTexture;
    renderer->LockTexture = Mini_LockTexture;
    renderer->UnlockTexture = Mini_UnlockTexture;
    renderer->SetTextureScaleMode = Mini_SetTextureScaleMode;
    renderer->SetRenderTarget = Mini_SetRenderTarget;
    renderer->QueueSetViewport = Mini_QueueSetViewport;
    renderer->QueueSetDrawColor = Mini_QueueSetViewport;
    renderer->QueueDrawPoints = Mini_QueueDrawPoints;
    renderer->QueueDrawLines = Mini_QueueDrawPoints;
    renderer->QueueGeometry = Mini_QueueGeometry;
    renderer->QueueFillRects = Mini_QueueFillRects;
    renderer->QueueCopy = Mini_QueueCopy;
    renderer->QueueCopyEx = Mini_QueueCopyEx;
    renderer->RunCommandQueue = Mini_RunCommandQueue;
    renderer->RenderReadPixels = Mini_RenderReadPixels;
    renderer->RenderPresent = Mini_RenderPresent;
    renderer->DestroyTexture = Mini_DestroyTexture;
    renderer->DestroyRenderer = Mini_DestroyRenderer;
    renderer->SetVSync = Mini_SetVSync;
    renderer->info = Mini_RenderDriver.info;
    renderer->info.flags = SDL_RENDERER_ACCELERATED;
    renderer->driverdata = data;
    renderer->window = window;

    if(data->is_init != SDL_FALSE) {
        return renderer;
    }
    data->is_init = SDL_TRUE;
    return renderer;
}

SDL_RenderDriver Mini_RenderDriver = {
    .CreateRenderer = Mini_CreateRenderer,
    .info = {
        .name = "Miyoo Mini",
        .flags = SDL_RENDERER_ACCELERATED,
        .num_texture_formats = 2,
        .texture_formats = {
            [0] = SDL_PIXELFORMAT_RGB565,
            [1] = SDL_PIXELFORMAT_ARGB8888,
        },
        .max_texture_width = 640,
        .max_texture_height = 480,
    }
};

#endif

