#include "usb-keyboard.h"
#include "mm.h"

#include <stddef.h>
#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA 0xCFCu
#define PCI_CLASS_SERIAL 0x0Cu
#define PCI_SUBCLASS_USB 0x03u
#define PCI_PROGIF_XHCI 0x30u

#define XHCI_TRB_COUNT 256u
#define XHCI_EVENT_COUNT 256u
#define XHCI_MAX_SLOTS 32u
#define XHCI_MAX_SCRATCHPADS 8u
#define XHCI_CONTEXT_BYTES 4096u
#define XHCI_CONTROL_BUFFER_BYTES 512u
#define XHCI_REPORT_BYTES 8u

#define TRB_CYCLE (1u << 0)
#define TRB_ENT (1u << 1)
#define TRB_ISP (1u << 2)
#define TRB_CHAIN (1u << 4)
#define TRB_IOC (1u << 5)
#define TRB_IDT (1u << 6)
#define TRB_TYPE_SHIFT 10u
#define TRB_TYPE_NORMAL 1u
#define TRB_TYPE_SETUP_STAGE 2u
#define TRB_TYPE_DATA_STAGE 3u
#define TRB_TYPE_STATUS_STAGE 4u
#define TRB_TYPE_LINK 6u
#define TRB_TYPE_ENABLE_SLOT 9u
#define TRB_TYPE_ADDRESS_DEVICE 11u
#define TRB_TYPE_CONFIGURE_ENDPOINT 12u
#define TRB_TYPE_EVALUATE_CONTEXT 13u
#define TRB_TYPE_TRANSFER_EVENT 32u
#define TRB_TYPE_COMMAND_COMPLETION 33u
#define TRB_TYPE_PORT_STATUS_CHANGE 34u

#define XHCI_CC_SUCCESS 1u
#define XHCI_CC_SHORT_PACKET 13u

#define USBCMD_RUN (1u << 0)
#define USBCMD_HCRST (1u << 1)
#define USBSTS_HCH (1u << 0)
#define USBSTS_CNR (1u << 11)

#define PORTSC_CCS (1u << 0)
#define PORTSC_PED (1u << 1)
#define PORTSC_PR (1u << 4)
#define PORTSC_PP (1u << 9)
#define PORTSC_SPEED_SHIFT 10u
#define PORTSC_SPEED_MASK (0xFu << PORTSC_SPEED_SHIFT)
#define PORTSC_CSC (1u << 17)
#define PORTSC_PEC (1u << 18)
#define PORTSC_WRC (1u << 19)
#define PORTSC_OCC (1u << 20)
#define PORTSC_PRC (1u << 21)
#define PORTSC_PLC (1u << 22)
#define PORTSC_CEC (1u << 23)
#define PORTSC_CHANGE_BITS (PORTSC_CSC | PORTSC_PEC | PORTSC_WRC | PORTSC_OCC | PORTSC_PRC | PORTSC_PLC | PORTSC_CEC)

#define USB_REQ_GET_DESCRIPTOR 0x06u
#define USB_REQ_SET_CONFIGURATION 0x09u
#define USB_REQ_SET_IDLE 0x0Au
#define USB_REQ_SET_PROTOCOL 0x0Bu
#define USB_DESC_DEVICE 1u
#define USB_DESC_CONFIGURATION 2u
#define USB_DESC_INTERFACE 4u
#define USB_DESC_ENDPOINT 5u
#define USB_CLASS_HID 3u
#define USB_SUBCLASS_BOOT 1u
#define USB_PROTOCOL_KEYBOARD 1u
#define USB_ENDPOINT_INTERRUPT 3u

#define XHCI_EP_CONTROL 4u
#define XHCI_EP_INTERRUPT_IN 7u
#define XHCI_DCI_EP0 1u

extern void osaura_keyboard_submit(char c);

typedef struct __attribute__((packed, aligned(16))) {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} xhci_trb;

typedef struct __attribute__((packed, aligned(16))) {
    uint64_t segment_base;
    uint32_t segment_size;
    uint32_t reserved;
} xhci_erst_entry;

typedef struct {
    xhci_trb *trbs;
    uint16_t enqueue;
    uint8_t cycle;
} xhci_ring;

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint64_t mmio;
} xhci_pci_device;

typedef struct {
    volatile uint8_t *cap;
    volatile uint8_t *op;
    volatile uint8_t *runtime;
    volatile uint8_t *doorbells;
    uint8_t cap_length;
    uint8_t max_slots;
    uint8_t max_ports;
    uint8_t context_size;
    uint8_t slot_id;
    uint8_t root_port;
    uint8_t speed;
    uint8_t interface_number;
    uint8_t interrupt_dci;
    uint16_t interrupt_mps;
    uint8_t interrupt_interval;
    uint16_t event_dequeue;
    uint8_t event_cycle;
    uint8_t ready;
    uint8_t last_report[XHCI_REPORT_BYTES];
} xhci_keyboard_state;

