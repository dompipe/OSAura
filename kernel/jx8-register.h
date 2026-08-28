#ifndef OSAURA_JX8_REGISTER_H
#define OSAURA_JX8_REGISTER_H

#include <stdint.h>

#define OSAURA_JX8_REGISTER_COUNT 256u
#define OSAURA_JX8_WINDOW_SLOTS   8u

/* Banks 14 and 15 / opcodes 0xF0..0xFF. */
#define OSAURA_HOT_BANK_JX8_READ  14u
#define OSAURA_HOT_BANK_JX8_WRITE 15u

typedef struct {
    uint64_t reg[OSAURA_JX8_REGISTER_COUNT];
    uint8_t window[OSAURA_JX8_WINDOW_SLOTS];
} osaura_jx8_register_file;

typedef struct {
    osaura_jx8_register_file *file;
    uint64_t value;
} osaura_jx8_register_request;

void osaura_jx8_register_init(osaura_jx8_register_file *file);
int osaura_jx8_register_set_window(osaura_jx8_register_file *file,
                                  const uint8_t register_ids[OSAURA_JX8_WINDOW_SLOTS]);
int osaura_jx8_register_hot_bind(void);
int osaura_jx8_read_slot(osaura_jx8_register_file *file, uint8_t slot, uint64_t *value);
int osaura_jx8_write_slot(osaura_jx8_register_file *file, uint8_t slot, uint64_t value);

#endif
