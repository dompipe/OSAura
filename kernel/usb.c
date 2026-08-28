#include "usb.h"

#include <stddef.h>
#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0x0cf8u
#define PCI_CONFIG_DATA 0x0cfcu

#define PCI_CLASS_SERIAL_BUS 0x0cu
#define PCI_SUBCLASS_USB 0x03u
#define PCI_PROGIF_XHCI 0x30u

#define XHCI_USBCMD_RUN 0x00000001u
#define XHCI_USBCMD_HCRST 0x00000002u
#define XHCI_USBSTS_HCH 0x00000001u
#define XHCI_USBSTS_CNR 0x00000800u

#define XHCI_RING_TRBS 256u
#define XHCI_EVENT_TRBS 256u
#define XHCI_MAX_SLOTS 32u
#define USB_KEY_QUEUE 64u

#define XHCI_EXT_CAP_LEGACY 1u
#define XHCI_LEGACY_BIOS_OWNED (1u << 16)
#define XHCI_LEGACY_OS_OWNED (1u << 24)

#define XHCI_INTR_IMAN 0x00u
#define XHCI_INTR_ERSTSZ 0x08u
#define XHCI_INTR_ERSTBA 0x10u
#define XHCI_INTR_ERDP 0x18u

#define XHCI_OP_USBCMD 0x00u
#define XHCI_OP_USBSTS 0x04u
#define XHCI_OP_PAGESIZE 0x08u
#define XHCI_OP_CRCR 0x18u
#define XHCI_OP_DCBAAP 0x30u
#define XHCI_OP_CONFIG 0x38u

#define XHCI_CAP_HCSPARAMS1 0x04u
#define XHCI_CAP_HCCPARAMS1 0x10u
#define XHCI_CAP_DBOFF 0x14u
#define XHCI_CAP_RTSOFF 0x18u

typedef struct __attribute__((packed, aligned(16))) {
    uint32_t parameter_lo;
    uint32_t parameter_hi;
    uint32_t status;
    uint32_t control;
} xhci_trb;

typedef struct __attribute__((packed, aligned(16))) {
    uint64_t ring_base;
    uint32_t ring_size;
    uint32_t reserved;
} xhci_erst_entry;

static volatile uint8_t *g_xhci_mmio;
static volatile uint8_t *g_xhci_op;
static volatile uint8_t *g_xhci_runtime;
static volatile uint32_t *g_xhci_doorbells;
static uint8_t g_xhci_present;
static uint8_t g_xhci_running;
static uint8_t g_keyboard_ready;
static uint8_t g_max_slots;
static uint8_t g_max_ports;

static uint64_t g_dcbaa[XHCI_MAX_SLOTS + 1u] __attribute__((aligned(64)));
static xhci_trb g_command_ring[XHCI_RING_TRBS] __attribute__((aligned(64)));
static xhci_trb g_event_ring[XHCI_EVENT_TRBS] __attribute__((aligned(64)));
static xhci_erst_entry g_erst[1] __attribute__((aligned(64)));
static volatile char g_key_queue[USB_KEY_QUEUE];
static volatile uint8_t g_key_head;
static volatile uint8_t g_key_tail;

static inline uint32_t in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void out32(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline void cpu_pause(void) {
    __asm__ volatile("pause");
}

static uint32_t pci_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return 0x80000000u |
           ((uint32_t)bus << 16) |
           ((uint32_t)device << 11) |
           ((uint32_t)function << 8) |
           (offset & 0xfcu);
}

static uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    out32(PCI_CONFIG_ADDRESS, pci_address(bus, device, function, offset));
    return in32(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    out32(PCI_CONFIG_ADDRESS, pci_address(bus, device, function, offset));
    out32(PCI_CONFIG_DATA, value);
}

static uint16_t pci_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t value = pci_read32(bus, device, function, offset);
    return (uint16_t)(value >> ((offset & 2u) * 8u));
}

static int pci_find_xhci(uint8_t *bus_out, uint8_t *dev_out, uint8_t *fn_out) {
    for (uint16_t bus = 0; bus < 256u; ++bus) {
        for (uint8_t dev = 0; dev < 32u; ++dev) {
            uint32_t id0 = pci_read32((uint8_t)bus, dev, 0u, 0u);
            if ((id0 & 0xffffu) == 0xffffu) continue;
            uint8_t header = (uint8_t)(pci_read32((uint8_t)bus, dev, 0u, 0x0cu) >> 16);
            uint8_t functions = (header & 0x80u) ? 8u : 1u;
            for (uint8_t fn = 0; fn < functions; ++fn) {
                uint32_t id = pci_read32((uint8_t)bus, dev, fn, 0u);
                if ((id & 0xffffu) == 0xffffu) continue;
                uint32_t class_reg = pci_read32((uint8_t)bus, dev, fn, 0x08u);
                uint8_t prog_if = (uint8_t)(class_reg >> 8);
                uint8_t subclass = (uint8_t)(class_reg >> 16);
                uint8_t class_code = (uint8_t)(class_reg >> 24);
                if (class_code == PCI_CLASS_SERIAL_BUS &&
                    subclass == PCI_SUBCLASS_USB &&
                    prog_if == PCI_PROGIF_XHCI) {
                    *bus_out = (uint8_t)bus;
                    *dev_out = dev;
                    *fn_out = fn;
                    return 1;
                }
            }
        }
    }
    return 0;
}