static xhci_keyboard_state g_xhci;
static uint64_t g_dcbaa[256] __attribute__((aligned(64)));
static uint64_t g_scratchpad_ptrs[XHCI_MAX_SCRATCHPADS] __attribute__((aligned(64)));
static uint8_t g_scratchpads[XHCI_MAX_SCRATCHPADS][4096] __attribute__((aligned(4096)));
static xhci_trb g_command_trbs[XHCI_TRB_COUNT] __attribute__((aligned(64)));
static xhci_trb g_event_trbs[XHCI_EVENT_COUNT] __attribute__((aligned(64)));
static xhci_erst_entry g_erst[1] __attribute__((aligned(64)));
static uint8_t g_input_context[XHCI_CONTEXT_BYTES] __attribute__((aligned(64)));
static uint8_t g_device_context[XHCI_CONTEXT_BYTES] __attribute__((aligned(64)));
static xhci_trb g_ep0_trbs[XHCI_TRB_COUNT] __attribute__((aligned(64)));
static xhci_trb g_interrupt_trbs[XHCI_TRB_COUNT] __attribute__((aligned(64)));
static uint8_t g_control_buffer[XHCI_CONTROL_BUFFER_BYTES] __attribute__((aligned(64)));
static uint8_t g_report[XHCI_REPORT_BYTES] __attribute__((aligned(64)));
static xhci_ring g_command_ring;
static xhci_ring g_ep0_ring;
static xhci_ring g_interrupt_ring;

static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

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

static void zero_bytes(void *target, size_t bytes) {
    uint8_t *out = (uint8_t *)target;
    for (size_t i = 0; i < bytes; ++i) out[i] = 0u;
}

static void copy_bytes(void *target, const void *source, size_t bytes) {
    uint8_t *out = (uint8_t *)target;
    const uint8_t *in = (const uint8_t *)source;
    for (size_t i = 0; i < bytes; ++i) out[i] = in[i];
}

static uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = 0x80000000u |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)device << 11) |
                       ((uint32_t)function << 8) |
                       ((uint32_t)offset & 0xFCu);
    out32(PCI_CONFIG_ADDRESS, address);
    return in32(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus,
                        uint8_t device,
                        uint8_t function,
                        uint8_t offset,
                        uint32_t value) {
    uint32_t address = 0x80000000u |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)device << 11) |
                       ((uint32_t)function << 8) |
                       ((uint32_t)offset & 0xFCu);
    out32(PCI_CONFIG_ADDRESS, address);
    out32(PCI_CONFIG_DATA, value);
}

static int pci_find_xhci(xhci_pci_device *out) {
    if (!out) return 0;
    for (uint32_t bus = 0; bus < 256u; ++bus) {
        for (uint32_t device = 0; device < 32u; ++device) {
            for (uint32_t function = 0; function < 8u; ++function) {
                uint32_t id = pci_read32((uint8_t)bus, (uint8_t)device, (uint8_t)function, 0x00u);
                if ((id & 0xFFFFu) == 0xFFFFu) {
                    if (function == 0u) break;
                    continue;
                }
                uint32_t class_reg = pci_read32((uint8_t)bus, (uint8_t)device, (uint8_t)function, 0x08u);
                uint8_t class_code = (uint8_t)(class_reg >> 24);
                uint8_t subclass = (uint8_t)(class_reg >> 16);
                uint8_t prog_if = (uint8_t)(class_reg >> 8);
                if (class_code != PCI_CLASS_SERIAL || subclass != PCI_SUBCLASS_USB || prog_if != PCI_PROGIF_XHCI)
                    continue;

                uint32_t bar_low = pci_read32((uint8_t)bus, (uint8_t)device, (uint8_t)function, 0x10u);
                if (bar_low & 1u) continue;
                uint64_t mmio = (uint64_t)(bar_low & ~0xFu);
                if ((bar_low & 0x6u) == 0x4u) {
                    uint32_t bar_high = pci_read32((uint8_t)bus, (uint8_t)device, (uint8_t)function, 0x14u);
                    mmio |= ((uint64_t)bar_high << 32);
                }
                if (!mmio || !osaura_vm_contains_phys(mmio, 0x10000u)) continue;

                uint32_t command = pci_read32((uint8_t)bus, (uint8_t)device, (uint8_t)function, 0x04u);
                command |= 0x00000006u;
                pci_write32((uint8_t)bus, (uint8_t)device, (uint8_t)function, 0x04u, command);

                out->bus = (uint8_t)bus;
                out->device = (uint8_t)device;
                out->function = (uint8_t)function;
                out->mmio = mmio;
                return 1;
            }
        }
    }
    return 0;
}

