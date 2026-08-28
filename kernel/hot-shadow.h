#ifndef OSAURA_HOT_SHADOW_H
#define OSAURA_HOT_SHADOW_H

#include <stdint.h>

/*
 * Kernel-wide hot-dispatch law.
 *
 * Every hot subsystem owns exactly eight prelinked shadows. A selector is
 * always three bits; canonical names, validation, parsing and policy stay on
 * cold paths. This mirrors JX's r0..r7 / 3-bit hot-register discipline while
 * keeping the kernel language-neutral.
 */
#define OSAURA_SHADOW_SLOT_COUNT 8u
#define OSAURA_SHADOW_SLOT_MASK  0x07u

typedef int (*osaura_shadow_fn)(void *context, void *request);

typedef struct {
    osaura_shadow_fn fn;
    void *context;
    uint32_t hits;
} osaura_shadow_slot;

typedef struct {
    osaura_shadow_slot slot[OSAURA_SHADOW_SLOT_COUNT];
} osaura_shadow_table;

static inline void osaura_shadow_table_init(osaura_shadow_table *table) {
    if (!table) return;
    for (uint32_t i = 0u; i < OSAURA_SHADOW_SLOT_COUNT; ++i) {
        table->slot[i].fn = 0;
        table->slot[i].context = 0;
        table->slot[i].hits = 0u;
    }
}

static inline int osaura_shadow_bind(osaura_shadow_table *table,
                                     uint8_t selector,
                                     osaura_shadow_fn fn,
                                     void *context) {
    if (!table || !fn || selector >= OSAURA_SHADOW_SLOT_COUNT) return -1;
    table->slot[selector].fn = fn;
    table->slot[selector].context = context;
    table->slot[selector].hits = 0u;
    return 0;
}

static inline int osaura_shadow_dispatch(osaura_shadow_table *table,
                                         uint8_t selector,
                                         void *request) {
    if (!table || selector >= OSAURA_SHADOW_SLOT_COUNT) return -1;
    osaura_shadow_slot *slot = &table->slot[selector & OSAURA_SHADOW_SLOT_MASK];
    if (!slot->fn) return -2;
    if (slot->hits != UINT32_MAX) ++slot->hits;
    return slot->fn(slot->context, request);
}

#endif
