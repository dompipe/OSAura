#include "e1000.h"
#include "hot-shadow.h"

#include <stddef.h>
#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0x0cf8u
#define PCI_CONFIG_DATA 0x0cfcu

#define E1000_REG_CTRL 0x0000u
#define E1000_REG_STATUS 0x0008u
#define E1000_REG_EERD 0x0014u
#define E1000_REG_ICR 0x00c0u
#define E1000_REG_IMC 0x00d8u
#define E1000_REG_RCTL 0x0100u
#define E1000_REG_TCTL 0x0400u
#define E1000_REG_TIPG 0x0410u
#define E1000_REG_RDBAL 0x2800u
#define E1000_REG_RDBAH 0x2804u
#define E1000_REG_RDLEN 0x2808u
#define E1000_REG_RDH 0x2810u
#define E1000_REG_RDT 0x2818u
#define E1000_REG_TDBAL 0x3800u
#define E1000_REG_TDBAH 0x3804u
#define E1000_REG_TDLEN 0x3808u
#define E1000_REG_TDH 0x3810u
#define E1000_REG_TDT 0x3818u
#define E1000_REG_RAL0 0x5400u
#define E1000_REG_RAH0 0x5404u

#define E1000_CTRL_SLU (1u << 6)
#define E1000_RCTL_EN (1u << 1)
#define E1000_RCTL_SBP (1u << 2)
#define E1000_RCTL_UPE (1u << 3)
#define E1000_RCTL_MPE (1u << 4)
#define E1000_RCTL_BAM (1u << 15)
#define E1000_RCTL_SECRC (1u << 26)
#define E1000_TCTL_EN (1u << 1)
#define E1000_TCTL_PSP (1u << 3)
#define E1000_TCTL_CT_SHIFT 4u
#define E1000_TCTL_COLD_SHIFT 12u

#define E1000_RX_COUNT 32u
#define E1000_TX_COUNT 32u
#define E1000_BUFFER_BYTES 2048u

#define E1000_RX_STATUS_DD 0x01u
#define E1000_RX_STATUS_EOP 0x02u
#define E1000_TX_CMD_EOP 0x01u
#define E1000_TX_CMD_IFCS 0x02u
#define E1000_TX_CMD_RS 0x08u
#define E1000_TX_STATUS_DD 0x01u

#define OSAURA_NET_HOT_RX_FRAME 0u
#define OSAURA_NET_HOT_TX_FRAME 1u

typedef struct __attribute__((packed, aligned(16))) {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} e1000_rx_desc;

typedef struct __attribute__((packed, aligned(16))) {
    uint64_t address;
    uint16_t length;
    uint8_t cso;
    uint8_t command;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} e1000_tx_desc;

typedef struct {
    void *frame;
    const void *const_frame;
    uint16_t capacity;
    uint16_t bytes;
    uint16_t *bytes_out;
} e1000_hot_request;

static volatile uint8_t *g_mmio;
static uint8_t g_ready;
static osaura_mac_address g_mac;
static e1000_rx_desc g_rx[E1000_RX_COUNT] __attribute__((aligned(128)));
static e1000_tx_desc g_tx[E1000_TX_COUNT] __attribute__((aligned(128)));
static uint8_t g_rx_buffers[E1000_RX_COUNT][E1000_BUFFER_BYTES] __attribute__((aligned(16)));
static uint8_t g_tx_buffers[E1000_TX_COUNT][E1000_BUFFER_BYTES] __attribute__((aligned(16)));
static uint32_t g_rx_next;
static uint32_t g_tx_next;