static uint8_t mmio_read8(volatile uint8_t *base, uint32_t offset) {
    return *(volatile uint8_t *)(base + offset);
}

static uint32_t mmio_read32(volatile uint8_t *base, uint32_t offset) {
    return *(volatile uint32_t *)(void *)(base + offset);
}

static void mmio_write32(volatile uint8_t *base, uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(void *)(base + offset) = value;
}

static void mmio_write64(volatile uint8_t *base, uint32_t offset, uint64_t value) {
    *(volatile uint64_t *)(void *)(base + offset) = value;
}

static int wait32_clear(volatile uint8_t *base, uint32_t offset, uint32_t mask) {
    for (uint32_t spin = 0; spin < 10000000u; ++spin) {
        if ((mmio_read32(base, offset) & mask) == 0u) return 1;
        cpu_pause();
    }
    return 0;
}

static int wait32_set(volatile uint8_t *base, uint32_t offset, uint32_t mask) {
    for (uint32_t spin = 0; spin < 10000000u; ++spin) {
        if ((mmio_read32(base, offset) & mask) == mask) return 1;
        cpu_pause();
    }
    return 0;
}

static void ring_init(xhci_ring *ring, xhci_trb *trbs) {
    zero_bytes(trbs, sizeof(xhci_trb) * XHCI_TRB_COUNT);
    ring->trbs = trbs;
    ring->enqueue = 0u;
    ring->cycle = 1u;
    trbs[XHCI_TRB_COUNT - 1u].parameter = (uint64_t)(uintptr_t)trbs;
    trbs[XHCI_TRB_COUNT - 1u].status = 0u;
    trbs[XHCI_TRB_COUNT - 1u].control = TRB_CYCLE | (1u << 1) | (TRB_TYPE_LINK << TRB_TYPE_SHIFT);
}

static xhci_trb *ring_enqueue(xhci_ring *ring, uint64_t parameter, uint32_t status, uint32_t control) {
    if (!ring || !ring->trbs) return NULL;
    if (ring->enqueue >= XHCI_TRB_COUNT - 1u) {
        xhci_trb *link = &ring->trbs[XHCI_TRB_COUNT - 1u];
        link->parameter = (uint64_t)(uintptr_t)ring->trbs;
        link->status = 0u;
        link->control = (ring->cycle ? TRB_CYCLE : 0u) |
                        (1u << 1) |
                        (TRB_TYPE_LINK << TRB_TYPE_SHIFT);
        ring->enqueue = 0u;
        ring->cycle ^= 1u;
    }

    xhci_trb *trb = &ring->trbs[ring->enqueue++];
    trb->parameter = parameter;
    trb->status = status;
    __asm__ volatile("" ::: "memory");
    trb->control = control | (ring->cycle ? TRB_CYCLE : 0u);
    return trb;
}

static int event_next(xhci_trb *out) {
    xhci_trb *event = &g_event_trbs[g_xhci.event_dequeue];
    uint32_t control = event->control;
    if ((control & TRB_CYCLE) != (g_xhci.event_cycle ? TRB_CYCLE : 0u)) return 0;

    if (out) *out = *event;
    ++g_xhci.event_dequeue;
    if (g_xhci.event_dequeue >= XHCI_EVENT_COUNT) {
        g_xhci.event_dequeue = 0u;
        g_xhci.event_cycle ^= 1u;
    }

    uint64_t next = (uint64_t)(uintptr_t)&g_event_trbs[g_xhci.event_dequeue];
    mmio_write64(g_xhci.runtime, 0x20u + 0x18u, next | (1ull << 3));
    return 1;
}

static uint8_t trb_type(const xhci_trb *trb) {
    return trb ? (uint8_t)((trb->control >> TRB_TYPE_SHIFT) & 0x3Fu) : 0u;
}

static uint8_t trb_completion_code(const xhci_trb *trb) {
    return trb ? (uint8_t)(trb->status >> 24) : 0u;
}

static int wait_event(uint8_t expected_type, uint8_t expected_slot, uint8_t expected_dci, xhci_trb *out) {
    for (uint32_t spin = 0; spin < 20000000u; ++spin) {
        xhci_trb event;
        if (!event_next(&event)) {
            cpu_pause();
            continue;
        }
        uint8_t type = trb_type(&event);
        if (type == TRB_TYPE_PORT_STATUS_CHANGE) continue;
        if (type != expected_type) continue;
        if (expected_slot && (uint8_t)(event.control >> 24) != expected_slot) continue;
        if (expected_dci && (uint8_t)((event.control >> 16) & 0x1Fu) != expected_dci) continue;
        if (out) *out = event;
        return 1;
    }
    return 0;
}

