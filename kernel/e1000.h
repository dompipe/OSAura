#ifndef OSAURA_E1000_H
#define OSAURA_E1000_H

#include <stdint.h>

typedef struct {
    uint8_t bytes[6];
} osaura_mac_address;

int osaura_e1000_init(uint8_t bus, uint8_t device, uint8_t function);
int osaura_e1000_ready(void);
const osaura_mac_address *osaura_e1000_mac(void);
int osaura_e1000_transmit_as(uint32_t subject, const void *frame, uint16_t bytes);
int osaura_e1000_transmit(const void *frame, uint16_t bytes);
int osaura_e1000_receive(void *frame, uint16_t capacity, uint16_t *bytes_out);

#endif
