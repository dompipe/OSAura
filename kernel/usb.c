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
#define XHCI_EXT_CAP_LEGACY 1u
#define XHCI_LEGACY_BIOS_OWNED (1u << 16)
#define XHCI_LEGACY_OS_OWNED (1u << 24)

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
#define XHCI_INTR_IMAN 0x00u
#define XHCI_INTR_ERSTSZ 0x08u
#define XHCI_INTR_ERSTBA 0x10u
#define XHCI_INTR_ERDP 0x18u
#define XHCI_PORTSC_BASE 0x400u
#define XHCI_PORTSC_STRIDE 0x10u
#define XHCI_PORT_CCS (1u << 0)
#define XHCI_PORT_PED (1u << 1)
#define XHCI_PORT_PR (1u << 4)
#define XHCI_PORT_SPEED_SHIFT 10u
#define XHCI_PORT_SPEED_MASK 0x0fu
#define XHCI_PORT_CHANGE_MASK ((1u << 17) | (1u << 18) | (1u << 20) | (1u << 21) | (1u << 22) | (1u << 23))

#define XHCI_TRB_CYCLE 0x00000001u
#define XHCI_TRB_TC (1u << 1)
#define XHCI_TRB_ISP (1u << 2)
#define XHCI_TRB_IOC (1u << 5)
#define XHCI_TRB_IDT (1u << 6)
#define XHCI_TRB_DIR_IN (1u << 16)
#define XHCI_TRB_TYPE_SHIFT 10u
#define XHCI_TRB_TYPE_NORMAL 1u
#define XHCI_TRB_TYPE_SETUP 2u
#define XHCI_TRB_TYPE_DATA 3u
#define XHCI_TRB_TYPE_STATUS 4u
#define XHCI_TRB_TYPE_LINK 6u
#define XHCI_TRB_TYPE_ENABLE_SLOT 9u
#define XHCI_TRB_TYPE_ADDRESS_DEVICE 11u
#define XHCI_TRB_TYPE_CONFIGURE_ENDPOINT 12u
#define XHCI_TRB_TYPE_TRANSFER_EVENT 32u
#define XHCI_TRB_TYPE_COMMAND_COMPLETION 33u
#define XHCI_TRB_TYPE_PORT_STATUS 34u
#define XHCI_COMPLETION_SUCCESS 1u
#define XHCI_COMPLETION_SHORT_PACKET 13u

#define XHCI_RING_TRBS 64u
#define XHCI_EVENT_TRBS 128u
#define XHCI_MAX_SLOTS 32u
#define XHCI_CONTEXT_BYTES 4096u
#define USB_EVENT_QUEUE 64u
#define USB_CONFIG_BUFFER 512u
#define USB_HID_REPORT_BYTES 8u

#define USB_REQ_GET_DESCRIPTOR 0x06u
#define USB_REQ_SET_CONFIGURATION 0x09u
#define USB_REQ_SET_PROTOCOL 0x0bu
#define USB_DESC_DEVICE 1u
#define USB_DESC_CONFIGURATION 2u
#define USB_DESC_INTERFACE 4u
#define USB_DESC_ENDPOINT 5u
#define USB_CLASS_HID 3u
#define USB_HID_SUBCLASS_BOOT 1u
#define USB_HID_PROTOCOL_KEYBOARD 1u
#define USB_ENDPOINT_IN 0x80u
#define USB_ENDPOINT_XFER_INTERRUPT 3u

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

typedef struct {
    uint8_t address;
    uint8_t attributes;
    uint16_t max_packet;
    uint8_t interval;
    uint8_t interface_number;
    uint8_t dci;
} hid_endpoint;

static volatile uint8_t *g_xhci_mmio;
static volatile uint8_t *g_xhci_op;
static volatile uint8_t *g_xhci_runtime;
static volatile uint32_t *g_xhci_doorbells;
static uint8_t g_xhci_present;
static uint8_t g_xhci_running;
static uint8_t g_keyboard_ready;
static uint8_t g_max_slots;
static uint8_t g_max_ports;
static uint8_t g_context_size;
static uint8_t g_slot_id;
static uint8_t g_root_port;
static uint8_t g_port_speed;
static uint8_t g_intr_pending;
static uint8_t g_intr_dci;