static int command_submit(uint64_t parameter,
                          uint32_t status,
                          uint32_t control,
                          uint8_t expected_slot,
                          uint8_t *slot_out) {
    xhci_trb *command = ring_enqueue(&g_command_ring, parameter, status, control);
    if (!command) return 0;
    __asm__ volatile("" ::: "memory");
    mmio_write32(g_xhci.doorbells, 0u, 0u);

    xhci_trb event;
    if (!wait_event(TRB_TYPE_COMMAND_COMPLETION, expected_slot, 0u, &event)) return 0;
    if (trb_completion_code(&event) != XHCI_CC_SUCCESS) return 0;
    if (slot_out) *slot_out = (uint8_t)(event.control >> 24);
    return 1;
}

static uint32_t *input_context_at(uint8_t dci) {
    size_t offset = (size_t)(dci + 1u) * g_xhci.context_size;
    if (offset + g_xhci.context_size > sizeof g_input_context) return NULL;
    return (uint32_t *)(void *)(g_input_context + offset);
}

static uint32_t *input_control_context(void) {
    return (uint32_t *)(void *)g_input_context;
}

static uint32_t *device_context_at(uint8_t dci) {
    size_t offset = (size_t)dci * g_xhci.context_size;
    if (offset + g_xhci.context_size > sizeof g_device_context) return NULL;
    return (uint32_t *)(void *)(g_device_context + offset);
}

static uint16_t initial_ep0_mps(uint8_t speed) {
    if (speed == 2u) return 8u;
    if (speed == 3u) return 64u;
    if (speed >= 4u) return 512u;
    return 8u;
}

static void fill_slot_context(uint32_t *slot, uint8_t context_entries) {
    slot[0] = ((uint32_t)g_xhci.speed << 20) | ((uint32_t)context_entries << 27);
    slot[1] = ((uint32_t)g_xhci.root_port << 16);
    slot[2] = 0u;
    slot[3] = 0u;
}

static void fill_endpoint_context(uint32_t *ep,
                                  uint8_t endpoint_type,
                                  uint16_t max_packet,
                                  uint8_t interval,
                                  const xhci_ring *ring) {
    ep[0] = ((uint32_t)interval << 16);
    ep[1] = (3u << 1) |
            ((uint32_t)endpoint_type << 3) |
            ((uint32_t)max_packet << 16);
    uint64_t dequeue = (uint64_t)(uintptr_t)ring->trbs | 1ull;
    ep[2] = (uint32_t)dequeue;
    ep[3] = (uint32_t)(dequeue >> 32);
    ep[4] = (uint32_t)max_packet | ((uint32_t)max_packet << 16);
}

static int xhci_legacy_handoff(uint32_t hccparams1) {
    uint32_t offset = ((hccparams1 >> 16) & 0xFFFFu) * 4u;
    for (uint32_t guard = 0; offset && guard < 64u; ++guard) {
        uint32_t capability = mmio_read32(g_xhci.cap, offset);
        uint8_t id = (uint8_t)capability;
        uint8_t next = (uint8_t)(capability >> 8);
        if (id == 1u) {
            mmio_write32(g_xhci.cap, offset, capability | (1u << 24));
            for (uint32_t spin = 0; spin < 1000000u; ++spin) {
                uint32_t value = mmio_read32(g_xhci.cap, offset);
                if ((value & (1u << 16)) == 0u) return 1;
                cpu_pause();
            }
            return 0;
        }
        if (!next) break;
        offset += (uint32_t)next * 4u;
    }
    return 1;
}

