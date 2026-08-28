#undef osaura_usb_keyboard_init

extern int osaura_usb_keyboard_init_legacy(void);
extern uint32_t osaura_usb_xhci_preflight(void);

#define USB_DIAG_COM1 0x3F8u

static inline uint8_t usb_diag_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void usb_diag_out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void usb_diag_char(char c) {
    while (!(usb_diag_in8(USB_DIAG_COM1 + 5u) & 0x20u))
        __asm__ volatile("pause");
    usb_diag_out8(USB_DIAG_COM1, (uint8_t)c);
}

static void usb_diag_text(const char *text) {
    while (*text) usb_diag_char(*text++);
}

int osaura_usb_keyboard_init(void) {
    uint32_t stage = osaura_usb_xhci_preflight();
    usb_diag_text("BOOT: USB XHCI PREFLIGHT ");
    usb_diag_char((char)('0' + (stage % 10u)));
    usb_diag_text("\r\n");
    return osaura_usb_keyboard_init_legacy();
}
