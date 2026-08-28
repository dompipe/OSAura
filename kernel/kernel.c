#include "boot-info.h"

#define GLYPH_W 5u
#define GLYPH_H 7u
#define SCALE 2u
#define CELL_W ((GLYPH_W + 1u) * SCALE)
#define CELL_H ((GLYPH_H + 1u) * SCALE)
#define MARGIN 16u
#define LINE_MAX 64u
#define COM1 0x3F8u
#define IDT_ENTRIES 256u
#define IRQ_BASE 32u
#define IRQ_TIMER 0u
#define IRQ_KEYBOARD 1u
#define PIC1_CMD 0x20u
#define PIC1_DATA 0x21u
#define PIC2_CMD 0xA0u
#define PIC2_DATA 0xA1u
#define PIT_COMMAND 0x43u
#define PIT_CHANNEL0 0x40u
#define PIT_HZ 100u
#define PIT_INPUT_HZ 1193182u
#define PAGE_SIZE 4096ull
#define MIN_ALLOC_PHYS 0x100000ull
#define EFI_CONVENTIONAL_MEMORY 7u
#define KEY_QUEUE_SIZE 64u

extern void osaura_arch_load_gdt(void);
extern void osaura_arch_load_idt(const void *base, uint16_t limit);
extern void osaura_arch_enable_interrupts(void);
extern void osaura_arch_disable_interrupts(void);
extern void *osaura_isr_table[48];

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} idt_gate;

typedef struct {
    uint32_t type;
    uint32_t pad;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} efi_memory_descriptor_view;

static osaura_boot_info g_boot;
static idt_gate g_idt[IDT_ENTRIES];
static uint32_t g_col;
static uint32_t g_row;
static uint8_t g_serial_ready;
static volatile uint64_t g_ticks;
static volatile char g_key_queue[KEY_QUEUE_SIZE];
static volatile uint8_t g_key_head;
static volatile uint8_t g_key_tail;
static uint8_t g_ps2_extended;
static uint64_t g_free_pages;
static uint64_t g_allocated_pages;
static uint64_t g_alloc_desc_index;
static uint64_t g_alloc_page_index;

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

static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void io_wait(void) {
    out8(0x80u, 0u);
}

static void serial_init(void) {
    out8(COM1 + 1u, 0x00u);
    out8(COM1 + 3u, 0x80u);
    out8(COM1 + 0u, 0x03u);
    out8(COM1 + 1u, 0x00u);
    out8(COM1 + 3u, 0x03u);
    out8(COM1 + 2u, 0xC7u);
    out8(COM1 + 4u, 0x0Bu);
    g_serial_ready = 1u;
}

static void serial_char(char c) {
    if (!g_serial_ready) return;
    if (c == '\n') serial_char('\r');
    while (!(in8(COM1 + 5u) & 0x20u)) __asm__ volatile("pause");
    out8(COM1, (uint8_t)c);
}

static void serial_text(const char *s) {
    while (*s) serial_char(*s++);
}

static void serial_u64(uint64_t value) {
    char digits[21];
    uint32_t count = 0;
    if (value == 0) { serial_char('0'); return; }
    while (value && count < sizeof digits) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count) serial_char(digits[--count]);
}

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
    serial_char(c);
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
    serial_text("\b \b");
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

static void idt_set_gate(uint8_t vector, void *handler) {
    uint64_t address = (uint64_t)(uintptr_t)handler;
    idt_gate *gate = &g_idt[vector];
    gate->offset_low = (uint16_t)address;
    gate->selector = 0x08u;
    gate->ist = 0u;
    gate->type_attr = 0x8Eu;
    gate->offset_mid = (uint16_t)(address >> 16);
    gate->offset_high = (uint32_t)(address >> 32);
    gate->zero = 0u;
}

static void idt_init(void) {
    for (uint32_t i = 0; i < IDT_ENTRIES; ++i) {
        g_idt[i].offset_low = 0;
        g_idt[i].selector = 0;
        g_idt[i].ist = 0;
        g_idt[i].type_attr = 0;
        g_idt[i].offset_mid = 0;
        g_idt[i].offset_high = 0;
        g_idt[i].zero = 0;
    }
    for (uint32_t i = 0; i < 48u; ++i)
        idt_set_gate((uint8_t)i, osaura_isr_table[i]);
    osaura_arch_load_idt(g_idt, (uint16_t)(sizeof g_idt - 1u));
}

static void pic_remap(void) {
    uint8_t master_mask = in8(PIC1_DATA);
    uint8_t slave_mask = in8(PIC2_DATA);

    out8(PIC1_CMD, 0x11u); io_wait();
    out8(PIC2_CMD, 0x11u); io_wait();
    out8(PIC1_DATA, IRQ_BASE); io_wait();
    out8(PIC2_DATA, IRQ_BASE + 8u); io_wait();
    out8(PIC1_DATA, 0x04u); io_wait();
    out8(PIC2_DATA, 0x02u); io_wait();
    out8(PIC1_DATA, 0x01u); io_wait();
    out8(PIC2_DATA, 0x01u); io_wait();

    (void)master_mask;
    (void)slave_mask;
    out8(PIC1_DATA, 0xFCu);
    out8(PIC2_DATA, 0xFFu);
}

