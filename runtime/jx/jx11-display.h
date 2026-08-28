#ifndef OSAURA_JX11_DISPLAY_H
#define OSAURA_JX11_DISPLAY_H

#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
} osaura_jx11_display_info;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t color_xrgb;
} osaura_jx11_fill_request;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    const uint32_t *pixels_xrgb;
    uint32_t stride_pixels;
} osaura_jx11_blit_request;

int osaura_jx11_display_info(osaura_jx11_display_info *out);
int osaura_jx11_display_clear(uint32_t color_xrgb);
int osaura_jx11_display_fill(const osaura_jx11_fill_request *request);
int osaura_jx11_display_blit(const osaura_jx11_blit_request *request);

#endif
