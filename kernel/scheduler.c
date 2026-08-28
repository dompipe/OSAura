#include "scheduler.h"
#include "jx-runtime.h"

#include <stddef.h>
#include <stdint.h>

#define TASK_STACK_BYTES 16384u
#define TASK_STACK_QWORDS (TASK_STACK_BYTES / sizeof(uint64_t))
#define KERNEL_CS 0x08ull
#define KERNEL_SS 0x10ull
#define INITIAL_RFLAGS 0x202ull
#define SAVED_GPRS_QWORDS 15u
#define IRET64_QWORDS 5u
#define SAVED_FRAME_QWORDS (SAVED_GPRS_QWORDS + IRET64_QWORDS)
#define FRAME_WITH_RETURN_QWORDS (SAVED_FRAME_QWORDS + 1u)

typedef struct {
    uint64_t *saved_rsp;
    uint64_t ticks;
    uint64_t switches;
    uint8_t ready;
} osaura_task;

static osaura_task g_tasks[OSAURA_TASK_MAX];
static uint64_t g_jx_stack[TASK_STACK_QWORDS] __attribute__((aligned(16)));
static uint64_t g_idle_stack[TASK_STACK_QWORDS] __attribute__((aligned(16)));
static uint32_t g_current;
static uint32_t g_quantum_ticks;
static uint8_t g_initialized;
static uint8_t g_running;

__attribute__((noreturn)) static void task_returned(void) {
    for (;;) __asm__ volatile("cli; hlt");
}

__attribute__((noreturn, noinline)) static void osaura_idle_task(void) {
    for (;;) __asm__ volatile("hlt");
}

/*
 * IRQ0's assembly restore path consumes this exact long-mode stack image:
 *
 *   r15..rax (15 qwords)
 *   RIP
 *   CS
 *   RFLAGS
 *   RSP
 *   SS
 *
 * Intel 64 IRETQ pops SS:RSP unconditionally in 64-bit mode. Real hardware
 * interrupts already provide the five-word IRET frame; synthetic first-run
 * task frames must do the same or IRETQ will interpret whatever follows
 * RFLAGS as the new task stack pointer and stack selector.
 *
 * The return slot lives at the naturally aligned top of the task stack. The
 * synthetic IRET frame loads RSP with its address, which gives a C entry point
 * the normal System V x86-64 entry alignment and a deterministic return trap.
 */
static uint64_t *build_initial_frame(uint64_t *stack,
                                     size_t qwords,
                                     void (*entry)(void)) {
    if (!stack || qwords <= FRAME_WITH_RETURN_QWORDS || !entry) return 0;

    uint64_t *top = stack + qwords;
    uint64_t *return_slot = top - 1u;
    uint64_t *frame = return_slot - SAVED_FRAME_QWORDS;

    for (size_t i = 0; i < SAVED_FRAME_QWORDS; ++i) frame[i] = 0;

    frame[15] = (uint64_t)(uintptr_t)entry;
    frame[16] = KERNEL_CS;
    frame[17] = INITIAL_RFLAGS;
    frame[18] = (uint64_t)(uintptr_t)return_slot;
    frame[19] = KERNEL_SS;
    *return_slot = (uint64_t)(uintptr_t)&task_returned;
    return frame;
}

void osaura_scheduler_init(void) {
    for (uint32_t i = 0; i < OSAURA_TASK_MAX; ++i) {
        g_tasks[i].saved_rsp = 0;
        g_tasks[i].ticks = 0;
        g_tasks[i].switches = 0;
        g_tasks[i].ready = 0;
    }

    /* Task zero is the bootstrap shell. Its frame is captured by IRQ0. */
    g_tasks[0].ready = 1u;

    /* Task one is the native JX applied-runtime service. */
    g_tasks[1].saved_rsp = build_initial_frame(g_jx_stack,
                                               TASK_STACK_QWORDS,
                                               osaura_jx_runtime_task);
    g_tasks[1].ready = g_tasks[1].saved_rsp != 0;

    /* Task two is the kernel idle thread used when no service work is needed. */
    g_tasks[2].saved_rsp = build_initial_frame(g_idle_stack,
                                               TASK_STACK_QWORDS,
                                               osaura_idle_task);
    g_tasks[2].ready = g_tasks[2].saved_rsp != 0;

    g_current = 0;
    g_quantum_ticks = 0;
    g_running = 0;
    g_initialized = g_tasks[1].ready && g_tasks[2].ready;
}

void osaura_scheduler_start(void) {
    if (g_initialized) g_running = 1u;
}

static uint32_t next_ready_task(uint32_t current) {
    for (uint32_t step = 1; step <= OSAURA_TASK_MAX; ++step) {
        uint32_t candidate = (current + step) % OSAURA_TASK_MAX;
        if (g_tasks[candidate].ready) return candidate;
    }
    return current;
}

uint64_t *osaura_scheduler_on_timer(uint64_t *interrupt_frame) {
    if (!interrupt_frame) return interrupt_frame;
    if (!g_initialized || !g_running) return interrupt_frame;

    g_tasks[g_current].ticks++;
    if (++g_quantum_ticks < OSAURA_SCHEDULER_QUANTUM_TICKS)
        return interrupt_frame;
    g_quantum_ticks = 0;

    g_tasks[g_current].saved_rsp = interrupt_frame;
    uint32_t next = next_ready_task(g_current);
    if (next == g_current || !g_tasks[next].saved_rsp)
        return interrupt_frame;

    g_tasks[g_current].switches++;
    g_current = next;
    return g_tasks[next].saved_rsp;
}

uint32_t osaura_scheduler_task_count(void) {
    return g_initialized ? OSAURA_TASK_MAX : 1u;
}

uint32_t osaura_scheduler_current_task(void) {
    return g_current;
}

const char *osaura_scheduler_task_name(uint32_t task_id) {
    switch (task_id) {
        case 0: return "SHELL";
        case 1: return "JX-RUNTIME";
        case 2: return "IDLE";
        default: return "UNKNOWN";
    }
}

uint64_t osaura_scheduler_task_ticks(uint32_t task_id) {
    return task_id < OSAURA_TASK_MAX ? g_tasks[task_id].ticks : 0;
}

uint64_t osaura_scheduler_task_switches(uint32_t task_id) {
    return task_id < OSAURA_TASK_MAX ? g_tasks[task_id].switches : 0;
}

int osaura_scheduler_running(void) {
    return g_initialized && g_running;
}