static void pic_eoi(uint8_t irq) {
    if (irq >= 8u) out8(PIC2_CMD, 0x20u);
    out8(PIC1_CMD, 0x20u);
}

static void pit_init(void) {
    uint16_t divisor = (uint16_t)(PIT_INPUT_HZ / PIT_HZ);
    out8(PIT_COMMAND, 0x36u);
    out8(PIT_CHANNEL0, (uint8_t)(divisor & 0xFFu));
    out8(PIT_CHANNEL0, (uint8_t)(divisor >> 8));
}

static char ps2_decode_make(uint8_t sc) {
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

static void keyboard_push(char c) {
    uint8_t next = (uint8_t)((g_key_head + 1u) % KEY_QUEUE_SIZE);
    if (next == g_key_tail) return;
    g_key_queue[g_key_head] = c;
    g_key_head = next;
}

static char keyboard_pop(void) {
    if (g_key_tail == g_key_head) return 0;
    char c = g_key_queue[g_key_tail];
    g_key_tail = (uint8_t)((g_key_tail + 1u) % KEY_QUEUE_SIZE);
    return c;
}

static void keyboard_irq(void) {
    uint8_t sc = in8(0x60u);
    if (sc == 0xE0u) {
        g_ps2_extended = 1u;
        return;
    }
    if (g_ps2_extended) {
        g_ps2_extended = 0u;
        return;
    }
    if (sc & 0x80u) return;
    char c = ps2_decode_make(sc);
    if (c) keyboard_push(c);
}

void osaura_interrupt_dispatch(uint64_t vector, uint64_t error_code) {
    if (vector < 32u) {
        osaura_arch_disable_interrupts();
        serial_text("\nOSAURA EXCEPTION VECTOR ");
        serial_u64(vector);
        serial_text(" ERROR ");
        serial_u64(error_code);
        serial_text("\nCPU HALTED\n");
        for (;;) __asm__ volatile("hlt");
    }

    if (vector >= IRQ_BASE && vector < IRQ_BASE + 16u) {
        uint8_t irq = (uint8_t)(vector - IRQ_BASE);
        if (irq == IRQ_TIMER) ++g_ticks;
        else if (irq == IRQ_KEYBOARD) keyboard_irq();
        pic_eoi(irq);
    }
}

static const efi_memory_descriptor_view *memory_descriptor(uint64_t index) {
    if (!g_boot.memory_descriptor_size) return 0;
    uint64_t count = g_boot.memory_map_size / g_boot.memory_descriptor_size;
    if (index >= count) return 0;
    const uint8_t *base = (const uint8_t *)(uintptr_t)g_boot.memory_map;
    return (const efi_memory_descriptor_view *)(const void *)(base + index * g_boot.memory_descriptor_size);
}

static uint64_t pages_below_minimum(const efi_memory_descriptor_view *desc) {
    if (desc->physical_start >= MIN_ALLOC_PHYS) return 0;
    uint64_t bytes = MIN_ALLOC_PHYS - desc->physical_start;
    uint64_t pages = (bytes + PAGE_SIZE - 1u) / PAGE_SIZE;
    return pages > desc->number_of_pages ? desc->number_of_pages : pages;
}

static void page_allocator_init(void) {
    g_free_pages = 0;
    g_allocated_pages = 0;
    g_alloc_desc_index = 0;
    g_alloc_page_index = 0;
    if (g_boot.memory_descriptor_size < sizeof(efi_memory_descriptor_view)) return;

    uint64_t count = g_boot.memory_map_size / g_boot.memory_descriptor_size;
    for (uint64_t i = 0; i < count; ++i) {
        const efi_memory_descriptor_view *desc = memory_descriptor(i);
        if (!desc || desc->type != EFI_CONVENTIONAL_MEMORY) continue;
        uint64_t skip = pages_below_minimum(desc);
        if (desc->number_of_pages > skip)
            g_free_pages += desc->number_of_pages - skip;
    }
}

static void *page_alloc(void) {
    uint64_t count = g_boot.memory_descriptor_size ?
        g_boot.memory_map_size / g_boot.memory_descriptor_size : 0;

    while (g_alloc_desc_index < count) {
        const efi_memory_descriptor_view *desc = memory_descriptor(g_alloc_desc_index);
        if (!desc || desc->type != EFI_CONVENTIONAL_MEMORY) {
            ++g_alloc_desc_index;
            g_alloc_page_index = 0;
            continue;
        }

        uint64_t skip = pages_below_minimum(desc);
        if (g_alloc_page_index < skip) g_alloc_page_index = skip;
        if (g_alloc_page_index < desc->number_of_pages) {
            uint64_t address = desc->physical_start + g_alloc_page_index * PAGE_SIZE;
            ++g_alloc_page_index;
            if (g_free_pages) --g_free_pages;
            ++g_allocated_pages;
            return (void *)(uintptr_t)address;
        }

        ++g_alloc_desc_index;
        g_alloc_page_index = 0;
    }
    return 0;
}

static int page_allocator_self_test(void) {
    uintptr_t frame = (uintptr_t)page_alloc();
    return frame >= MIN_ALLOC_PHYS && (frame & (PAGE_SIZE - 1u)) == 0u;
}

static void interrupts_init(void) {
    osaura_arch_disable_interrupts();
    serial_text("BOOT: GDT\n");
    osaura_arch_load_gdt();
    serial_text("BOOT: IDT\n");
    idt_init();
    serial_text("BOOT: PIC\n");
    pic_remap();
    serial_text("BOOT: PIT\n");
    pit_init();
    serial_text("BOOT: STI\n");
    osaura_arch_enable_interrupts();
}

static void wait_for_timer_irq(void) {
    uint64_t start = g_ticks;
    while (g_ticks - start < 2u)
        __asm__ volatile("hlt");
}

static void print_prompt(void) { write_text("OSAURA> "); }

static void run_command(const char *line) {
    if (!line[0]) return;
    if (text_equal(line, "HELP")) {
        write_text("HELP ABOUT MEM TICKS ALLOC CLEAR HALT\n");
    } else if (text_equal(line, "ABOUT")) {
        write_text("OSAURA NATIVE X86-64 KERNEL\n");
        write_text("INTERRUPT CORE AND PAGE ALLOCATOR ACTIVE\n");
    } else if (text_equal(line, "MEM")) {
        write_text("MEMORY MAP BYTES: ");
        write_u64(g_boot.memory_map_size);
        write_text("\nDESCRIPTORS: ");
        write_u64(g_boot.memory_descriptor_size ?
                  g_boot.memory_map_size / g_boot.memory_descriptor_size : 0);
        write_text("\nFREE PAGES: ");
        write_u64(g_free_pages);
        write_text("\nALLOCATED PAGES: ");
        write_u64(g_allocated_pages);
        write_text("\n");
    } else if (text_equal(line, "TICKS")) {
        write_text("PIT TICKS: ");
        write_u64(g_ticks);
        write_text("\n");
    } else if (text_equal(line, "ALLOC")) {
        void *page = page_alloc();
        write_text(page ? "PAGE FRAME ALLOCATED\n" : "OUT OF PAGE FRAMES\n");
    } else if (text_equal(line, "CLEAR")) {
        clear_screen();
        serial_text("\nSCREEN CLEARED\n");
    } else if (text_equal(line, "HALT")) {
        write_text("CPU HALTED\n");
        osaura_arch_disable_interrupts();
        for (;;) __asm__ volatile("hlt");
    } else {
        write_text("UNKNOWN COMMAND\n");
    }
}

__attribute__((noreturn)) static void terminal_loop(void) {
    char line[LINE_MAX];
    uint32_t length = 0;
    print_prompt();

    for (;;) {
        char c = keyboard_pop();
        if (!c) {
            __asm__ volatile("hlt");
            continue;
        }
        if (c == '\b') {
            if (length) { --length; erase_char(); }
            continue;
        }
        if (c == '\n') {
            line[length] = 0;
            draw_char('\n');
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
    serial_init();
    if (!boot || boot->version != OSAURA_BOOT_INFO_VERSION || !boot->framebuffer_base) {
        serial_text("OSAURA KERNEL BOOT INFO ERROR\n");
        osaura_arch_disable_interrupts();
        for (;;) __asm__ volatile("hlt");
    }

    g_boot = *boot;
    serial_text("BOOT: INFO OK\n");
    page_allocator_init();
    serial_text("BOOT: FRAME MAP INDEXED\n");
    int allocator_ok = page_allocator_self_test();
    clear_screen();

    interrupts_init();
    serial_text("BOOT: WAIT IRQ0\n");
    wait_for_timer_irq();
    serial_text("BOOT: IRQ0 RECEIVED\n");

    write_text("OSAURA KERNEL 0.3-DEV\n");
    write_text("X86-64 NATIVE MODE\n");
    write_text("UEFI BOOT SERVICES: EXITED\n");
    write_text("FRAMEBUFFER: OWNED\n");
    write_text("MEMORY MAP: CAPTURED\n");
    write_text("GDT: ACTIVE\n");
    write_text("IDT: ACTIVE\n");
    write_text("PIC: REMAPPED 32-47\n");
    write_text("PIT IRQ0: ACTIVE\n");
    write_text("PS2 IRQ1: ACTIVE\n");
    write_text(allocator_ok ? "PAGE ALLOCATOR: ACTIVE\n" : "PAGE ALLOCATOR: FAILED\n");
    write_text("SERIAL COM1: ACTIVE\n\n");
    write_text("TYPE HELP FOR COMMANDS\n\n");
    terminal_loop();
}
