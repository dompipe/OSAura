#include "jx11-display.h"
#include "../../kernel/display.h"

static uint32_t pack_xrgb(uint32_t xrgb) {
    uint8_t r = (uint8_t)((xrgb >> 16) & 0xffu);
    uint8_t g = (uint8_t)((xrgb >> 8) & 0xffu);
    uint8_t b = (uint8_t)(xrgb & 0xffu);
    return osaura_display_pack_rgb(r, g, b);
}

int osaura_jx11_display_info(osaura_jx11_display_info *out) {
    if (!out) return -1;
    const osaura_display_surface *surface = osaura_display_primary();
    if (!surface) return -2;
    out->width = surface->width;
    out->height = surface->height;
    out->stride_pixels = surface->stride_pixels;
    out->pixel_format = surface->pixel_format;
    return 0;
}

int osaura_jx11_display_clear(uint32_t color_xrgb) {
    if (!osaura_display_ready()) return -1;
    return osaura_display_clear(pack_xrgb(color_xrgb));
}

int osaura_jx11_display_fill(const osaura_jx11_fill_request *request) {
    if (!request || !osaura_display_ready()) return -1;
    osaura_display_rect rect = {
        request->x,
        request->y,
        request->width,
        request->height
    };
    return osaura_display_fill_rect(&rect, pack_xrgb(request->color_xrgb));
}

int osaura_jx11_display_blit(const osaura_jx11_blit_request *request) {
    if (!request || !osaura_display_ready()) return -1;
    osaura_display_rect rect = {
        request->x,
        request->y,
        request->width,
        request->height
    };
    return osaura_display_blit_xrgb(&rect,
                                    request->pixels_xrgb,
                                    request->stride_pixels);
}