static int xhci_controller_init(uint64_t mmio_base) {
    zero_bytes(&g_xhci, sizeof g_xhci);
    g_xhci.cap = (volatile uint8_t *)(uintptr_t)mmio_base;
    g_xhci.cap_length = mmio_read8(g_xhci.cap, 0u);
    if (g_xhci.cap_length < 0x20u) return 0;

    uint32_t hcsparams1 = mmio_read32(g_xhci.cap, 0x04u);
    uint32_t hcsparams2 = mmio_read32(g_xhci.cap, 0x08u);
    uint32_t hccparams1 = mmio_read32(g_xhci.cap, 0x10u);
    uint32_t dboff = mmio_read32(g_xhci.cap, 0x14u) & ~0x3u;
    uint32_t rtsoff = mmio_read32(g_xhci.cap, 0x18u) & ~0x1Fu;

    g_xhci.max_slots = (uint8_t)(hcsparams1 & 0xFFu);
    if (!g_xhci.max_slots) return 0;
    if (g_xhci.max_slots > XHCI_MAX_SLOTS) g_xhci.max_slots = XHCI_MAX_SLOTS;
    g_xhci.max_ports = (uint8_t)(hcsparams1 >> 24);
    if (!g_xhci.max_ports) return 0;
    g_xhci.context_size = (hccparams1 & (1u << 2)) ? 64u : 32u;
    g_xhci.op = g_xhci.cap + g_xhci.cap_length;
    g_xhci.runtime = g_xhci.cap + rtsoff;
    g_xhci.doorbells = g_xhci.cap + dboff;

    if (!xhci_legacy_handoff(hccparams1)) return 0;

    uint32_t command = mmio_read32(g_xhci.op, 0x00u);
    command &= ~USBCMD_RUN;
    mmio_write32(g_xhci.op, 0x00u, command);
    if (!wait32_set(g_xhci.op, 0x04u, USBSTS_HCH)) return 0;

    mmio_write32(g_xhci.op, 0x00u, command | USBCMD_HCRST);
    if (!wait32_clear(g_xhci.op, 0x00u, USBCMD_HCRST)) return 0;
    if (!wait32_clear(g_xhci.op, 0x04u, USBSTS_CNR)) return 0;

    zero_bytes(g_dcbaa, sizeof g_dcbaa);
    zero_bytes(g_scratchpad_ptrs, sizeof g_scratchpad_ptrs);
    uint32_t scratch_hi = (hcsparams2 >> 27) & 0x1Fu;
    uint32_t scratch_lo = (hcsparams2 >> 21) & 0x1Fu;
    uint32_t scratch_count = (scratch_hi << 5) | scratch_lo;
    if (scratch_count > XHCI_MAX_SCRATCHPADS) return 0;
    if (scratch_count) {
        for (uint32_t i = 0; i < scratch_count; ++i) {
            zero_bytes(g_scratchpads[i], sizeof g_scratchpads[i]);
            g_scratchpad_ptrs[i] = (uint64_t)(uintptr_t)g_scratchpads[i];
        }
        g_dcbaa[0] = (uint64_t)(uintptr_t)g_scratchpad_ptrs;
    }

    ring_init(&g_command_ring, g_command_trbs);
    ring_init(&g_ep0_ring, g_ep0_trbs);
    ring_init(&g_interrupt_ring, g_interrupt_trbs);
    zero_bytes(g_event_trbs, sizeof g_event_trbs);
    g_xhci.event_dequeue = 0u;
    g_xhci.event_cycle = 1u;

    g_erst[0].segment_base = (uint64_t)(uintptr_t)g_event_trbs;
    g_erst[0].segment_size = XHCI_EVENT_COUNT;
    g_erst[0].reserved = 0u;

    mmio_write64(g_xhci.op, 0x30u, (uint64_t)(uintptr_t)g_dcbaa);
    mmio_write64(g_xhci.op, 0x18u, (uint64_t)(uintptr_t)g_command_trbs | 1ull);
    mmio_write32(g_xhci.op, 0x38u, g_xhci.max_slots);

    mmio_write32(g_xhci.runtime, 0x20u + 0x08u, 1u);
    mmio_write64(g_xhci.runtime, 0x20u + 0x10u, (uint64_t)(uintptr_t)g_erst);
    mmio_write64(g_xhci.runtime, 0x20u + 0x18u, (uint64_t)(uintptr_t)g_event_trbs);
    mmio_write32(g_xhci.runtime, 0x20u + 0x04u, 0u);

    mmio_write32(g_xhci.op, 0x00u, USBCMD_RUN);
    if (!wait32_clear(g_xhci.op, 0x04u, USBSTS_HCH)) return 0;
    return 1;
}

static int xhci_reset_connected_port(void) {
    for (uint32_t port = 1u; port <= g_xhci.max_ports; ++port) {
        uint32_t offset = 0x400u + (port - 1u) * 0x10u;
        uint32_t value = mmio_read32(g_xhci.op, offset);
        if (!(value & PORTSC_CCS)) continue;

        uint32_t write_value = value & ~PORTSC_CHANGE_BITS & ~PORTSC_PED;
        write_value |= PORTSC_PP;
        mmio_write32(g_xhci.op, offset, write_value);

        value = mmio_read32(g_xhci.op, offset);
        if (!(value & PORTSC_PED)) {
            write_value = value & ~PORTSC_CHANGE_BITS & ~PORTSC_PED;
            write_value |= PORTSC_PP | PORTSC_PR;
            mmio_write32(g_xhci.op, offset, write_value);
            if (!wait32_clear(g_xhci.op, offset, PORTSC_PR)) continue;
        }

        value = mmio_read32(g_xhci.op, offset);
        if (!(value & PORTSC_CCS) || !(value & PORTSC_PED)) continue;
        g_xhci.root_port = (uint8_t)port;
        g_xhci.speed = (uint8_t)((value & PORTSC_SPEED_MASK) >> PORTSC_SPEED_SHIFT);
        mmio_write32(g_xhci.op, offset, value | PORTSC_CHANGE_BITS);
        return 1;
    }
    return 0;
}

