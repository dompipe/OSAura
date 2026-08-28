#include "jx-runtime.h"

#include <stddef.h>
#include <stdint.h>

#define JX_SYSTEM_ESCAPE 0x7fu
#define JX_SYSTEM_BUS 0x00u
#define JX_BUS_TICK 0x01u
#define JX_BUS_COLLECT 0x02u
#define JX_SYSTEM_BYTES 3u

/*
 * Canonical source remains in dompipe/jx. OSAura consumes the stable applied
 * host ABI after JX lowering. AppliedBytecode::runtimeBusPage() is exactly:
 *
 *   7f 00 01   BUS.TICK
 *   7f 00 02   BUS.COLLECT
 *
 * This bootstrap service deliberately implements only that six-byte runtime
 * page. It proves that JX applied execution is running as a preempted OSAura
 * task before .64B loading, Bags, channels, and the full bus are migrated.
 */
static const uint8_t g_runtime_page[OSAURA_JX_RUNTIME_PAGE_BYTES] = {
    JX_SYSTEM_ESCAPE, JX_SYSTEM_BUS, JX_BUS_TICK,
    JX_SYSTEM_ESCAPE, JX_SYSTEM_BUS, JX_BUS_COLLECT
};

static volatile uint8_t g_active;
static volatile uint64_t g_heartbeat;
static volatile uint64_t g_bus_ticks;
static volatile uint64_t g_bus_collects;
static volatile uint64_t g_errors;
static uint32_t g_pc;

static void execute_applied_entry(void) {
    uint32_t pc = g_pc;
    if (pc + JX_SYSTEM_BYTES > OSAURA_JX_RUNTIME_PAGE_BYTES) {
        ++g_errors;
        g_pc = 0;
        return;
    }

    const uint8_t *op = &g_runtime_page[pc];
    if (op[0] != JX_SYSTEM_ESCAPE || op[1] != JX_SYSTEM_BUS) {
        ++g_errors;
        g_pc = 0;
        return;
    }

    if (op[2] == JX_BUS_TICK) {
        ++g_bus_ticks;
    } else if (op[2] == JX_BUS_COLLECT) {
        ++g_bus_collects;
    } else {
        ++g_errors;
    }

    ++g_heartbeat;
    pc += JX_SYSTEM_BYTES;
    g_pc = pc < OSAURA_JX_RUNTIME_PAGE_BYTES ? pc : 0u;
}

__attribute__((noreturn)) void osaura_jx_runtime_task(void) {
    g_active = 1u;
    for (;;) {
        execute_applied_entry();
        __asm__ volatile("hlt");
    }
}

int osaura_jx_runtime_active(void) {
    return g_active != 0u;
}

uint64_t osaura_jx_runtime_heartbeat(void) {
    return g_heartbeat;
}

uint64_t osaura_jx_runtime_bus_ticks(void) {
    return g_bus_ticks;
}

uint64_t osaura_jx_runtime_bus_collects(void) {
    return g_bus_collects;
}

uint64_t osaura_jx_runtime_errors(void) {
    return g_errors;
}