static uint64_t pci_bar0(uint8_t bus, uint8_t dev, uint8_t fn) {
    uint32_t lo = pci_read32(bus, dev, fn, 0x10u);
    if (lo & 1u) return 0u;
    uint32_t type = (lo >> 1) & 3u;
    uint64_t base = (uint64_t)(lo & ~0x0fu);
    if (type == 2u) {
        uint32_t hi = pci_read32(bus, dev, fn, 0x14u);
        base |= (uint64_t)hi << 32;
    }
    return base;
}

static inline uint32_t mmio_read32(volatile uint8_t *base, uint32_t offset) {
    return *(volatile uint32_t *)(base + offset);
}

static inline void mmio_write32(volatile uint8_t *base, uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(base + offset) = value;
}

static inline uint64_t mmio_read64(volatile uint8_t *base, uint32_t offset) {
    uint32_t lo = mmio_read32(base, offset);
    uint32_t hi = mmio_read32(base, offset + 4u);
    return (uint64_t)lo | ((uint64_t)hi << 32);
}

static inline void mmio_write64(volatile uint8_t *base, uint32_t offset, uint64_t value) {
    mmio_write32(base, offset, (uint32_t)value);
    mmio_write32(base, offset + 4u, (uint32_t)(value >> 32));
}

static int wait_mask(volatile uint8_t *base,
                     uint32_t offset,
                     uint32_t mask,
                     uint32_t expected,
                     uint32_t spins) {
    while (spins--) {
        if ((mmio_read32(base, offset) & mask) == expected) return 1;
        cpu_pause();
    }
    return 0;
}

static void zero_bytes(void *ptr, size_t bytes) {
    uint8_t *p = (uint8_t *)ptr;
    while (bytes--) *p++ = 0u;
}

static void xhci_legacy_handoff(void) {
    uint32_t hcc = mmio_read32(g_xhci_mmio, XHCI_CAP_HCCPARAMS1);
    uint32_t ext = ((hcc >> 16) & 0xffffu) * 4u;
    for (uint32_t guard = 0; ext && guard < 64u; ++guard) {
        uint32_t cap = mmio_read32(g_xhci_mmio, ext);
        uint8_t id = (uint8_t)(cap & 0xffu);
        uint8_t next = (uint8_t)((cap >> 8) & 0xffu);
        if (id == XHCI_EXT_CAP_LEGACY) {
            if (cap & XHCI_LEGACY_BIOS_OWNED) {
                mmio_write32(g_xhci_mmio, ext, cap | XHCI_LEGACY_OS_OWNED);
                for (uint32_t spin = 0; spin < 1000000u; ++spin) {
                    uint32_t now = mmio_read32(g_xhci_mmio, ext);
                    if (!(now & XHCI_LEGACY_BIOS_OWNED)) break;
                    cpu_pause();
                }
            }
            return;
        }
        if (!next) break;
        ext += (uint32_t)next * 4u;
    }
}

