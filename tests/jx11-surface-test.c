#include "../kernel/display.h"
#include "../runtime/jx/jx11-surface.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint32_t framebuffer[64u * 64u];

static void *test_alloc(size_t bytes, void *context) {
    (void)context;
    return calloc(1u, bytes);
}

static void test_free(void *ptr, size_t bytes, void *context) {
    (void)bytes;
    (void)context;
    free(ptr);
}

static int expect_pixel(uint32_t x, uint32_t y, uint32_t packed) {
    uint32_t actual = framebuffer[y * 64u + x];
    if (actual != packed) {
        fprintf(stderr, "pixel %u,%u expected %08x got %08x\n", x, y, packed, actual);
        return 0;
    }
    return 1;
}

int main(void) {
    osaura_display_surface primary = {64u, 64u, 64u, OSAURA_DISPLAY_PIXEL_RGBX8,
                                      (uint64_t)(uintptr_t)framebuffer, sizeof framebuffer};
    if (osaura_display_init_surface(&primary) != 0) return 1;
    if (osaura_jx11_surface_init(test_alloc, test_free, 0) != 0) return 2;
    if (osaura_jx11_surface_set_background(0x00000000u) != 0) return 3;

    uint32_t red = OSAURA_JX11_SURFACE_NONE;
    uint32_t blue = OSAURA_JX11_SURFACE_NONE;
    if (osaura_jx11_surface_create(1u, 20u, 20u, &red) != 0) return 4;
    if (osaura_jx11_surface_create(2u, 20u, 20u, &blue) != 0) return 5;
    if (red == blue || osaura_jx11_surface_count() != 2u) return 6;

    osaura_jx11_surface_fill fill_red = {0u, 0u, 20u, 20u, 0xffff0000u};
    osaura_jx11_surface_fill fill_blue_half = {0u, 0u, 20u, 20u, 0x800000ffu};
    if (osaura_jx11_surface_fill_as(1u, red, &fill_red) != 0) return 7;
    if (osaura_jx11_surface_fill_as(2u, blue, &fill_blue_half) != 0) return 8;
    if (osaura_jx11_surface_move_as(1u, red, 10, 10) != 0) return 9;
    if (osaura_jx11_surface_move_as(2u, blue, 15, 15) != 0) return 10;
    if (osaura_jx11_surface_set_z_as(1u, red, 1) != 0) return 11;
    if (osaura_jx11_surface_set_z_as(2u, blue, 2) != 0) return 12;

    if (osaura_jx11_surface_fill_as(99u, red, &fill_red) != -3) return 13;
    if (osaura_jx11_surface_compose() <= 0) return 14;

    if (!expect_pixel(11u, 11u, 0x000000ffu)) return 15; /* opaque red */
    if (!expect_pixel(16u, 16u, 0x0080007fu)) return 16; /* half blue over red */
    if (!expect_pixel(2u, 2u, 0x00000000u)) return 17;

    if (osaura_jx11_surface_set_opacity_as(2u, blue, 128u) != 0) return 18;
    if (osaura_jx11_surface_compose() <= 0) return 19;
    if (!expect_pixel(16u, 16u, 0x004000bfu)) return 20; /* 128 alpha * 128 global opacity */

    if (osaura_jx11_surface_move_as(1u, red, 36, 36) != 0) return 21;
    if (osaura_jx11_surface_compose() <= 0) return 22;
    if (!expect_pixel(11u, 11u, 0x00000000u)) return 23; /* old red location repaired */
    if (!expect_pixel(37u, 37u, 0x000000ffu)) return 24;

    if (osaura_jx11_surface_set_visible_as(2u, blue, 0) != 0) return 25;
    if (osaura_jx11_surface_compose() <= 0) return 26;
    if (!expect_pixel(16u, 16u, 0x00000000u)) return 27;

    if (osaura_jx11_surface_destroy(2u, blue) != 0) return 28;
    if (osaura_jx11_surface_destroy(1u, red) != 0) return 29;
    if (osaura_jx11_surface_count() != 0u) return 30;
    osaura_jx11_surface_shutdown();

    puts("JX11 SURFACES: PASS");
    return 0;
}
