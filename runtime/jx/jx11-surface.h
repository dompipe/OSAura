#ifndef OSAURA_JX11_SURFACE_H
#define OSAURA_JX11_SURFACE_H

#include <stdint.h>
#include <stddef.h>

#define OSAURA_JX11_SURFACE_MAX 64u
#define OSAURA_JX11_SURFACE_NONE UINT32_MAX

typedef void *(*osaura_jx11_alloc_fn)(size_t bytes, void *context);
typedef void (*osaura_jx11_free_fn)(void *ptr, size_t bytes, void *context);

typedef struct {
    uint32_t surface_id;
    uint32_t owner_subject;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    int32_t z;
    uint8_t opacity;
    uint8_t visible;
} osaura_jx11_surface_info;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t color_argb;
} osaura_jx11_surface_fill;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    const uint32_t *pixels_argb;
    uint32_t stride_pixels;
} osaura_jx11_surface_blit;

int osaura_jx11_surface_init(osaura_jx11_alloc_fn alloc_fn,
                             osaura_jx11_free_fn free_fn,
                             void *context);
void osaura_jx11_surface_shutdown(void);
int osaura_jx11_surface_set_background(uint32_t color_xrgb);
int osaura_jx11_surface_create(uint32_t owner_subject,
                               uint32_t width,
                               uint32_t height,
                               uint32_t *surface_id);
int osaura_jx11_surface_destroy(uint32_t owner_subject, uint32_t surface_id);
int osaura_jx11_surface_get_info(uint32_t surface_id, osaura_jx11_surface_info *out);
int osaura_jx11_surface_fill_as(uint32_t owner_subject,
                                uint32_t surface_id,
                                const osaura_jx11_surface_fill *request);
int osaura_jx11_surface_blit_as(uint32_t owner_subject,
                                uint32_t surface_id,
                                const osaura_jx11_surface_blit *request);
int osaura_jx11_surface_move_as(uint32_t owner_subject,
                                uint32_t surface_id,
                                int32_t x,
                                int32_t y);
int osaura_jx11_surface_set_z_as(uint32_t owner_subject,
                                 uint32_t surface_id,
                                 int32_t z);
int osaura_jx11_surface_set_opacity_as(uint32_t owner_subject,
                                       uint32_t surface_id,
                                       uint8_t opacity);
int osaura_jx11_surface_set_visible_as(uint32_t owner_subject,
                                       uint32_t surface_id,
                                       int visible);
int osaura_jx11_surface_compose(void);
uint32_t osaura_jx11_surface_count(void);

#endif