static uint64_t g_dcbaa[XHCI_MAX_SLOTS + 1u] __attribute__((aligned(64)));
static xhci_trb g_command_ring[XHCI_RING_TRBS] __attribute__((aligned(64)));
static xhci_trb g_event_ring[XHCI_EVENT_TRBS] __attribute__((aligned(64)));
static xhci_trb g_ep0_ring[XHCI_RING_TRBS] __attribute__((aligned(64)));
static xhci_trb g_intr_ring[XHCI_RING_TRBS] __attribute__((aligned(64)));
static xhci_erst_entry g_erst[1] __attribute__((aligned(64)));
static uint8_t g_output_context[XHCI_CONTEXT_BYTES] __attribute__((aligned(64)));
static uint8_t g_input_context[XHCI_CONTEXT_BYTES] __attribute__((aligned(64)));
static uint8_t g_control_buffer[USB_CONFIG_BUFFER] __attribute__((aligned(64)));
static uint8_t g_intr_report[USB_HID_REPORT_BYTES] __attribute__((aligned(64)));
static uint8_t g_previous_report[USB_HID_REPORT_BYTES] __attribute__((aligned(64)));
static osaura_key_event g_event_queue[USB_EVENT_QUEUE];
static volatile uint8_t g_event_head;
static volatile uint8_t g_event_tail;

static uint16_t g_command_enqueue;
static uint8_t g_command_cycle;
static uint16_t g_event_dequeue;
static uint8_t g_event_cycle;
static uint16_t g_ep0_enqueue;
static uint8_t g_ep0_cycle;
static uint16_t g_intr_enqueue;
static uint8_t g_intr_cycle;

static inline uint32_t in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void out32(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline void cpu_pause(void) { __asm__ volatile("pause"); }

static void zero_bytes(void *ptr, size_t bytes) {
    uint8_t *p = (uint8_t *)ptr;
    while (bytes--) *p++ = 0u;
}

static void copy_bytes(void *target, const void *source, size_t bytes) {
    uint8_t *out = (uint8_t *)target;
    const uint8_t *in = (const uint8_t *)source;
    while (bytes--) *out++ = *in++;
}

static int bytes_equal(const uint8_t *a, const uint8_t *b, size_t bytes) {
    while (bytes--) if (*a++ != *b++) return 0;
    return 1;
}

static uint32_t pci_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)device << 11) |
           ((uint32_t)function << 8) | (offset & 0xfcu);
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
                if ((uint8_t)(class_reg >> 24) == PCI_CLASS_SERIAL_BUS &&
                    (uint8_t)(class_reg >> 16) == PCI_SUBCLASS_USB &&
                    (uint8_t)(class_reg >> 8) == PCI_PROGIF_XHCI) {
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
    if (type == 2u) base |= (uint64_t)pci_read32(bus, dev, fn, 0x14u) << 32;
    return base;
}

static inline uint32_t mmio_read32(volatile uint8_t *base, uint32_t offset) {
    return *(volatile uint32_t *)(base + offset);
}

static inline void mmio_write32(volatile uint8_t *base, uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(base + offset) = value;
}

static inline void mmio_write64(volatile uint8_t *base, uint32_t offset, uint64_t value) {
    mmio_write32(base, offset, (uint32_t)value);
    mmio_write32(base, offset + 4u, (uint32_t)(value >> 32));
}

static int wait_mask(volatile uint8_t *base, uint32_t offset, uint32_t mask,
                     uint32_t expected, uint32_t spins) {
    while (spins--) {
        if ((mmio_read32(base, offset) & mask) == expected) return 1;
        cpu_pause();
    }
    return 0;
}

static uint32_t trb_type(const xhci_trb *trb) {
    return (trb->control >> XHCI_TRB_TYPE_SHIFT) & 0x3fu;
}

static uint32_t completion_code(const xhci_trb *trb) {
    return (trb->status >> 24) & 0xffu;
}

static void ring_init(xhci_trb *ring, uint16_t *enqueue, uint8_t *cycle) {
    zero_bytes(ring, sizeof(xhci_trb) * XHCI_RING_TRBS);
    *enqueue = 0u;
    *cycle = 1u;
    xhci_trb *link = &ring[XHCI_RING_TRBS - 1u];
    uint64_t base = (uint64_t)(uintptr_t)ring;
    link->parameter_lo = (uint32_t)base;
    link->parameter_hi = (uint32_t)(base >> 32);
    link->control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_TC | XHCI_TRB_CYCLE;
}

