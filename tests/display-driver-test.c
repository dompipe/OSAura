#include "../kernel/display.h"
#include "../runtime/jx/jx11-display.h"

#include <stdint.h>
#include <stdio.h>

static uint32_t fb[6u * 4u];

static int expect(int condition, const char *name) {
    if (condition) return 0;
    printf("FAIL: %s\n", name);
    return 1;
}

static void zero_fb(void) {
    for (uint32_t i = 0u; i < 24u; ++i) fb[i] = 0u;
}

int main(void) {
    osaura_boot_info boot = {0};
    boot.width = 4u;
    boot.height = 4u;
    boot.pixels_per_scanline = 6u;
    boot.framebuffer_base = (uint64_t)(uintptr_t)fb;
    boot.framebuffer_size = sizeof fb;
    boot.pixel_format = OSAURA_PIXEL_RGBX8;

    if (expect(osaura_display_init_gop(&boot) == 0, "init RGBX")) return 1;
    if (expect(osaura_display_ready(), "ready")) return 1;

    osaura_jx11_display_info info = {0};
    if (expect(osaura_jx11_display_get_info(&info) == 0, "jx11 info")) return 1;
    if (expect(info.width == 4u && info.height == 4u && info.stride_pixels == 6u,
               "surface geometry")) return 1;

    zero_fb();
    if (expect(osaura_jx11_display_clear(0x00112233u) == 0, "clear")) return 1;
    if (expect(fb[0] == 0x00332211u, "RGBX packing")) return 1;
    if (expect(fb[4] == 0u && fb[5] == 0u, "clear respects stride padding")) return 1;

    zero_fb();
    osaura_jx11_fill_request fill = {3u, 3u, 4u, 4u, 0x00a0b0c0u};
    if (expect(osaura_jx11_display_fill(&fill) == 0, "clipped fill")) return 1;
    if (expect(fb[3u + 3u * 6u] == 0x00c0b0a0u, "fill pixel")) return 1;
    if (expect(fb[2u + 3u * 6u] == 0u, "fill clip x")) return 1;

    zero_fb();
    const uint32_t src[4] = {0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0x00ffffffu};
    osaura_jx11_blit_request blit = {1u, 1u, 2u, 2u, src, 2u};
    if (expect(osaura_jx11_display_blit(&blit) == 0, "blit")) return 1;
    if (expect(fb[1u + 1u * 6u] == 0x000000ffu, "blit red")) return 1;
    if (expect(fb[2u + 1u * 6u] == 0x0000ff00u, "blit green")) return 1;
    if (expect(fb[1u + 2u * 6u] == 0x00ff0000u, "blit blue")) return 1;

    boot.pixel_format = OSAURA_PIXEL_BGRX8;
    if (expect(osaura_display_init_gop(&boot) == 0, "init BGRX")) return 1;
    zero_fb();
    if (expect(osaura_jx11_display_clear(0x00112233u) == 0, "BGR clear")) return 1;
    if (expect(fb[0] == 0x00112233u, "BGRX packing")) return 1;

    puts("JX11 GOP DISPLAY DRIVER: PASS");
    return 0;
}
