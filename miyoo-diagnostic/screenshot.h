/* Framebuffer screenshots for the MINI diagnostic. The BMP writer streams one
 * row at a time and reverses the driver's fixed 180-degree panel rotation. */
#include <errno.h>
#include <string.h>

typedef struct ScreenshotView {
    const Uint8 *pixels;
    Uint32 width, height, stride;
    unsigned red_offset, red_length;
    unsigned green_offset, green_length;
    unsigned blue_offset, blue_length;
} ScreenshotView;

static void screenshot_u32(Uint8 *out, Uint32 value)
{
    out[0] = (Uint8)value;
    out[1] = (Uint8)(value >> 8);
    out[2] = (Uint8)(value >> 16);
    out[3] = (Uint8)(value >> 24);
}

static Uint8 screenshot_channel(Uint32 pixel, unsigned offset, unsigned length)
{
    Uint32 mask = (1u << length) - 1u;
    return (Uint8)(((pixel >> offset) & mask) * 255u / mask);
}

static int screenshot_write_bmp(const char *path, const ScreenshotView *view)
{
    Uint8 header[54] = {0};
    Uint8 *row;
    Uint32 row_bytes, image_bytes, y, x;
    FILE *out;
    int rc = 0;
    if (!view->pixels || !view->width || !view->height ||
        view->width > 4096 || view->height > 4096 ||
        view->stride < view->width * 4 ||
        !view->red_length || view->red_length > 8 || view->red_offset > 32 - view->red_length ||
        !view->green_length || view->green_length > 8 || view->green_offset > 32 - view->green_length ||
        !view->blue_length || view->blue_length > 8 || view->blue_offset > 32 - view->blue_length)
        return SDL_SetError("Unsupported screenshot pixel layout");
    row_bytes = (view->width * 3 + 3) & ~3u;
    image_bytes = row_bytes * view->height;
    row = (Uint8 *)SDL_calloc(1, row_bytes);
    if (!row) return SDL_OutOfMemory();
    out = fopen(path, "wb");
    if (!out) {
        SDL_free(row);
        return SDL_SetError("Cannot create screenshot: %s", strerror(errno));
    }
    header[0] = 'B'; header[1] = 'M';
    screenshot_u32(header + 2, 54 + image_bytes);
    screenshot_u32(header + 10, 54);
    screenshot_u32(header + 14, 40);
    screenshot_u32(header + 18, view->width);
    screenshot_u32(header + 22, view->height);
    header[26] = 1; header[28] = 24;
    screenshot_u32(header + 34, image_bytes);
    if (fwrite(header, 1, sizeof(header), out) != sizeof(header)) rc = -1;
    /* BMP stores rows bottom-up. Reversing each raw row horizontally therefore
     * also reverses the vertical axis when the BMP is displayed upright. */
    for (y = 0; y < view->height && rc == 0; ++y) {
        const Uint8 *source = view->pixels + (size_t)y * view->stride;
        for (x = 0; x < view->width; ++x) {
            Uint32 pixel;
            SDL_memcpy(&pixel, source + (size_t)(view->width - 1 - x) * 4, sizeof(pixel));
            row[x * 3] = screenshot_channel(pixel, view->blue_offset, view->blue_length);
            row[x * 3 + 1] = screenshot_channel(pixel, view->green_offset, view->green_length);
            row[x * 3 + 2] = screenshot_channel(pixel, view->red_offset, view->red_length);
        }
        if (fwrite(row, 1, row_bytes, out) != row_bytes) rc = -1;
    }
    if (fclose(out) != 0) rc = -1;
    SDL_free(row);
    if (rc < 0) return SDL_SetError("Cannot write screenshot BMP");
    return 0;
}

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include "mi_sys.h"

static int screenshot_capture(const char *path)
{
    struct fb_fix_screeninfo fixed;
    struct fb_var_screeninfo visible;
    ScreenshotView view;
    Uint64 offset, end;
    void *mapping = NULL;
    MI_S32 mapped;
    int rc, fd = open("/dev/fb0", O_RDONLY);
    if (fd < 0) return SDL_SetError("Cannot open screenshot framebuffer: %s", strerror(errno));
    rc = ioctl(fd, FBIOGET_FSCREENINFO, &fixed);
    if (rc == 0) rc = ioctl(fd, FBIOGET_VSCREENINFO, &visible);
    close(fd);
    if (rc < 0) return SDL_SetError("Cannot query screenshot framebuffer: %s", strerror(errno));
    if (visible.bits_per_pixel != 32 || !visible.xres || !visible.yres ||
        visible.xres > 4096 || visible.yres > 4096 || !fixed.smem_len ||
        visible.red.msb_right || visible.green.msb_right || visible.blue.msb_right ||
        visible.nonstd || visible.grayscale || fixed.type != FB_TYPE_PACKED_PIXELS ||
        fixed.visual != FB_VISUAL_TRUECOLOR ||
        (Uint64)(visible.xoffset + (Uint64)visible.xres) * 4 > fixed.line_length)
        return SDL_SetError("Unsupported screenshot framebuffer layout");
    offset = (Uint64)visible.yoffset * fixed.line_length + (Uint64)visible.xoffset * 4;
    end = offset + (Uint64)(visible.yres - 1) * fixed.line_length + (Uint64)visible.xres * 4;
    if (end > fixed.smem_len)
        return SDL_SetError("Screenshot viewport exceeds framebuffer memory");
    /* SDL's MINI backend already owns MI_SYS_Init/Exit. Read its framebuffer
     * through an uncached mapping so MI_GFX writes are visible to the CPU. */
    mapped = MI_SYS_Mmap(fixed.smem_start, fixed.smem_len, &mapping, FALSE);
    if (mapped != MI_SUCCESS || !mapping) {
        if (mapping) MI_SYS_Munmap(mapping, fixed.smem_len);
        return SDL_SetError("Cannot map screenshot framebuffer: %d", (int)mapped);
    }
    view.pixels = (const Uint8 *)mapping + (size_t)offset;
    view.width = visible.xres; view.height = visible.yres; view.stride = fixed.line_length;
    view.red_offset = visible.red.offset; view.red_length = visible.red.length;
    view.green_offset = visible.green.offset; view.green_length = visible.green.length;
    view.blue_offset = visible.blue.offset; view.blue_length = visible.blue.length;
    rc = screenshot_write_bmp(path, &view);
    mapped = MI_SYS_Munmap(mapping, fixed.smem_len);
    if (mapped != MI_SUCCESS && rc == 0)
        rc = SDL_SetError("Cannot unmap screenshot framebuffer: %d", (int)mapped);
    return rc;
}
#endif

static void screenshot_scene(int scene)
{
    static const char *names[] = {"bars", "blending", "lines", "textures", "modulation", "flips", "viewport"};
    const char *enabled = SDL_getenv("MIYOO_SCREENSHOTS");
    char path[96];
    if (enabled && strcmp(enabled, "0") == 0) return;
    snprintf(path, sizeof(path), "screenshots/scene-%02d-%s.bmp", scene, names[scene]);
#if defined(__linux__)
    if (mkdir("screenshots", 0777) < 0 && errno != EEXIST) {
        result(path, SDL_SetError("Cannot create screenshots directory: %s", strerror(errno)));
        return;
    }
    result(path, screenshot_capture(path));
#else
    (void)screenshot_write_bmp;
    result(path, SDL_SetError("Framebuffer screenshots require Miyoo Linux"));
#endif
}