static xhci_trb *ring_reserve(xhci_trb *ring, uint16_t *enqueue, uint8_t *cycle) {
    if (*enqueue >= XHCI_RING_TRBS - 1u) {
        xhci_trb *link = &ring[XHCI_RING_TRBS - 1u];
        link->control &= ~XHCI_TRB_CYCLE;
        link->control |= *cycle ? XHCI_TRB_CYCLE : 0u;
        *enqueue = 0u;
        *cycle ^= 1u;
    }
    xhci_trb *trb = &ring[*enqueue];
    zero_bytes(trb, sizeof *trb);
    ++*enqueue;
    return trb;
}

static void trb_commit(xhci_trb *trb, uint32_t control, uint8_t cycle) {
    __asm__ volatile("" ::: "memory");
    trb->control = control | (cycle ? XHCI_TRB_CYCLE : 0u);
    __asm__ volatile("" ::: "memory");
}

static int event_take(xhci_trb *out) {
    xhci_trb *event = &g_event_ring[g_event_dequeue];
    if ((event->control & XHCI_TRB_CYCLE) != (g_event_cycle ? XHCI_TRB_CYCLE : 0u)) return 0;
    *out = *event;
    ++g_event_dequeue;
    if (g_event_dequeue == XHCI_EVENT_TRBS) {
        g_event_dequeue = 0u;
        g_event_cycle ^= 1u;
    }
    volatile uint8_t *intr0 = g_xhci_runtime + 0x20u;
    uint64_t dequeue = (uint64_t)(uintptr_t)&g_event_ring[g_event_dequeue];
    mmio_write64(intr0, XHCI_INTR_ERDP, dequeue | (1ull << 3));
    return 1;
}

static int wait_event_type(uint32_t wanted, xhci_trb *out, uint32_t spins) {
    while (spins--) {
        xhci_trb event;
        if (!event_take(&event)) { cpu_pause(); continue; }
        uint32_t type = trb_type(&event);
        if (type == wanted) { if (out) *out = event; return 1; }
        if (type == XHCI_TRB_TYPE_PORT_STATUS) continue;
    }
    return 0;
}

static int command_submit(uint64_t parameter, uint32_t control, xhci_trb *completion) {
    uint8_t cycle = g_command_cycle;
    xhci_trb *trb = ring_reserve(g_command_ring, &g_command_enqueue, &g_command_cycle);
    trb->parameter_lo = (uint32_t)parameter;
    trb->parameter_hi = (uint32_t)(parameter >> 32);
    trb_commit(trb, control, cycle);
    g_xhci_doorbells[0] = 0u;
    if (!wait_event_type(XHCI_TRB_TYPE_COMMAND_COMPLETION, completion, 5000000u)) return 0;
    return completion_code(completion) == XHCI_COMPLETION_SUCCESS;
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
    uint32_t cmd = mmio_read32(g_xhci_op, XHCI_OP_USBCMD) & ~XHCI_USBCMD_RUN;
    mmio_write32(g_xhci_op, XHCI_OP_USBCMD, cmd);
    if (!wait_mask(g_xhci_op, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, XHCI_USBSTS_HCH, 2000000u)) return 0;
    mmio_write32(g_xhci_op, XHCI_OP_USBCMD, cmd | XHCI_USBCMD_HCRST);
    if (!wait_mask(g_xhci_op, XHCI_OP_USBCMD, XHCI_USBCMD_HCRST, 0u, 2000000u)) return 0;
    if (!wait_mask(g_xhci_op, XHCI_OP_USBSTS, XHCI_USBSTS_CNR, 0u, 2000000u)) return 0;
    if (!(mmio_read32(g_xhci_op, XHCI_OP_PAGESIZE) & 1u)) return 0;

    zero_bytes(g_dcbaa, sizeof g_dcbaa);
    zero_bytes(g_event_ring, sizeof g_event_ring);
    zero_bytes(g_erst, sizeof g_erst);
    ring_init(g_command_ring, &g_command_enqueue, &g_command_cycle);
    ring_init(g_ep0_ring, &g_ep0_enqueue, &g_ep0_cycle);
    ring_init(g_intr_ring, &g_intr_enqueue, &g_intr_cycle);
    g_event_dequeue = 0u;
    g_event_cycle = 1u;

    g_erst[0].ring_base = (uint64_t)(uintptr_t)g_event_ring;
    g_erst[0].ring_size = XHCI_EVENT_TRBS;
    mmio_write64(g_xhci_op, XHCI_OP_DCBAAP, (uint64_t)(uintptr_t)g_dcbaa);
    mmio_write64(g_xhci_op, XHCI_OP_CRCR, ((uint64_t)(uintptr_t)g_command_ring) | 1u);

    volatile uint8_t *intr0 = g_xhci_runtime + 0x20u;
    mmio_write32(intr0, XHCI_INTR_ERSTSZ, 1u);
    mmio_write64(intr0, XHCI_INTR_ERSTBA, (uint64_t)(uintptr_t)g_erst);
    mmio_write64(intr0, XHCI_INTR_ERDP, (uint64_t)(uintptr_t)g_event_ring);
    mmio_write32(intr0, XHCI_INTR_IMAN, 0u);

    uint32_t config = mmio_read32(g_xhci_op, XHCI_OP_CONFIG) & ~0xffu;
    mmio_write32(g_xhci_op, XHCI_OP_CONFIG, config | g_max_slots);
    mmio_write32(g_xhci_op, XHCI_OP_USBCMD, mmio_read32(g_xhci_op, XHCI_OP_USBCMD) | XHCI_USBCMD_RUN);
    return wait_mask(g_xhci_op, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, 0u, 2000000u);
}