static int xhci_address_device(void) {
    uint8_t slot = 0u;
    if (!command_submit(0u, 0u, TRB_TYPE_ENABLE_SLOT << TRB_TYPE_SHIFT, 0u, &slot)) return 0;
    if (!slot || slot > g_xhci.max_slots) return 0;
    g_xhci.slot_id = slot;

    zero_bytes(g_input_context, sizeof g_input_context);
    zero_bytes(g_device_context, sizeof g_device_context);
    g_dcbaa[slot] = (uint64_t)(uintptr_t)g_device_context;

    uint32_t *control = input_control_context();
    uint32_t *slot_context = input_context_at(0u);
    uint32_t *ep0 = input_context_at(XHCI_DCI_EP0);
    if (!control || !slot_context || !ep0) return 0;
    control[1] = (1u << 0) | (1u << XHCI_DCI_EP0);
    fill_slot_context(slot_context, XHCI_DCI_EP0);
    fill_endpoint_context(ep0,
                          XHCI_EP_CONTROL,
                          initial_ep0_mps(g_xhci.speed),
                          0u,
                          &g_ep0_ring);

    if (!command_submit((uint64_t)(uintptr_t)g_input_context,
                        0u,
                        (TRB_TYPE_ADDRESS_DEVICE << TRB_TYPE_SHIFT) |
                        ((uint32_t)slot << 24),
                        slot,
                        NULL))
        return 0;
    return 1;
}

static int control_transfer(uint8_t request_type,
                            uint8_t request,
                            uint16_t value,
                            uint16_t index,
                            void *buffer,
                            uint16_t length) {
    uint8_t direction_in = (request_type & 0x80u) != 0u;
    uint64_t setup = (uint64_t)request_type |
                     ((uint64_t)request << 8) |
                     ((uint64_t)value << 16) |
                     ((uint64_t)index << 32) |
                     ((uint64_t)length << 48);
    uint32_t trt = length ? (direction_in ? 3u : 2u) : 0u;

    if (!ring_enqueue(&g_ep0_ring,
                      setup,
                      8u,
                      (TRB_TYPE_SETUP_STAGE << TRB_TYPE_SHIFT) |
                      TRB_IDT |
                      (trt << 16)))
        return 0;

    if (length) {
        if (!buffer) return 0;
        if (!ring_enqueue(&g_ep0_ring,
                          (uint64_t)(uintptr_t)buffer,
                          length,
                          (TRB_TYPE_DATA_STAGE << TRB_TYPE_SHIFT) |
                          (direction_in ? (1u << 16) : 0u)))
            return 0;
    }

    uint32_t status_direction = (!length || !direction_in) ? (1u << 16) : 0u;
    if (!ring_enqueue(&g_ep0_ring,
                      0u,
                      0u,
                      (TRB_TYPE_STATUS_STAGE << TRB_TYPE_SHIFT) |
                      status_direction |
                      TRB_IOC))
        return 0;

    __asm__ volatile("" ::: "memory");
    mmio_write32(g_xhci.doorbells, (uint32_t)g_xhci.slot_id * 4u, XHCI_DCI_EP0);

    xhci_trb event;
    if (!wait_event(TRB_TYPE_TRANSFER_EVENT, g_xhci.slot_id, XHCI_DCI_EP0, &event)) return 0;
    uint8_t cc = trb_completion_code(&event);
    return cc == XHCI_CC_SUCCESS || cc == XHCI_CC_SHORT_PACKET;
}

static int update_ep0_mps(uint16_t max_packet) {
    uint32_t *device_ep0 = device_context_at(XHCI_DCI_EP0);
    if (!device_ep0) return 0;
    uint16_t current = (uint16_t)(device_ep0[1] >> 16);
    if (current == max_packet) return 1;

    zero_bytes(g_input_context, sizeof g_input_context);
    uint32_t *control = input_control_context();
    uint32_t *ep0 = input_context_at(XHCI_DCI_EP0);
    if (!control || !ep0) return 0;
    control[1] = (1u << XHCI_DCI_EP0);
    fill_endpoint_context(ep0, XHCI_EP_CONTROL, max_packet, 0u, &g_ep0_ring);
    return command_submit((uint64_t)(uintptr_t)g_input_context,
                          0u,
                          (TRB_TYPE_EVALUATE_CONTEXT << TRB_TYPE_SHIFT) |
                          ((uint32_t)g_xhci.slot_id << 24),
                          g_xhci.slot_id,
                          NULL);
}

