#include "display.h"

#include <stddef.h>
#include <stdint.h>

static osaura_display_surface g_primary;
static uint8_t g_ready;

static uint32_t convert_xrgb(uint32_t xrgb) {
    uint8_t r = (uint8_t)((xrgb >> 16) & 0xffu);
    uint8_t g = (uint8_t)((xrgb >> 8) & 0xffu);
    uint8_t b = (uint8_t)(xrgb & 0xffu);
    return osaura_display_pack_rgb(r, g, b);
}

int osaura_display_init_surface(const osaura_display_surface *surface) {
    g_ready = 0u;
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
    return 0;
}
