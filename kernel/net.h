#ifndef OSAURA_NET_H
#define OSAURA_NET_H

#include <stdint.h>

/* Bank 2 / opcodes 0x90..0x97. */
#define OSAURA_NET_HOT_RX_FRAME     0u
#define OSAURA_NET_HOT_TX_FRAME     1u
#define OSAURA_NET_HOT_POLL         2u
#define OSAURA_NET_HOT_ARP_RESOLVE  3u
#define OSAURA_NET_HOT_IPV4_SEND    4u
#define OSAURA_NET_HOT_UDP_SEND     5u
#define OSAURA_NET_HOT_TCP_STEP     6u
#define OSAURA_NET_HOT_WAKE         7u

typedef void (*osaura_net_write_fn)(const char *text);

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
} osaura_net_device;

void osaura_net_init(void);
uint32_t osaura_net_device_count(void);
const osaura_net_device *osaura_net_device_at(uint32_t index);

/*
 * Native pre-JX11 recovery/network command set. Text parsing remains cold.
 * Packet/repeat work enters the eight bank-2 shadows after resolution.
 */
int osaura_net_command(const char *line, osaura_net_write_fn write);

#endif
