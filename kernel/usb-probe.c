#include "mm.h"

#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA 0xCFCu
#define PCI_CLASS_SERIAL 0x0Cu
#define PCI_SUBCLASS_USB 0x03u
#define PCI_PROGIF_XHCI 0x30u
#define PORTSC_CCS (1u << 0)

static inline uint32_t in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void out32(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
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

uint32_t osaura_usb_xhci_preflight(void) {
    for (uint32_t bus = 0; bus < 256u; ++bus) {
        for (uint32_t device = 0; device < 32u; ++device) {
            for (uint32_t function = 0; function < 8u; ++function) {
                uint32_t id = pci_read32((uint8_t)bus, (uint8_t)device, (uint8_t)function, 0x00u);
                if ((id & 0xFFFFu) == 0xFFFFu) {
                    if (function == 0u) break;
                    continue;
                }
                uint32_t class_reg = pci_read32((uint8_t)bus, (uint8_t)device, (uint8_t)function, 0x08u);
                if ((uint8_t)(class_reg >> 24) != PCI_CLASS_SERIAL ||
                    (uint8_t)(class_reg >> 16) != PCI_SUBCLASS_USB ||
                    (uint8_t)(class_reg >> 8) != PCI_PROGIF_XHCI)
                    continue;

                uint32_t bar_low = pci_read32((uint8_t)bus, (uint8_t)device, (uint8_t)function, 0x10u);
                if (bar_low & 1u) return 2u;
                uint64_t mmio = (uint64_t)(bar_low & ~0xFu);
                if ((bar_low & 0x6u) == 0x4u)
                    mmio |= (uint64_t)pci_read32((uint8_t)bus, (uint8_t)device, (uint8_t)function, 0x14u) << 32;
                if (!mmio || !osaura_vm_contains_phys(mmio, 0x10000u)) return 3u;

                volatile uint8_t *cap = (volatile uint8_t *)(uintptr_t)mmio;
                uint8_t cap_length = *(volatile uint8_t *)cap;
                if (cap_length < 0x20u) return 4u;
                uint32_t hcsparams1 = *(volatile uint32_t *)(void *)(cap + 0x04u);
                uint8_t max_ports = (uint8_t)(hcsparams1 >> 24);
                if (!max_ports) return 5u;
                volatile uint8_t *op = cap + cap_length;
                for (uint32_t port = 1u; port <= max_ports; ++port) {
                    uint32_t portsc = *(volatile uint32_t *)(void *)(op + 0x400u + (port - 1u) * 0x10u);
                    if (portsc & PORTSC_CCS) return 7u;
                }
                return 6u;
            }
        }
    }
    return 1u;
}