static uint32_t port_offset(uint8_t port) {
    return XHCI_PORTSC_BASE + ((uint32_t)port - 1u) * XHCI_PORTSC_STRIDE;
}

static int root_port_reset(uint8_t port, uint8_t *speed_out) {
    uint32_t offset = port_offset(port);
    uint32_t ps = mmio_read32(g_xhci_op, offset);
    if (!(ps & XHCI_PORT_CCS)) return 0;
    uint32_t write = ps & ~XHCI_PORT_CHANGE_MASK;
    mmio_write32(g_xhci_op, offset, write | XHCI_PORT_PR);
    for (uint32_t spin = 0; spin < 5000000u; ++spin) {
        ps = mmio_read32(g_xhci_op, offset);
        if (!(ps & XHCI_PORT_PR) && (ps & XHCI_PORT_PED)) {
            *speed_out = (uint8_t)((ps >> XHCI_PORT_SPEED_SHIFT) & XHCI_PORT_SPEED_MASK);
            if (ps & XHCI_PORT_CHANGE_MASK) mmio_write32(g_xhci_op, offset, ps & XHCI_PORT_CHANGE_MASK);
            return 1;
        }
        cpu_pause();
    }
    return 0;
}

static uint16_t initial_ep0_mps(uint8_t speed) {
    if (speed == 4u) return 512u;
    if (speed == 3u) return 64u;
    return 8u;
}

static uint32_t *context_dwords(uint8_t *base, uint32_t index) {
    return (uint32_t *)(void *)(base + (size_t)index * g_context_size);
}

static void prepare_address_context(uint8_t port, uint8_t speed, uint16_t ep0_mps) {
    zero_bytes(g_input_context, sizeof g_input_context);
    uint32_t *icc = (uint32_t *)(void *)g_input_context;
    uint32_t *slot = context_dwords(g_input_context, 1u);
    uint32_t *ep0 = context_dwords(g_input_context, 2u);
    icc[1] = 0x3u;
    slot[0] = ((uint32_t)speed << 20) | (1u << 27);
    slot[1] = (uint32_t)port << 16;
    uint64_t ring = (uint64_t)(uintptr_t)g_ep0_ring;
    ep0[1] = (3u << 1) | (4u << 3) | ((uint32_t)ep0_mps << 16);
    ep0[2] = (uint32_t)(ring | 1u);
    ep0[3] = (uint32_t)(ring >> 32);
    ep0[4] = 8u;
}