static int xhci_reset_and_start(void) {
    uint32_t cmd = mmio_read32(g_xhci_op, XHCI_OP_USBCMD);
    cmd &= ~XHCI_USBCMD_RUN;
    mmio_write32(g_xhci_op, XHCI_OP_USBCMD, cmd);
    if (!wait_mask(g_xhci_op, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, XHCI_USBSTS_HCH, 2000000u))
        return 0;

    mmio_write32(g_xhci_op, XHCI_OP_USBCMD, cmd | XHCI_USBCMD_HCRST);
    if (!wait_mask(g_xhci_op, XHCI_OP_USBCMD, XHCI_USBCMD_HCRST, 0u, 2000000u))
        return 0;
    if (!wait_mask(g_xhci_op, XHCI_OP_USBSTS, XHCI_USBSTS_CNR, 0u, 2000000u))
        return 0;

    if (!(mmio_read32(g_xhci_op, XHCI_OP_PAGESIZE) & 1u)) return 0;

    zero_bytes(g_dcbaa, sizeof g_dcbaa);
    zero_bytes(g_command_ring, sizeof g_command_ring);
    zero_bytes(g_event_ring, sizeof g_event_ring);
    zero_bytes(g_erst, sizeof g_erst);

    /* Command ring starts with cycle state 1; reserve final TRB for link later. */
    g_command_ring[0].control = 1u;

    g_erst[0].ring_base = (uint64_t)(uintptr_t)g_event_ring;
    g_erst[0].ring_size = XHCI_EVENT_TRBS;

    mmio_write64(g_xhci_op, XHCI_OP_DCBAAP, (uint64_t)(uintptr_t)g_dcbaa);
    mmio_write64(g_xhci_op, XHCI_OP_CRCR, ((uint64_t)(uintptr_t)g_command_ring) | 1u);

    volatile uint8_t *intr0 = g_xhci_runtime + 0x20u;
    mmio_write32(intr0, XHCI_INTR_ERSTSZ, 1u);
    mmio_write64(intr0, XHCI_INTR_ERSTBA, (uint64_t)(uintptr_t)g_erst);
    mmio_write64(intr0, XHCI_INTR_ERDP, (uint64_t)(uintptr_t)g_event_ring);
    mmio_write32(intr0, XHCI_INTR_IMAN, 0u); /* polling first; IRQ wiring later */

    uint32_t config = mmio_read32(g_xhci_op, XHCI_OP_CONFIG);
    config &= ~0xffu;
    config |= g_max_slots;
    mmio_write32(g_xhci_op, XHCI_OP_CONFIG, config);

    cmd = mmio_read32(g_xhci_op, XHCI_OP_USBCMD);
    mmio_write32(g_xhci_op, XHCI_OP_USBCMD, cmd | XHCI_USBCMD_RUN);
    if (!wait_mask(g_xhci_op, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, 0u, 2000000u))
        return 0;
    return 1;
}

int osaura_usb_init(void) {
    uint8_t bus = 0u, dev = 0u, fn = 0u;
    g_xhci_present = 0u;
    g_xhci_running = 0u;
    g_keyboard_ready = 0u;
    g_key_head = 0u;
    g_key_tail = 0u;

    if (!pci_find_xhci(&bus, &dev, &fn)) return 0;

    uint64_t bar = pci_bar0(bus, dev, fn);
    if (!bar) return 0;

    uint16_t command = pci_read16(bus, dev, fn, 0x04u);
    uint32_t command_status = pci_read32(bus, dev, fn, 0x04u);
    command |= 0x0006u; /* memory space + bus master */
    command_status = (command_status & 0xffff0000u) | command;
    pci_write32(bus, dev, fn, 0x04u, command_status);

    g_xhci_mmio = (volatile uint8_t *)(uintptr_t)bar;
    g_xhci_present = 1u;

    uint8_t cap_length = g_xhci_mmio[0];
    uint32_t hcs1 = mmio_read32(g_xhci_mmio, XHCI_CAP_HCSPARAMS1);
    g_max_slots = (uint8_t)(hcs1 & 0xffu);
    if (!g_max_slots || g_max_slots > XHCI_MAX_SLOTS) g_max_slots = XHCI_MAX_SLOTS;
    g_max_ports = (uint8_t)((hcs1 >> 24) & 0xffu);

    uint32_t dboff = mmio_read32(g_xhci_mmio, XHCI_CAP_DBOFF) & ~3u;
    uint32_t rtsoff = mmio_read32(g_xhci_mmio, XHCI_CAP_RTSOFF) & ~0x1fu;
    g_xhci_op = g_xhci_mmio + cap_length;
    g_xhci_doorbells = (volatile uint32_t *)(g_xhci_mmio + dboff);
    g_xhci_runtime = g_xhci_mmio + rtsoff;

    xhci_legacy_handoff();
    if (!xhci_reset_and_start()) return 0;

    g_xhci_running = 1u;
    return 1;
}

void osaura_usb_poll(void) {
    if (!g_xhci_running) return;

    /*
     * Controller ownership and rings are live here.  HID device enumeration
     * and interrupt-IN consumption are the next layer; leave the controller
     * running so that work can be added without changing terminal callers.
     */
    (void)g_xhci_doorbells;
    (void)g_max_ports;
    (void)mmio_read64;
}

int osaura_usb_xhci_present(void) {
    return g_xhci_present != 0u;
}

int osaura_usb_keyboard_ready(void) {
    return g_keyboard_ready != 0u;
}

char osaura_usb_keyboard_pop(void) {
    if (g_key_tail == g_key_head) return 0;
    char c = g_key_queue[g_key_tail];
    g_key_tail = (uint8_t)((g_key_tail + 1u) % USB_KEY_QUEUE);
    return c;
}