static inline uint32_t in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void out32(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline void cpu_pause(void) { __asm__ volatile("pause"); }

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

static uint64_t pci_bar0(uint8_t bus, uint8_t dev, uint8_t fn) {
    uint32_t lo = pci_read32(bus, dev, fn, 0x10u);
    if (lo & 1u) return 0u;
    uint32_t type = (lo >> 1) & 3u;
    uint64_t base = (uint64_t)(lo & ~0x0fu);
    if (type == 2u) base |= (uint64_t)pci_read32(bus, dev, fn, 0x14u) << 32;
    return base;
}

static inline uint32_t reg_read(uint32_t offset) {
    return *(volatile uint32_t *)(g_mmio + offset);
}

static inline void reg_write(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(g_mmio + offset) = value;
}

static void zero_bytes(void *ptr, size_t bytes) {
    uint8_t *p = (uint8_t *)ptr;
    while (bytes--) *p++ = 0u;
}

static void copy_bytes(void *target, const void *source, size_t bytes) {
    uint8_t *out = (uint8_t *)target;
    const uint8_t *in = (const uint8_t *)source;
    while (bytes--) *out++ = *in++;
}

static int read_mac(void) {
    uint32_t ral = reg_read(E1000_REG_RAL0);
    uint32_t rah = reg_read(E1000_REG_RAH0);
    if (rah & (1u << 31)) {
        g_mac.bytes[0] = (uint8_t)ral;
        g_mac.bytes[1] = (uint8_t)(ral >> 8);
        g_mac.bytes[2] = (uint8_t)(ral >> 16);
        g_mac.bytes[3] = (uint8_t)(ral >> 24);
        g_mac.bytes[4] = (uint8_t)rah;
        g_mac.bytes[5] = (uint8_t)(rah >> 8);
        return 1;
    }

    for (uint32_t word = 0; word < 3u; ++word) {
        reg_write(E1000_REG_EERD, 1u | (word << 8));
        uint32_t data = 0u;
        uint32_t spin = 100000u;
        while (spin--) {
            data = reg_read(E1000_REG_EERD);
            if (data & (1u << 4)) break;
            cpu_pause();
        }
        if (!(data & (1u << 4))) return 0;
        uint16_t value = (uint16_t)(data >> 16);
        g_mac.bytes[word * 2u] = (uint8_t)value;
        g_mac.bytes[word * 2u + 1u] = (uint8_t)(value >> 8);
    }
    return 1;
}

static void init_rx(void) {
    zero_bytes(g_rx, sizeof g_rx);
    for (uint32_t i = 0; i < E1000_RX_COUNT; ++i)
        g_rx[i].address = (uint64_t)(uintptr_t)g_rx_buffers[i];

    uint64_t ring = (uint64_t)(uintptr_t)g_rx;
    reg_write(E1000_REG_RDBAL, (uint32_t)ring);
    reg_write(E1000_REG_RDBAH, (uint32_t)(ring >> 32));
    reg_write(E1000_REG_RDLEN, sizeof g_rx);
    reg_write(E1000_REG_RDH, 0u);
    reg_write(E1000_REG_RDT, E1000_RX_COUNT - 1u);
    g_rx_next = 0u;

    reg_write(E1000_REG_RCTL,
              E1000_RCTL_EN | E1000_RCTL_SBP | E1000_RCTL_UPE |
              E1000_RCTL_MPE | E1000_RCTL_BAM | E1000_RCTL_SECRC);
}

static void init_tx(void) {
    zero_bytes(g_tx, sizeof g_tx);
    for (uint32_t i = 0; i < E1000_TX_COUNT; ++i) {
        g_tx[i].address = (uint64_t)(uintptr_t)g_tx_buffers[i];
        g_tx[i].status = E1000_TX_STATUS_DD;
    }

    uint64_t ring = (uint64_t)(uintptr_t)g_tx;
    reg_write(E1000_REG_TDBAL, (uint32_t)ring);
    reg_write(E1000_REG_TDBAH, (uint32_t)(ring >> 32));
    reg_write(E1000_REG_TDLEN, sizeof g_tx);
    reg_write(E1000_REG_TDH, 0u);
    reg_write(E1000_REG_TDT, 0u);
    g_tx_next = 0u;

    reg_write(E1000_REG_TCTL,
              E1000_TCTL_EN | E1000_TCTL_PSP |
              (0x10u << E1000_TCTL_CT_SHIFT) |
              (0x40u << E1000_TCTL_COLD_SHIFT));
    reg_write(E1000_REG_TIPG, 10u | (8u << 10) | (6u << 20));
}

static int e1000_transmit_raw(const void *frame, uint16_t bytes) {
    if (!g_ready || !frame || bytes < 14u || bytes > E1000_BUFFER_BYTES) return 0;
    uint32_t index = g_tx_next;
    e1000_tx_desc *desc = &g_tx[index];
    uint32_t spin = 1000000u;
    while (!(desc->status & E1000_TX_STATUS_DD) && spin--) cpu_pause();
    if (!(desc->status & E1000_TX_STATUS_DD)) return 0;

    copy_bytes(g_tx_buffers[index], frame, bytes);
    desc->length = bytes;
    desc->cso = 0u;
    desc->command = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    desc->status = 0u;
    desc->css = 0u;
    desc->special = 0u;

    g_tx_next = (index + 1u) % E1000_TX_COUNT;
    reg_write(E1000_REG_TDT, g_tx_next);
    return 1;
}

static int e1000_receive_raw(void *frame, uint16_t capacity, uint16_t *bytes_out) {
    if (bytes_out) *bytes_out = 0u;
    if (!g_ready || !frame || !capacity) return 0;

    e1000_rx_desc *desc = &g_rx[g_rx_next];
    if (!(desc->status & E1000_RX_STATUS_DD)) return 0;

    uint16_t bytes = desc->length;
    int complete = (desc->status & E1000_RX_STATUS_EOP) && !desc->errors;
    if (complete && bytes <= capacity) {
        copy_bytes(frame, g_rx_buffers[g_rx_next], bytes);
        if (bytes_out) *bytes_out = bytes;
    } else {
        complete = 0;
    }

    uint32_t consumed = g_rx_next;
    desc->status = 0u;
    desc->errors = 0u;
    desc->length = 0u;
    g_rx_next = (g_rx_next + 1u) % E1000_RX_COUNT;
    reg_write(E1000_REG_RDT, consumed);
    return complete;
}

static int hot_rx_frame(void *context, void *opaque) {
    (void)context;
    e1000_hot_request *request = (e1000_hot_request *)opaque;
    if (!request) return 0;
    return e1000_receive_raw(request->frame, request->capacity, request->bytes_out);
}

static int hot_tx_frame(void *context, void *opaque) {
    (void)context;
    e1000_hot_request *request = (e1000_hot_request *)opaque;
    if (!request) return 0;
    return e1000_transmit_raw(request->const_frame, request->bytes);
}

int osaura_e1000_init(uint8_t bus, uint8_t device, uint8_t function) {
    g_ready = 0u;
    uint32_t id = pci_read32(bus, device, function, 0u);
    if ((id & 0xffffu) != 0x8086u) return 0;

    uint64_t bar = pci_bar0(bus, device, function);
    if (!bar) return 0;
    g_mmio = (volatile uint8_t *)(uintptr_t)bar;

    uint32_t command = pci_read32(bus, device, function, 0x04u);
    command = (command & 0xffff0000u) | ((command & 0xffffu) | 0x0006u);
    pci_write32(bus, device, function, 0x04u, command);

    reg_write(E1000_REG_IMC, 0xffffffffu);
    (void)reg_read(E1000_REG_ICR);
    reg_write(E1000_REG_CTRL, reg_read(E1000_REG_CTRL) | E1000_CTRL_SLU);
    if (!read_mac()) return 0;

    init_rx();
    init_tx();
    (void)reg_read(E1000_REG_STATUS);
    g_ready = 1u;

    if (osaura_hot_bind(OSAURA_HOT_BANK_NETWORK, OSAURA_NET_HOT_RX_FRAME, hot_rx_frame, 0) != 0 ||
        osaura_hot_bind(OSAURA_HOT_BANK_NETWORK, OSAURA_NET_HOT_TX_FRAME, hot_tx_frame, 0) != 0) {
        g_ready = 0u;
        return 0;
    }
    return 1;
}

int osaura_e1000_ready(void) {
    return g_ready != 0u;
}

const osaura_mac_address *osaura_e1000_mac(void) {
    return g_ready ? &g_mac : NULL;
}

int osaura_e1000_transmit(const void *frame, uint16_t bytes) {
    e1000_hot_request request = {0};
    request.const_frame = frame;
    request.bytes = bytes;
    return osaura_hot_dispatch_opcode(
        osaura_hot_opcode(OSAURA_HOT_BANK_NETWORK, OSAURA_NET_HOT_TX_FRAME),
        &request);
}

int osaura_e1000_receive(void *frame, uint16_t capacity, uint16_t *bytes_out) {
    e1000_hot_request request = {0};
    request.frame = frame;
    request.capacity = capacity;
    request.bytes_out = bytes_out;
    return osaura_hot_dispatch_opcode(
        osaura_hot_opcode(OSAURA_HOT_BANK_NETWORK, OSAURA_NET_HOT_RX_FRAME),
        &request);
}