static uint8_t interval_to_xhci(uint8_t speed, uint8_t usb_interval) {
    if (!usb_interval) usb_interval = 1u;
    if (speed >= 3u) {
        if (usb_interval > 16u) usb_interval = 16u;
        return usb_interval;
    }
    uint32_t value = usb_interval;
    uint8_t log2 = 0u;
    uint32_t power = 1u;
    while (power < value && log2 < 7u) {
        power <<= 1;
        ++log2;
    }
    return (uint8_t)(log2 + 4u);
}

static int parse_keyboard_configuration(const uint8_t *data,
                                        uint16_t bytes,
                                        uint8_t *configuration_value) {
    if (!data || bytes < 9u || !configuration_value) return 0;
    *configuration_value = data[5];
    uint8_t keyboard_interface = 0xFFu;
    uint8_t in_keyboard = 0u;

    for (uint16_t offset = 0u; offset + 2u <= bytes;) {
        uint8_t length = data[offset];
        uint8_t type = data[offset + 1u];
        if (length < 2u || offset + length > bytes) return 0;

        if (type == USB_DESC_INTERFACE && length >= 9u) {
            in_keyboard = data[offset + 5u] == USB_CLASS_HID &&
                          data[offset + 6u] == USB_SUBCLASS_BOOT &&
                          data[offset + 7u] == USB_PROTOCOL_KEYBOARD;
            if (in_keyboard) keyboard_interface = data[offset + 2u];
        } else if (type == USB_DESC_ENDPOINT && in_keyboard && length >= 7u) {
            uint8_t address = data[offset + 2u];
            uint8_t attributes = data[offset + 3u] & 0x03u;
            if ((address & 0x80u) && attributes == USB_ENDPOINT_INTERRUPT) {
                uint8_t endpoint_number = address & 0x0Fu;
                if (!endpoint_number || endpoint_number > 15u) return 0;
                g_xhci.interface_number = keyboard_interface;
                g_xhci.interrupt_dci = (uint8_t)(endpoint_number * 2u + 1u);
                g_xhci.interrupt_mps = (uint16_t)data[offset + 4u] |
                                       ((uint16_t)data[offset + 5u] << 8);
                g_xhci.interrupt_mps &= 0x07FFu;
                g_xhci.interrupt_interval = data[offset + 6u];
                return g_xhci.interrupt_mps != 0u;
            }
        }
        offset = (uint16_t)(offset + length);
    }
    return 0;
}

static int enumerate_keyboard(void) {
    zero_bytes(g_control_buffer, sizeof g_control_buffer);
    if (!control_transfer(0x80u,
                          USB_REQ_GET_DESCRIPTOR,
                          (uint16_t)(USB_DESC_DEVICE << 8),
                          0u,
                          g_control_buffer,
                          18u))
        return 0;
    if (g_control_buffer[1] != USB_DESC_DEVICE || g_control_buffer[0] < 18u) return 0;
    if (!update_ep0_mps(g_control_buffer[7])) return 0;

    zero_bytes(g_control_buffer, sizeof g_control_buffer);
    if (!control_transfer(0x80u,
                          USB_REQ_GET_DESCRIPTOR,
                          (uint16_t)(USB_DESC_CONFIGURATION << 8),
                          0u,
                          g_control_buffer,
                          9u))
        return 0;
    uint16_t total = (uint16_t)g_control_buffer[2] | ((uint16_t)g_control_buffer[3] << 8);
    if (total < 9u || total > sizeof g_control_buffer) return 0;

    zero_bytes(g_control_buffer, sizeof g_control_buffer);
    if (!control_transfer(0x80u,
                          USB_REQ_GET_DESCRIPTOR,
                          (uint16_t)(USB_DESC_CONFIGURATION << 8),
                          0u,
                          g_control_buffer,
                          total))
        return 0;

    uint8_t configuration = 0u;
    if (!parse_keyboard_configuration(g_control_buffer, total, &configuration)) return 0;
    if (!control_transfer(0x00u,
                          USB_REQ_SET_CONFIGURATION,
                          configuration,
                          0u,
                          NULL,
                          0u))
        return 0;

    if (!control_transfer(0x21u,
                          USB_REQ_SET_PROTOCOL,
                          0u,
                          g_xhci.interface_number,
                          NULL,
                          0u))
        return 0;

    (void)control_transfer(0x21u,
                           USB_REQ_SET_IDLE,
                           0u,
                           g_xhci.interface_number,
                           NULL,
                           0u);
    return 1;
}

