#include "display.h"

#include <stddef.h>
#include <stdint.h>

static osaura_display_surface g_primary;
static osaura_display_rect g_dirty;
static uint8_t g_ready;
static uint8_t g_dirty_valid;

static uint32_t convert_xrgb(uint32_t xrgb) {
    uint8_t r = (uint8_t)((xrgb >> 16) & 0xffu);
    uint8_t g = (uint8_t)((xrgb >> 8) & 0xffu);
    uint8_t b = (uint8_t)(xrgb & 0xffu);
    return osaura_display_pack_rgb(r, g, b);
}

static void mark_dirty(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!g_ready || !width || !height || x >= g_primary.width || y >= g_primary.height) return;
    uint32_t x2 = width > g_primary.width - x ? g_primary.width : x + width;
    uint32_t y2 = height > g_primary.height - y ? g_primary.height : y + height;
    if (!g_dirty_valid) {
        g_dirty.x = x;
        g_dirty.y = y;
        g_dirty.width = x2 - x;
        g_dirty.height = y2 - y;
        g_dirty_valid = 1u;
        return;
    }
    uint32_t old_x2 = g_dirty.x + g_dirty.width;
    uint32_t old_y2 = g_dirty.y + g_dirty.height;
    uint32_t nx = x < g_dirty.x ? x : g_dirty.x;
    uint32_t ny = y < g_dirty.y ? y : g_dirty.y;
    uint32_t nx2 = x2 > old_x2 ? x2 : old_x2;
    uint32_t ny2 = y2 > old_y2 ? y2 : old_y2;
    g_dirty.x = nx;
    g_dirty.y = ny;
    g_dirty.width = nx2 - nx;
    g_dirty.height = ny2 - ny;
}

int osaura_display_init_surface(const osaura_display_surface *surface) {
    g_ready = 0u;
    g_dirty_valid = 0u;
    if (!surface || !surface->framebuffer_base || !surface->framebuffer_size ||
        !surface->width || !surface->height || !surface->stride_pixels)
        return -1;
    if (surface->stride_pixels < surface->width) return -2;
    if (surface->pixel_format != OSAURA_DISPLAY_PIXEL_RGBX8 &&
        surface->pixel_format != OSAURA_DISPLAY_PIXEL_BGRX8)
        return -3;

    uint64_t needed = (uint64_t)surface->stride_pixels *
                      (uint64_t)surface->height * sizeof(uint32_t);
    if (needed > surface->framebuffer_size) return -4;

    g_primary = *surface;
    g_ready = 1u;
    osaura_display_dirty_all();
    return 0;
}

int osaura_display_init_gop(const osaura_boot_info *boot) {
    if (!boot) return -1;
    osaura_display_surface surface;
    surface.width = boot->width;
    surface.height = boot->height;
    surface.stride_pixels = boot->pixels_per_scanline;
    surface.pixel_format = boot->pixel_format == OSAURA_PIXEL_RGBX8
        ? OSAURA_DISPLAY_PIXEL_RGBX8
        : boot->pixel_format == OSAURA_PIXEL_BGRX8
            ? OSAURA_DISPLAY_PIXEL_BGRX8
            : UINT32_MAX;
    surface.framebuffer_base = boot->framebuffer_base;
    surface.framebuffer_size = boot->framebuffer_size;
    return osaura_display_init_surface(&surface);
}

int osaura_display_ready(void) { return g_ready ? 1 : 0; }

const osaura_display_surface *osaura_display_primary(void) {
    return g_ready ? &g_primary : 0;
}

uint32_t osaura_display_pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (!g_ready || g_primary.pixel_format == OSAURA_DISPLAY_PIXEL_RGBX8)
        return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
    return (uint32_t)b | ((uint32_t)g << 8) | ((uint32_t)r << 16);
}

int osaura_display_put_pixel(uint32_t x, uint32_t y, uint32_t packed_pixel) {
    if (!g_ready) return -1;
    if (x >= g_primary.width || y >= g_primary.height) return -2;
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)g_primary.framebuffer_base;
    fb[(uint64_t)y * g_primary.stride_pixels + x] = packed_pixel;
    mark_dirty(x, y, 1u, 1u);
    return 0;
}

int osaura_display_fill_rect(const osaura_display_rect *rect, uint32_t packed_pixel) {
    if (!g_ready || !rect) return -1;
    if (!rect->width || !rect->height) return 0;
    if (rect->x >= g_primary.width || rect->y >= g_primary.height) return 0;

    uint32_t x_end = rect->width > g_primary.width - rect->x
        ? g_primary.width
        : rect->x + rect->width;
    uint32_t y_end = rect->height > g_primary.height - rect->y
        ? g_primary.height
        : rect->y + rect->height;
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)g_primary.framebuffer_base;

    for (uint32_t y = rect->y; y < y_end; ++y) {
        volatile uint32_t *row = fb + (uint64_t)y * g_primary.stride_pixels;
        for (uint32_t x = rect->x; x < x_end; ++x) row[x] = packed_pixel;
    }
    mark_dirty(rect->x, rect->y, x_end - rect->x, y_end - rect->y);
    return 0;
}

int osaura_display_clear(uint32_t packed_pixel) {
    if (!g_ready) return -1;
    osaura_display_rect rect = {0u, 0u, g_primary.width, g_primary.height};
    return osaura_display_fill_rect(&rect, packed_pixel);
}

int osaura_display_blit_xrgb(const osaura_display_rect *dst,
                             const uint32_t *src,
                             uint32_t src_stride_pixels) {
    if (!g_ready || !dst || !src || !src_stride_pixels) return -1;
    if (!dst->width || !dst->height) return 0;
    if (dst->x >= g_primary.width || dst->y >= g_primary.height) return 0;
    if (src_stride_pixels < dst->width) return -2;

    uint32_t copy_w = dst->width > g_primary.width - dst->x
        ? g_primary.width - dst->x
        : dst->width;
    uint32_t copy_h = dst->height > g_primary.height - dst->y
        ? g_primary.height - dst->y
        : dst->height;
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)g_primary.framebuffer_base;

    for (uint32_t y = 0u; y < copy_h; ++y) {
        volatile uint32_t *row = fb + (uint64_t)(dst->y + y) * g_primary.stride_pixels + dst->x;
        const uint32_t *src_row = src + (uint64_t)y * src_stride_pixels;
        for (uint32_t x = 0u; x < copy_w; ++x)
            row[x] = convert_xrgb(src_row[x]);
    }
    mark_dirty(dst->x, dst->y, copy_w, copy_h);
    return 0;
}

int osaura_display_dirty_peek(osaura_display_rect *rect) {
    if (!rect) return -1;
    if (!g_dirty_valid) return 0;
    *rect = g_dirty;
    return 1;
}

int osaura_display_dirty_take(osaura_display_rect *rect) {
    int rc = osaura_display_dirty_peek(rect);
    if (rc > 0) g_dirty_valid = 0u;
    return rc;
}

void osaura_display_dirty_all(void) {
    if (!g_ready) return;
    g_dirty.x = 0u;
    g_dirty.y = 0u;
    g_dirty.width = g_primary.width;
    g_dirty.height = g_primary.height;
    g_dirty_valid = 1u;
}
