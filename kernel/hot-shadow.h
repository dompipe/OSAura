#ifndef OSAURA_HOT_SHADOW_H
#define OSAURA_HOT_SHADOW_H

#include <stdint.h>

/* Shared with JX ABI v4: [1][bank:4][shadow:3]. */
#define OSAURA_HOT_BASE          0x80u
#define OSAURA_HOT_BANK_COUNT    16u
#define OSAURA_SHADOW_SLOT_COUNT 8u
#define OSAURA_HOT_ENTRY_COUNT   128u
#define OSAURA_HOT_BANK_MASK     0x0Fu
#define OSAURA_SHADOW_SLOT_MASK  0x07u

/* Stable kernel bank ownership. */
#define OSAURA_HOT_BANK_STORAGE  0u
#define OSAURA_HOT_BANK_IPC      1u
#define OSAURA_HOT_BANK_NETWORK  2u
#define OSAURA_HOT_BANK_JOBS     3u
#define OSAURA_HOT_BANK_INPUT    4u
#define OSAURA_HOT_BANK_TERMINAL 5u
#define OSAURA_HOT_BANK_USB      6u
#define OSAURA_HOT_BANK_WIFI     7u
/* 8..15 remain available for measured future hot paths. */

typedef int (*osaura_shadow_fn)(void *context, void *request);

typedef struct {
    osaura_shadow_fn fn;
    void *context;
    uint32_t hits;
} osaura_shadow_slot;

typedef struct {
    /* One cacheable machine-wide map: opcode & 0x7f is the complete index. */
    osaura_shadow_slot slot[OSAURA_HOT_ENTRY_COUNT];
} osaura_shadow_table;

static inline uint8_t osaura_hot_opcode(uint8_t bank, uint8_t shadow) {
    return (uint8_t)(OSAURA_HOT_BASE |
                     ((bank & OSAURA_HOT_BANK_MASK) << 3) |
                     (shadow & OSAURA_SHADOW_SLOT_MASK));
}

static inline uint8_t osaura_hot_bank(uint8_t opcode) {
    return (uint8_t)((opcode >> 3) & OSAURA_HOT_BANK_MASK);
}

static inline uint8_t osaura_hot_shadow(uint8_t opcode) {
    return (uint8_t)(opcode & OSAURA_SHADOW_SLOT_MASK);
}

/*
 * Global kernel hot map. Binding is boot/prelink work. Dispatch is the awake
 * path: mask one byte, fetch one prelinked slot, call native code.
 */
void osaura_hot_init(void);
int osaura_hot_bind(uint8_t bank,
                    uint8_t shadow,
                    osaura_shadow_fn fn,
                    void *context);
int osaura_hot_dispatch_opcode(uint8_t opcode, void *request);
const osaura_shadow_table *osaura_hot_table(void);

static inline int osaura_hot_dispatch(uint8_t bank,
                                      uint8_t shadow,
                                      void *request) {
    if (bank >= OSAURA_HOT_BANK_COUNT || shadow >= OSAURA_SHADOW_SLOT_COUNT)
        return -1;
    return osaura_hot_dispatch_opcode(osaura_hot_opcode(bank, shadow), request);
}

#endif
