#include "hot-shadow.h"
#include "usb-hot.h"
#include "wifi-hot.h"
#include "clock-hot.h"
#include "memory-hot.h"
#include "task-hot.h"
#include "book-hot.h"

#include <stdint.h>

static osaura_shadow_table g_hot;
static uint8_t g_hot_initialized;

void osaura_hot_init(void) {
    for (uint32_t i = 0u; i < OSAURA_HOT_ENTRY_COUNT; ++i) {
        g_hot.slot[i].fn = 0;
        g_hot.slot[i].context = 0;
        g_hot.slot[i].hits = 0u;
    }
    g_hot_initialized = 1u;
    (void)osaura_usb_hot_bind();
    (void)osaura_wifi_hot_bind();
    (void)osaura_clock_hot_bind();
    (void)osaura_memory_hot_bind();
    (void)osaura_task_hot_bind();
    (void)osaura_book_hot_bind();
}

static void ensure_hot_init(void) {
    if (!g_hot_initialized) osaura_hot_init();
}

int osaura_hot_bind(uint8_t bank,
                    uint8_t shadow,
                    osaura_shadow_fn fn,
                    void *context) {
    if (!fn || bank >= OSAURA_HOT_BANK_COUNT || shadow >= OSAURA_SHADOW_SLOT_COUNT)
        return -1;
    ensure_hot_init();
    const uint8_t index = (uint8_t)((bank << 3) | shadow);
    g_hot.slot[index].fn = fn;
    g_hot.slot[index].context = context;
    g_hot.slot[index].hits = 0u;
    return 0;
}

int osaura_hot_dispatch_opcode(uint8_t opcode, void *request) {
    if ((opcode & OSAURA_HOT_BASE) == 0u) return -1;
    osaura_shadow_slot *slot = &g_hot.slot[opcode & 0x7fu];
    if (!slot->fn) return -2;
#if defined(OSAURA_HOT_PROFILE) && OSAURA_HOT_PROFILE
    if (slot->hits != UINT32_MAX) ++slot->hits;
#endif
    return slot->fn(slot->context, request);
}

const osaura_shadow_table *osaura_hot_table(void) {
    ensure_hot_init();
    return &g_hot;
}