static int configure_interrupt_endpoint(void) {
    uint8_t dci = g_xhci.interrupt_dci;
    if (dci <= XHCI_DCI_EP0 || dci >= 32u) return 0;

    zero_bytes(g_input_context, sizeof g_input_context);
    uint32_t *control = input_control_context();
    uint32_t *slot = input_context_at(0u);
    uint32_t *ep = input_context_at(dci);
    if (!control || !slot || !ep) return 0;

    control[1] = (1u << 0) | (1u << dci);
    fill_slot_context(slot, dci);
    fill_endpoint_context(ep,
                          XHCI_EP_INTERRUPT_IN,
                          g_xhci.interrupt_mps,
                          interval_to_xhci(g_xhci.speed, g_xhci.interrupt_interval),
                          &g_interrupt_ring);

    if (!command_submit((uint64_t)(uintptr_t)g_input_context,
                        0u,
                        (TRB_TYPE_CONFIGURE_ENDPOINT << TRB_TYPE_SHIFT) |
                        ((uint32_t)g_xhci.slot_id << 24),
                        g_xhci.slot_id,
                        NULL))
        return 0;
    return 1;
}

static int queue_keyboard_report(void) {
    zero_bytes(g_report, sizeof g_report);
    if (!ring_enqueue(&g_interrupt_ring,
                      (uint64_t)(uintptr_t)g_report,
                      sizeof g_report,
                      (TRB_TYPE_NORMAL << TRB_TYPE_SHIFT) |
                      TRB_ISP |
                      TRB_IOC))
        return 0;
    __asm__ volatile("" ::: "memory");
    mmio_write32(g_xhci.doorbells,
                 (uint32_t)g_xhci.slot_id * 4u,
                 g_xhci.interrupt_dci);
    return 1;
}

static int key_was_down(uint8_t key) {
    for (uint32_t i = 2u; i < XHCI_REPORT_BYTES; ++i)
        if (g_xhci.last_report[i] == key) return 1;
    return 0;
}

static char hid_key_to_char(uint8_t key, uint8_t modifiers) {
    uint8_t shift = (modifiers & 0x22u) != 0u;
    if (key >= 0x04u && key <= 0x1Du)
        return (char)('A' + (key - 0x04u));
    if (key >= 0x1Eu && key <= 0x26u)
        return (char)('1' + (key - 0x1Eu));
    if (key == 0x27u) return '0';
    if (key == 0x28u) return '\n';
    if (key == 0x2Au) return '\b';
    if (key == 0x2Cu) return ' ';
    if (key == 0x2Du) return shift ? '_' : '-';
    if (key == 0x37u) return shift ? '>' : '.';
    if (key == 0x38u) return shift ? '?' : '/';
    return 0;
}

static void process_keyboard_report(void) {
    for (uint32_t i = 2u; i < XHCI_REPORT_BYTES; ++i) {
        uint8_t key = g_report[i];
        if (!key || key_was_down(key)) continue;
        char c = hid_key_to_char(key, g_report[0]);
        if (c) osaura_keyboard_submit(c);
    }
    copy_bytes(g_xhci.last_report, g_report, sizeof g_xhci.last_report);
}

int osaura_usb_keyboard_init(void) {
    xhci_pci_device pci;
    if (!pci_find_xhci(&pci)) return 0;
    if (!xhci_controller_init(pci.mmio)) return 0;
    if (!xhci_reset_connected_port()) return 0;
    if (!xhci_address_device()) return 0;
    if (!enumerate_keyboard()) return 0;
    if (!configure_interrupt_endpoint()) return 0;
    if (!queue_keyboard_report()) return 0;
    g_xhci.ready = 1u;
    return 1;
}

int osaura_usb_keyboard_ready(void) {
    return g_xhci.ready != 0u;
}

void osaura_usb_keyboard_poll(void) {
    if (!g_xhci.ready) return;

    xhci_trb event;
    while (event_next(&event)) {
        if (trb_type(&event) != TRB_TYPE_TRANSFER_EVENT) continue;
        if ((uint8_t)(event.control >> 24) != g_xhci.slot_id) continue;
        if ((uint8_t)((event.control >> 16) & 0x1Fu) != g_xhci.interrupt_dci) continue;
        uint8_t cc = trb_completion_code(&event);
        if (cc != XHCI_CC_SUCCESS && cc != XHCI_CC_SHORT_PACKET) {
            g_xhci.ready = 0u;
            return;
        }
        process_keyboard_report();
        if (!queue_keyboard_report()) {
            g_xhci.ready = 0u;
            return;
        }
    }
}
