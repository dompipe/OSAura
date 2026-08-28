#ifndef OSAURA_NET_H
#define OSAURA_NET_H

#include <stdint.h>

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
 * Native pre-JX11 recovery/network command set.
 * Returns 1 when the command name belongs to the network utility family.
 * Backends that are not online must report that fact rather than pretending
 * an operation completed.
 */
int osaura_net_command(const char *line, osaura_net_write_fn write);

#endif
