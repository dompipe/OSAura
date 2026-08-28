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

/* Initial kernel bank ownership. Keep this stable across releases. */
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
    /* Flat so opcode & 0x7f is the complete lookup index. */
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

static inline void osaura_shadow_table_init(osaura_shadow_table *table) {
    if (!table) return;
    for (uint32_t i = 0u; i < OSAURA_HOT_ENTRY_COUNT; ++i) {
        table->slot[i].fn = 0;
        table->slot[i].context = 0;
        table->slot[i].hits = 0u;
    }
}

static inline int osaura_shadow_bind(osaura_shadow_table *table,
                                     uint8_t bank,
                                     uint8_t shadow,
                                     osaura_shadow_fn fn,
                                     void *context) {
    if (!table || !fn || bank >= OSAURA_HOT_BANK_COUNT ||
        shadow >= OSAURA_SHADOW_SLOT_COUNT) return -1;
    uint8_t index = (uint8_t)((bank << 3) | shadow);
    table->slot[index].fn = fn;
    table->slot[index].context = context;
    table->slot[index].hits = 0u;
    return 0;
}

/* Fast entry: caller supplies a complete MSB=1 opcode. */
static inline int osaura_shadow_dispatch_opcode(osaura_shadow_table *table,
                                                 uint8_t opcode,
                                                 void *request) {
    if (!table || (opcode & OSAURA_HOT_BASE) == 0u) return -1;
    osaura_shadow_slot *slot = &table->slot[opcode & 0x7Fu];
    if (!slot->fn) return -2;
    if (slot->hits != UINT32_MAX) ++slot->hits;
    return slot->fn(slot->context, request);
}

/* Convenience path for cold/control code; hot loops should cache the opcode. */
static inline int osaura_shadow_dispatch(osaura_shadow_table *table,
                                         uint8_t bank,
                                         uint8_t shadow,
                                         void *request) {
    if (bank >= OSAURA_HOT_BANK_COUNT || shadow >= OSAURA_SHADOW_SLOT_COUNT)
        return -1;
    return osaura_shadow_dispatch_opcode(table, osaura_hot_opcode(bank, shadow), request);
}

#endif
