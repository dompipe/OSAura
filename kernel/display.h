#ifndef OSAURA_DISPLAY_H
#define OSAURA_DISPLAY_H

#include <stdint.h>
#include "boot-info.h"

typedef enum {
    OSAURA_DISPLAY_PIXEL_RGBX8 = 0,
    OSAURA_DISPLAY_PIXEL_BGRX8 = 1
} osaura_display_pixel_format;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
} osaura_display_surface;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} osaura_display_rect;

int osaura_display_init_surface(const osaura_display_surface *surface);
int osaura_display_init_gop(const osaura_boot_info *boot);
int osaura_display_ready(void);
const osaura_display_surface *osaura_display_primary(void);
uint32_t osaura_display_pack_rgb(uint8_t r, uint8_t g, uint8_t b);
int osaura_display_put_pixel(uint32_t x, uint32_t y, uint32_t packed_pixel);
int osaura_display_fill_rect(const osaura_display_rect *rect, uint32_t packed_pixel);
int osaura_display_clear(uint32_t packed_pixel);
int osaura_display_blit_xrgb(const osaura_display_rect *dst,
                             const uint32_t *src,
                             uint32_t src_stride_pixels);

/* Backend-neutral damage tracking. Drawing expands one aggregate dirty region.
 * A presenter consumes it after copying the changed pixels to the device/window. */
int osaura_display_dirty_peek(osaura_display_rect *rect);
int osaura_display_dirty_take(osaura_display_rect *rect);
void osaura_display_dirty_all(void);

#endif