static int enable_and_address(uint8_t port, uint8_t speed) {
    xhci_trb event;
    if (!command_submit(0u, XHCI_TRB_TYPE_ENABLE_SLOT << XHCI_TRB_TYPE_SHIFT, &event)) return 0;
    g_slot_id = (uint8_t)(event.control >> 24);
    if (!g_slot_id || g_slot_id > g_max_slots) return 0;

    zero_bytes(g_output_context, sizeof g_output_context);
    g_dcbaa[g_slot_id] = (uint64_t)(uintptr_t)g_output_context;
    prepare_address_context(port, speed, initial_ep0_mps(speed));
    uint32_t control = (XHCI_TRB_TYPE_ADDRESS_DEVICE << XHCI_TRB_TYPE_SHIFT) |
                       ((uint32_t)g_slot_id << 24);
    return command_submit((uint64_t)(uintptr_t)g_input_context, control, &event);
}

static uint64_t setup_packet(uint8_t request_type, uint8_t request, uint16_t value,
                             uint16_t index, uint16_t length) {
    return (uint64_t)request_type |
           ((uint64_t)request << 8) |
           ((uint64_t)value << 16) |
           ((uint64_t)index << 32) |
           ((uint64_t)length << 48);
}

static int control_transfer(uint8_t request_type, uint8_t request, uint16_t value,
                            uint16_t index, void *buffer, uint16_t length) {
    uint64_t setup = setup_packet(request_type, request, value, index, length);
    uint8_t cycle = g_ep0_cycle;
    xhci_trb *stage = ring_reserve(g_ep0_ring, &g_ep0_enqueue, &g_ep0_cycle);
    stage->parameter_lo = (uint32_t)setup;
    stage->parameter_hi = (uint32_t)(setup >> 32);
    stage->status = 8u;
    uint32_t trt = length ? ((request_type & 0x80u) ? 3u : 2u) : 0u;
    trb_commit(stage, (XHCI_TRB_TYPE_SETUP << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IDT | (trt << 16), cycle);

    if (length) {
        cycle = g_ep0_cycle;
        stage = ring_reserve(g_ep0_ring, &g_ep0_enqueue, &g_ep0_cycle);
        uint64_t data = (uint64_t)(uintptr_t)buffer;
        stage->parameter_lo = (uint32_t)data;
        stage->parameter_hi = (uint32_t)(data >> 32);
        stage->status = length;
        uint32_t flags = (XHCI_TRB_TYPE_DATA << XHCI_TRB_TYPE_SHIFT);
        if (request_type & 0x80u) flags |= XHCI_TRB_DIR_IN;
        trb_commit(stage, flags, cycle);
    }

    cycle = g_ep0_cycle;
    stage = ring_reserve(g_ep0_ring, &g_ep0_enqueue, &g_ep0_cycle);
    uint32_t status_flags = (XHCI_TRB_TYPE_STATUS << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC;
    if (!(request_type & 0x80u) || !length) status_flags |= XHCI_TRB_DIR_IN;
    trb_commit(stage, status_flags, cycle);
    g_xhci_doorbells[g_slot_id] = 1u;

    xhci_trb event;
    if (!wait_event_type(XHCI_TRB_TYPE_TRANSFER_EVENT, &event, 5000000u)) return 0;
    uint32_t cc = completion_code(&event);
    return cc == XHCI_COMPLETION_SUCCESS || cc == XHCI_COMPLETION_SHORT_PACKET;
}

static int get_descriptor(uint8_t type, uint8_t index, void *buffer, uint16_t length) {
    zero_bytes(buffer, length);
    return control_transfer(0x80u, USB_REQ_GET_DESCRIPTOR,
                            (uint16_t)(((uint16_t)type << 8) | index), 0u, buffer, length);
}

static int parse_hid_keyboard(const uint8_t *config, size_t bytes, hid_endpoint *hid) {
    uint8_t current_interface = 0xffu;
    uint8_t keyboard_interface = 0xffu;
    size_t offset = 0u;
    while (offset + 2u <= bytes) {
        uint8_t length = config[offset];
        uint8_t type = config[offset + 1u];
        if (length < 2u || offset + length > bytes) break;
        if (type == USB_DESC_INTERFACE && length >= 9u) {
            current_interface = config[offset + 2u];
            uint8_t klass = config[offset + 5u];
            uint8_t subclass = config[offset + 6u];
            uint8_t protocol = config[offset + 7u];
            keyboard_interface = (klass == USB_CLASS_HID && subclass == USB_HID_SUBCLASS_BOOT &&
                                  protocol == USB_HID_PROTOCOL_KEYBOARD) ? current_interface : 0xffu;
        } else if (type == USB_DESC_ENDPOINT && length >= 7u && keyboard_interface == current_interface) {
            uint8_t address = config[offset + 2u];
            uint8_t attributes = config[offset + 3u];
            if ((address & USB_ENDPOINT_IN) && (attributes & 3u) == USB_ENDPOINT_XFER_INTERRUPT) {
                hid->address = address;
                hid->attributes = attributes;
                hid->max_packet = (uint16_t)config[offset + 4u] | ((uint16_t)config[offset + 5u] << 8);
                hid->max_packet &= 0x07ffu;
                hid->interval = config[offset + 6u];
                hid->interface_number = keyboard_interface;
                uint8_t ep = address & 0x0fu;
                hid->dci = (uint8_t)(ep * 2u + 1u);
                return hid->dci > 1u;
            }
        }
        offset += length;
    }
    return 0;
}

static uint8_t interrupt_interval(uint8_t speed, uint8_t b_interval) {
    if (!b_interval) return 0u;
    if (speed >= 3u) return b_interval > 16u ? 15u : (uint8_t)(b_interval - 1u);
    uint32_t microframes = (uint32_t)b_interval * 8u;
    uint8_t exponent = 0u;
    uint32_t value = 1u;
    while (value < microframes && exponent < 15u) { value <<= 1u; ++exponent; }
    return exponent;
}

static int configure_interrupt_endpoint(const hid_endpoint *hid) {
    zero_bytes(g_input_context, sizeof g_input_context);
    uint32_t *icc = (uint32_t *)(void *)g_input_context;
    uint32_t *slot_in = context_dwords(g_input_context, 1u);
    const uint32_t *slot_out = (const uint32_t *)(const void *)g_output_context;
    copy_bytes(slot_in, slot_out, g_context_size);
    slot_in[0] &= ~(0x1fu << 27);
    slot_in[0] |= (uint32_t)hid->dci << 27;
    icc[1] = 1u | (1u << hid->dci);

    uint32_t *ep = context_dwords(g_input_context, (uint32_t)hid->dci + 1u);
    uint64_t ring = (uint64_t)(uintptr_t)g_intr_ring;
    ep[0] = (uint32_t)interrupt_interval(g_port_speed, hid->interval) << 16;
    ep[1] = (3u << 1) | (7u << 3) | ((uint32_t)hid->max_packet << 16);
    ep[2] = (uint32_t)(ring | 1u);
    ep[3] = (uint32_t)(ring >> 32);
    ep[4] = hid->max_packet;

    xhci_trb event;
    uint32_t control = (XHCI_TRB_TYPE_CONFIGURE_ENDPOINT << XHCI_TRB_TYPE_SHIFT) |
                       ((uint32_t)g_slot_id << 24);
    return command_submit((uint64_t)(uintptr_t)g_input_context, control, &event);
}

static char usage_to_char(uint8_t usage, uint8_t modifiers) {
    int shift = (modifiers & OSAURA_KEY_MOD_SHIFT) != 0u;
    if (usage >= 0x04u && usage <= 0x1du) {
        char c = (char)('A' + (usage - 0x04u));
        return shift ? c : c;
    }
    if (usage >= 0x1eu && usage <= 0x26u) {
        static const char normal[] = "123456789";
        static const char shifted[] = "!@#$%^&*(";
        return shift ? shifted[usage - 0x1eu] : normal[usage - 0x1eu];
    }
    if (usage == 0x27u) return shift ? ')' : '0';
    if (usage == OSAURA_KEY_ENTER) return '\n';
    if (usage == OSAURA_KEY_BACKSPACE) return '\b';
    if (usage == OSAURA_KEY_TAB) return '\t';
    if (usage == 0x2cu) return ' ';
    if (usage == 0x2du) return shift ? '_' : '-';
    if (usage == 0x2eu) return shift ? '+' : '=';
    if (usage == 0x36u) return shift ? '<' : ',';
    if (usage == 0x37u) return shift ? '>' : '.';
    if (usage == 0x38u) return shift ? '?' : '/';
    return 0;
}

static uint8_t hid_modifiers(uint8_t hid) {
    uint8_t out = 0u;
    if (hid & ((1u << 0) | (1u << 4))) out |= OSAURA_KEY_MOD_CTRL;
    if (hid & ((1u << 1) | (1u << 5))) out |= OSAURA_KEY_MOD_SHIFT;
    if (hid & ((1u << 2) | (1u << 6))) out |= OSAURA_KEY_MOD_ALT;
    if (hid & ((1u << 3) | (1u << 7))) out |= OSAURA_KEY_MOD_GUI;
    return out;
}

static int report_has_usage(const uint8_t report[USB_HID_REPORT_BYTES], uint8_t usage) {
    for (uint32_t i = 2u; i < USB_HID_REPORT_BYTES; ++i)
        if (report[i] == usage) return 1;
    return 0;
}

static void event_push(uint8_t usage, uint8_t modifiers, uint8_t pressed) {
    uint8_t next = (uint8_t)((g_event_head + 1u) % USB_EVENT_QUEUE);
    if (next == g_event_tail) return;
    osaura_key_event *event = &g_event_queue[g_event_head];
    event->usage = usage;
    event->modifiers = modifiers;
    event->pressed = pressed;
    event->character = pressed ? usage_to_char(usage, modifiers) : 0;
    g_event_head = next;
}

static void process_report(void) {
    uint8_t modifiers = hid_modifiers(g_intr_report[0]);
    for (uint32_t i = 2u; i < USB_HID_REPORT_BYTES; ++i) {
        uint8_t usage = g_intr_report[i];
        if (usage && !report_has_usage(g_previous_report, usage)) event_push(usage, modifiers, 1u);
    }
    for (uint32_t i = 2u; i < USB_HID_REPORT_BYTES; ++i) {
        uint8_t usage = g_previous_report[i];
        if (usage && !report_has_usage(g_intr_report, usage)) event_push(usage, modifiers, 0u);
    }
    copy_bytes(g_previous_report, g_intr_report, USB_HID_REPORT_BYTES);
}

static void submit_interrupt_report(void) {
    if (!g_keyboard_ready || g_intr_pending) return;
    zero_bytes(g_intr_report, sizeof g_intr_report);
    uint8_t cycle = g_intr_cycle;
    xhci_trb *trb = ring_reserve(g_intr_ring, &g_intr_enqueue, &g_intr_cycle);
    uint64_t buffer = (uint64_t)(uintptr_t)g_intr_report;
    trb->parameter_lo = (uint32_t)buffer;
    trb->parameter_hi = (uint32_t)(buffer >> 32);
    trb->status = USB_HID_REPORT_BYTES;
    trb_commit(trb, (XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC | XHCI_TRB_ISP, cycle);
    g_intr_pending = 1u;
    g_xhci_doorbells[g_slot_id] = g_intr_dci;
}

static void poll_interrupt_report(void) {
    if (!g_intr_pending) { submit_interrupt_report(); return; }
    xhci_trb event;
    while (event_take(&event)) {
        uint32_t type = trb_type(&event);
        if (type == XHCI_TRB_TYPE_PORT_STATUS) continue;
        if (type != XHCI_TRB_TYPE_TRANSFER_EVENT) continue;
        if ((uint8_t)(event.control >> 24) != g_slot_id) continue;
        uint32_t endpoint_id = (event.control >> 16) & 0x1fu;
        if (endpoint_id != g_intr_dci) continue;
        g_intr_pending = 0u;
        uint32_t cc = completion_code(&event);
        if (cc == XHCI_COMPLETION_SUCCESS || cc == XHCI_COMPLETION_SHORT_PACKET) process_report();
        submit_interrupt_report();
        return;
    }
}

static int enumerate_keyboard(void) {
    g_root_port = 0u;
    for (uint8_t port = 1u; port <= g_max_ports; ++port) {
        uint32_t ps = mmio_read32(g_xhci_op, port_offset(port));
        if (!(ps & XHCI_PORT_CCS)) continue;
        uint8_t speed = 0u;
        if (!root_port_reset(port, &speed)) continue;
        g_root_port = port;
        g_port_speed = speed;
        break;
    }
    if (!g_root_port || !enable_and_address(g_root_port, g_port_speed)) return 0;

    if (!get_descriptor(USB_DESC_DEVICE, 0u, g_control_buffer, 18u)) return 0;
    if (g_control_buffer[0] < 8u || g_control_buffer[1] != USB_DESC_DEVICE) return 0;

    if (!get_descriptor(USB_DESC_CONFIGURATION, 0u, g_control_buffer, 9u)) return 0;
    if (g_control_buffer[0] < 9u || g_control_buffer[1] != USB_DESC_CONFIGURATION) return 0;
    uint16_t total = (uint16_t)g_control_buffer[2] | ((uint16_t)g_control_buffer[3] << 8);
    if (total < 9u || total > USB_CONFIG_BUFFER) return 0;
    if (!get_descriptor(USB_DESC_CONFIGURATION, 0u, g_control_buffer, total)) return 0;

    hid_endpoint hid;
    zero_bytes(&hid, sizeof hid);
    if (!parse_hid_keyboard(g_control_buffer, total, &hid)) return 0;
    uint8_t configuration = g_control_buffer[5];
    if (!configuration) return 0;
    if (!control_transfer(0x00u, USB_REQ_SET_CONFIGURATION, configuration, 0u, NULL, 0u)) return 0;
    if (!control_transfer(0x21u, USB_REQ_SET_PROTOCOL, 0u, hid.interface_number, NULL, 0u)) return 0;
    if (!configure_interrupt_endpoint(&hid)) return 0;

    g_intr_dci = hid.dci;
    zero_bytes(g_previous_report, sizeof g_previous_report);
    g_keyboard_ready = 1u;
    submit_interrupt_report();
    return 1;
}

int osaura_usb_init(void) {
    uint8_t bus = 0u, dev = 0u, fn = 0u;
    g_xhci_present = 0u;
    g_xhci_running = 0u;
    g_keyboard_ready = 0u;
    g_event_head = 0u;
    g_event_tail = 0u;
    g_intr_pending = 0u;

    if (!pci_find_xhci(&bus, &dev, &fn)) return 0;
    uint64_t bar = pci_bar0(bus, dev, fn);
    if (!bar) return 0;

    uint16_t command = pci_read16(bus, dev, fn, 0x04u) | 0x0006u;
    uint32_t command_status = pci_read32(bus, dev, fn, 0x04u);
    pci_write32(bus, dev, fn, 0x04u, (command_status & 0xffff0000u) | command);

    g_xhci_mmio = (volatile uint8_t *)(uintptr_t)bar;
    g_xhci_present = 1u;
    uint8_t cap_length = g_xhci_mmio[0];
    uint32_t hcs1 = mmio_read32(g_xhci_mmio, XHCI_CAP_HCSPARAMS1);
    uint32_t hcc1 = mmio_read32(g_xhci_mmio, XHCI_CAP_HCCPARAMS1);
    g_max_slots = (uint8_t)(hcs1 & 0xffu);
    if (!g_max_slots || g_max_slots > XHCI_MAX_SLOTS) g_max_slots = XHCI_MAX_SLOTS;
    g_max_ports = (uint8_t)((hcs1 >> 24) & 0xffu);
    g_context_size = (hcc1 & (1u << 2)) ? 64u : 32u;
    uint32_t dboff = mmio_read32(g_xhci_mmio, XHCI_CAP_DBOFF) & ~3u;
    uint32_t rtsoff = mmio_read32(g_xhci_mmio, XHCI_CAP_RTSOFF) & ~0x1fu;
    g_xhci_op = g_xhci_mmio + cap_length;
    g_xhci_doorbells = (volatile uint32_t *)(g_xhci_mmio + dboff);
    g_xhci_runtime = g_xhci_mmio + rtsoff;

    xhci_legacy_handoff();
    if (!xhci_reset_and_start()) return 0;
    g_xhci_running = 1u;
    (void)enumerate_keyboard();
    return 1;
}

void osaura_usb_poll(void) {
    if (!g_xhci_running || !g_keyboard_ready) return;
    poll_interrupt_report();
}

int osaura_usb_xhci_present(void) { return g_xhci_present != 0u; }
int osaura_usb_keyboard_ready(void) { return g_keyboard_ready != 0u; }

int osaura_usb_keyboard_event_pop(osaura_key_event *event) {
    if (!event || g_event_tail == g_event_head) return 0;
    *event = g_event_queue[g_event_tail];
    g_event_tail = (uint8_t)((g_event_tail + 1u) % USB_EVENT_QUEUE);
    return 1;
}

char osaura_usb_keyboard_pop(void) {
    osaura_key_event event;
    while (osaura_usb_keyboard_event_pop(&event)) {
        if (event.pressed && event.character) return event.character;
    }
    return 0;
}
