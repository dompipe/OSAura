#include "boot-info.h"

#define GLYPH_W 5u
#define GLYPH_H 7u
#define SCALE 2u
#define CELL_W ((GLYPH_W + 1u) * SCALE)
#define CELL_H ((GLYPH_H + 1u) * SCALE)
#define MARGIN 16u
#define LINE_MAX 64u

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
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
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
    if (g_boot.pixel_format == OSAURA_PIXEL_RGBX8)
        return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
    return (uint32_t)b | ((uint32_t)g << 8) | ((uint32_t)r << 16);
}

static void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= g_boot.width || y >= g_boot.height) return;
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)g_boot.framebuffer_base;
    fb[(uint64_t)y * g_boot.pixels_per_scanline + x] = color;
}

static void fill_cell(uint32_t col, uint32_t row, uint32_t color) {
    uint32_t ox = col * CELL_W + MARGIN;
    uint32_t oy = row * CELL_H + MARGIN;
    for (uint32_t y = 0; y < CELL_H; ++y)
        for (uint32_t x = 0; x < CELL_W; ++x)
            put_pixel(ox + x, oy + y, color);
}

static void clear_screen(void) {
    uint32_t bg = pack_rgb(0, 0, 0);
    for (uint32_t y = 0; y < g_boot.height; ++y)
        for (uint32_t x = 0; x < g_boot.width; ++x)
            put_pixel(x, y, bg);
    g_col = 0;
    g_row = 0;
}

static void ensure_row(void) {
    if ((g_row + 1u) * CELL_H + MARGIN >= g_boot.height)
        clear_screen();
}

static void newline(void) {
    g_col = 0;
    ++g_row;
    ensure_row();
}

static void draw_char(char c) {
    if (c == '\n') { newline(); return; }
    int gi = glyph_index(c);
    uint32_t fg = pack_rgb(235, 235, 235);
    uint32_t ox = g_col * CELL_W + MARGIN;
    uint32_t oy = g_row * CELL_H + MARGIN;
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
    if ((g_col + 1u) * CELL_W + MARGIN >= g_boot.width) newline();
}

static void erase_char(void) {
    if (g_col == 0) return;
    --g_col;
    fill_cell(g_col, g_row, pack_rgb(0, 0, 0));
}

static void write_text(const char *s) {
    while (*s) draw_char(*s++);
}

static void write_u64(uint64_t value) {
    char digits[21];
    uint32_t count = 0;
    if (value == 0) { draw_char('0'); return; }
    while (value && count < sizeof digits) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count) draw_char(digits[--count]);
}

static int text_equal(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
        if (ca != cb) return 0;
    }
    return *a == 0 && *b == 0;
}

static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static char ps2_poll_char(void) {
    static uint8_t extended;
    if (!(in8(0x64u) & 0x01u)) return 0;
    uint8_t sc = in8(0x60u);
    if (sc == 0xE0u) { extended = 1u; return 0; }
    if (extended) { extended = 0u; return 0; }
    if (sc & 0x80u) return 0;

    if (sc >= 0x02u && sc <= 0x0Au) return (char)('1' + (sc - 0x02u));
    if (sc == 0x0Bu) return '0';
    if (sc == 0x0Cu) return '-';
    if (sc == 0x0Eu) return '\b';
    if (sc == 0x1Cu) return '\n';
    if (sc == 0x39u) return ' ';

    switch (sc) {
        case 0x10: return 'Q'; case 0x11: return 'W'; case 0x12: return 'E';
        case 0x13: return 'R'; case 0x14: return 'T'; case 0x15: return 'Y';
        case 0x16: return 'U'; case 0x17: return 'I'; case 0x18: return 'O';
        case 0x19: return 'P'; case 0x1E: return 'A'; case 0x1F: return 'S';
        case 0x20: return 'D'; case 0x21: return 'F'; case 0x22: return 'G';
        case 0x23: return 'H'; case 0x24: return 'J'; case 0x25: return 'K';
        case 0x26: return 'L'; case 0x2C: return 'Z'; case 0x2D: return 'X';
        case 0x2E: return 'C'; case 0x2F: return 'V'; case 0x30: return 'B';
        case 0x31: return 'N'; case 0x32: return 'M'; default: return 0;
    }
}

static void print_prompt(void) { write_text("OSAURA> "); }

static void run_command(const char *line) {
    if (!line[0]) return;
    if (text_equal(line, "HELP")) {
        write_text("HELP ABOUT MEM CLEAR HALT\n");
    } else if (text_equal(line, "ABOUT")) {
        write_text("OSAURA NATIVE X86-64 KERNEL\n");
        write_text("JX RUNTIME LAYER COMES NEXT\n");
    } else if (text_equal(line, "MEM")) {
        write_text("MEMORY MAP BYTES: ");
        write_u64(g_boot.memory_map_size);
        write_text("\nDESCRIPTORS: ");
        write_u64(g_boot.memory_descriptor_size ?
                  g_boot.memory_map_size / g_boot.memory_descriptor_size : 0);
        write_text("\n");
    } else if (text_equal(line, "CLEAR")) {
        clear_screen();
    } else if (text_equal(line, "HALT")) {
        write_text("CPU HALTED\n");
        __asm__ volatile("cli");
        for (;;) __asm__ volatile("hlt");
    } else {
        write_text("UNKNOWN COMMAND\n");
    }
}

static void terminal_loop(void) {
    char line[LINE_MAX];
    uint32_t length = 0;
    print_prompt();

    for (;;) {
        char c = ps2_poll_char();
        if (!c) { __asm__ volatile("pause"); continue; }
        if (c == '\b') {
            if (length) { --length; erase_char(); }
            continue;
        }
        if (c == '\n') {
            line[length] = 0;
            newline();
            run_command(line);
            length = 0;
            print_prompt();
            continue;
        }
        if (length + 1u < LINE_MAX) {
            line[length++] = c;
            draw_char(c);
        }
    }
}

__attribute__((noreturn)) void osaura_kernel_main(const osaura_boot_info *boot) {
    if (!boot || boot->version != OSAURA_BOOT_INFO_VERSION || !boot->framebuffer_base) {
        __asm__ volatile("cli");
        for (;;) __asm__ volatile("hlt");
    }

    g_boot = *boot;
    clear_screen();
    write_text("OSAURA KERNEL 0.2-DEV\n");
    write_text("X86-64 NATIVE MODE\n");
    write_text("UEFI BOOT SERVICES: EXITED\n");
    write_text("FRAMEBUFFER: OWNED\n");
    write_text("MEMORY MAP: CAPTURED\n");
    write_text("PS2 TERMINAL: ACTIVE\n\n");
    write_text("TYPE HELP FOR COMMANDS\n\n");
    terminal_loop();
}
