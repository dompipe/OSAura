#include "boot-info.h"

#define GLYPH_W 5u
#define GLYPH_H 7u
#define SCALE 2u
#define CELL_W ((GLYPH_W + 1u) * SCALE)
#define CELL_H ((GLYPH_H + 1u) * SCALE)

static osaura_boot_info g_boot;
static uint32_t g_col;
static uint32_t g_row;

static const uint8_t glyphs[43][7] = {
    {0,0,0,0,0,0,0},
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
    {1,1,1,1,17,17,14},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14},{0,4,0,0,4,0,0},{0,0,0,31,0,0,0},
    {0,0,0,0,0,12,12},{0,0,4,0,4,0,0},{0,2,4,8,16,0,0},{0,4,2,31,2,4,0}
};

static int glyph_index(char c) {
    if (c == ' ') return 0;
    if (c >= 'A' && c <= 'Z') return 1 + (c - 'A');
    if (c >= '0' && c <= '9') return 27 + (c - '0');
    if (c == ':') return 37;
    if (c == '-') return 38;
    if (c == '.') return 39;
    if (c == '!') return 40;
    if (c == '/') return 41;
    if (c == '>') return 42;
    return 0;
}

static uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (g_boot.pixel_format == OSAURA_PIXEL_BGRX8)
        return ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= g_boot.width || y >= g_boot.height) return;
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)g_boot.framebuffer_base;
    fb[(uint64_t)y * g_boot.pixels_per_scanline + x] = color;
}

static void clear_screen(void) {
    uint32_t bg = pack_rgb(0, 0, 0);
    for (uint32_t y = 0; y < g_boot.height; ++y)
        for (uint32_t x = 0; x < g_boot.width; ++x)
            put_pixel(x, y, bg);
    g_col = 0;
    g_row = 0;
}

static void draw_char(char c) {
    if (c == '\n') { g_col = 0; ++g_row; return; }
    int gi = glyph_index(c);
    uint32_t fg = pack_rgb(235, 235, 235);
    uint32_t ox = g_col * CELL_W + 16u;
    uint32_t oy = g_row * CELL_H + 16u;
    for (uint32_t y = 0; y < GLYPH_H; ++y) {
        uint8_t bits = glyphs[gi][y];
        for (uint32_t x = 0; x < GLYPH_W; ++x) {
            if (!(bits & (1u << (GLYPH_W - 1u - x)))) continue;
            for (uint32_t sy = 0; sy < SCALE; ++sy)
                for (uint32_t sx = 0; sx < SCALE; ++sx)
                    put_pixel(ox + x * SCALE + sx, oy + y * SCALE + sy, fg);
        }
    }
    ++g_col;
    if ((g_col + 1u) * CELL_W + 16u >= g_boot.width) { g_col = 0; ++g_row; }
}

static void write_text(const char *s) {
    while (*s) draw_char(*s++);
}

__attribute__((noreturn)) void osaura_kernel_main(const osaura_boot_info *boot) {
    if (!boot || boot->version != OSAURA_BOOT_INFO_VERSION || !boot->framebuffer_base) {
        for (;;) __asm__ volatile("hlt");
    }
    g_boot = *boot;
    clear_screen();
    write_text("OSAURA KERNEL\n");
    write_text("X86-64 NATIVE MODE\n");
    write_text("UEFI BOOT SERVICES: EXITED\n");
    write_text("FRAMEBUFFER: OWNED\n");
    write_text("MEMORY MAP: CAPTURED\n\n");
    write_text("OSAURA> ");
    for (;;) __asm__ volatile("hlt");
}
