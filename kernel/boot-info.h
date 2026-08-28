#ifndef OSAURA_BOOT_INFO_H
#define OSAURA_BOOT_INFO_H

#include <stddef.h>
#include <stdint.h>

#define OSAURA_BOOT_INFO_VERSION 1u

typedef enum {
    OSAURA_PIXEL_RGBX8 = 0,
    OSAURA_PIXEL_BGRX8 = 1,
    OSAURA_PIXEL_UNKNOWN = 255
} osaura_pixel_format;

typedef struct {
    uint8_t version;
    uint8_t pixel_format;
    uint16_t reserved16;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    uint32_t reserved32;
    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_descriptor_size;
    uint32_t memory_descriptor_version;
    uint32_t reserved_map;
} osaura_boot_info;

#endif
