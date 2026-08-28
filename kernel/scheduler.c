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
#define OSAURA_TERMINAL_MAX 8u

typedef struct {
    uint64_t *saved_rsp;
    uint64_t ticks;
    uint64_t switches;
    const char *name;
    osaura_task_state state;
    osaura_task_role role;
    uint8_t terminal_id;
    uint8_t background;
} osaura_task;

/* Set by the UEFI loader before ExitBootServices. */
extern uint64_t osaura_jx_boot_book;
extern uint64_t osaura_jx_boot_book_size;
extern uint64_t osaura_jx_next_book;
extern uint64_t osaura_jx_next_book_size;

static osaura_task g_tasks[OSAURA_TASK_MAX];
static uint64_t g_jx_stack[TASK_STACK_QWORDS] __attribute__((aligned(16)));
static uint64_t g_idle_stack[TASK_STACK_QWORDS] __attribute__((aligned(16)));
static uint32_t g_foreground[OSAURA_TERMINAL_MAX];
static uint32_t g_background_stack[OSAURA_JOB_MAX];
static uint32_t g_background_count;
static uint32_t g_current;
static uint32_t g_quantum_ticks;
static uint32_t g_task_high_water;
static uint8_t g_initialized;
static uint8_t g_running;

__attribute__((noreturn)) static void task_returned(void) {
    for (;;) __asm__ volatile("cli; hlt");
}

__attribute__((noreturn, noinline)) static void osaura_idle_task(void) {
    for (;;) __asm__ volatile("hlt");
}

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

static void task_clear(osaura_task *task) {
    if (!task) return;
    task->saved_rsp = 0;
    task->ticks = 0;
    task->switches = 0;
    task->name = "UNUSED";
    task->state = OSAURA_TASK_UNUSED;
    task->role = OSAURA_TASK_ROLE_KERNEL;
    task->terminal_id = 0u;
    task->background = 0u;
}

static int task_schedulable(uint32_t id) {
    return id < OSAURA_TASK_MAX &&
           g_tasks[id].state == OSAURA_TASK_RUNNABLE &&
           (id == 0u || g_tasks[id].saved_rsp != 0);
}

void osaura_scheduler_init(void) {
    for (uint32_t i = 0; i < OSAURA_TASK_MAX; ++i) task_clear(&g_tasks[i]);
    for (uint32_t i = 0; i < OSAURA_TERMINAL_MAX; ++i) g_foreground[i] = OSAURA_TASK_NONE;
    for (uint32_t i = 0; i < OSAURA_JOB_MAX; ++i) g_background_stack[i] = OSAURA_TASK_NONE;
    g_background_count = 0u;

    /* Task zero is the bootstrap shell. Its frame is captured by IRQ0. */
    g_tasks[0].name = "SHELL";
    g_tasks[0].state = OSAURA_TASK_RUNNABLE;
    g_tasks[0].role = OSAURA_TASK_ROLE_KERNEL;

    /*
     * Task one is the JX service runtime. It is not a user job: fg/bg must not
     * accidentally detach the language runtime that services all terminals.
     */
    int jx_book_ok = osaura_jx_runtime_load_book(
                         (const void *)(uintptr_t)osaura_jx_boot_book,
                         osaura_jx_boot_book_size) == 0;
    int jx_candidate_ok = jx_book_ok &&
                          osaura_jx_runtime_queue_book(
                              (const void *)(uintptr_t)osaura_jx_next_book,
                              osaura_jx_next_book_size) == 0;
    g_tasks[1].name = "JX-RUNTIME";
    g_tasks[1].role = OSAURA_TASK_ROLE_SERVICE;
    if (jx_candidate_ok) {
        g_tasks[1].saved_rsp = build_initial_frame(g_jx_stack,
                                                   TASK_STACK_QWORDS,
                                                   osaura_jx_runtime_task);
    }
    g_tasks[1].state = (jx_candidate_ok && g_tasks[1].saved_rsp) ?
                       OSAURA_TASK_RUNNABLE : OSAURA_TASK_UNUSED;

    /* Task two is the kernel idle thread used when no service work is needed. */
    g_tasks[2].name = "IDLE";
    g_tasks[2].role = OSAURA_TASK_ROLE_KERNEL;
    g_tasks[2].saved_rsp = build_initial_frame(g_idle_stack,
                                               TASK_STACK_QWORDS,
                                               osaura_idle_task);
    g_tasks[2].state = g_tasks[2].saved_rsp ? OSAURA_TASK_RUNNABLE : OSAURA_TASK_UNUSED;

    g_current = 0u;
    g_quantum_ticks = 0u;
    g_task_high_water = OSAURA_BOOT_TASK_COUNT;
    g_running = 0u;
    g_initialized = g_tasks[1].state == OSAURA_TASK_RUNNABLE &&
                    g_tasks[2].state == OSAURA_TASK_RUNNABLE;
}

void osaura_scheduler_start(void) {
    if (g_initialized) g_running = 1u;
}

static uint32_t next_ready_task(uint32_t current) {
    for (uint32_t step = 1; step <= OSAURA_TASK_MAX; ++step) {
        uint32_t candidate = (current + step) % OSAURA_TASK_MAX;
        if (task_schedulable(candidate)) return candidate;
    }
    return current;
}

uint64_t *osaura_scheduler_on_timer(uint64_t *interrupt_frame) {
    if (!interrupt_frame) return interrupt_frame;
    if (!g_initialized || !g_running) return interrupt_frame;

    if (g_current < OSAURA_TASK_MAX) g_tasks[g_current].ticks++;
    if (++g_quantum_ticks < OSAURA_SCHEDULER_QUANTUM_TICKS)
        return interrupt_frame;
    g_quantum_ticks = 0u;

    if (g_current < OSAURA_TASK_MAX) g_tasks[g_current].saved_rsp = interrupt_frame;
    uint32_t next = next_ready_task(g_current);
    if (next == g_current || !g_tasks[next].saved_rsp)
        return interrupt_frame;

    if (g_current < OSAURA_TASK_MAX) g_tasks[g_current].switches++;
    g_current = next;
    return g_tasks[next].saved_rsp;
}

uint32_t osaura_scheduler_task_count(void) {
    return g_initialized ? g_task_high_water : 1u;
}

uint32_t osaura_scheduler_current_task(void) {
    return g_current;
}

const char *osaura_scheduler_task_name(uint32_t task_id) {
    if (task_id >= OSAURA_TASK_MAX || g_tasks[task_id].state == OSAURA_TASK_UNUSED)
        return "UNUSED";
    return g_tasks[task_id].name ? g_tasks[task_id].name : "TASK";
}

uint64_t osaura_scheduler_task_ticks(uint32_t task_id) {
    return task_id < OSAURA_TASK_MAX ? g_tasks[task_id].ticks : 0u;
}

uint64_t osaura_scheduler_task_switches(uint32_t task_id) {
    return task_id < OSAURA_TASK_MAX ? g_tasks[task_id].switches : 0u;
}

osaura_task_state osaura_scheduler_task_state(uint32_t task_id) {
    return task_id < OSAURA_TASK_MAX ? g_tasks[task_id].state : OSAURA_TASK_UNUSED;
}

osaura_task_role osaura_scheduler_task_role(uint32_t task_id) {
    return task_id < OSAURA_TASK_MAX ? g_tasks[task_id].role : OSAURA_TASK_ROLE_KERNEL;
}

int osaura_scheduler_set_task_state(uint32_t task_id, osaura_task_state state) {
    if (task_id >= OSAURA_TASK_MAX || task_id == 0u || task_id == 2u ||
        g_tasks[task_id].state == OSAURA_TASK_UNUSED || state == OSAURA_TASK_UNUSED)
        return -1;
    g_tasks[task_id].state = state;
    return 0;
}

int osaura_scheduler_attach_program(uint32_t task_id, uint8_t terminal_id) {
    if (task_id >= OSAURA_TASK_MAX || terminal_id >= OSAURA_TERMINAL_MAX ||
        g_tasks[task_id].state == OSAURA_TASK_UNUSED ||
        g_tasks[task_id].role != OSAURA_TASK_ROLE_PROGRAM)
        return -1;
    uint32_t old = g_foreground[terminal_id];
    if (old != OSAURA_TASK_NONE && old < OSAURA_TASK_MAX && old != task_id)
        g_tasks[old].background = 1u;
    g_foreground[terminal_id] = task_id;
    g_tasks[task_id].terminal_id = terminal_id;
    g_tasks[task_id].background = 0u;
    return 0;
}

int osaura_scheduler_background(uint8_t terminal_id, uint32_t *task_id) {
    if (terminal_id >= OSAURA_TERMINAL_MAX || g_background_count >= OSAURA_JOB_MAX)
        return -1;
    uint32_t id = g_foreground[terminal_id];
    if (id == OSAURA_TASK_NONE || id >= OSAURA_TASK_MAX ||
        g_tasks[id].role != OSAURA_TASK_ROLE_PROGRAM ||
        g_tasks[id].state == OSAURA_TASK_UNUSED)
        return -2;
    g_foreground[terminal_id] = OSAURA_TASK_NONE;
    g_tasks[id].background = 1u;
    g_background_stack[g_background_count++] = id;
    if (task_id) *task_id = id;
    return 0;
}

int osaura_scheduler_foreground(uint8_t terminal_id, uint32_t *task_id) {
    if (terminal_id >= OSAURA_TERMINAL_MAX) return -1;
    if (g_foreground[terminal_id] != OSAURA_TASK_NONE) return -2;
    while (g_background_count) {
        uint32_t id = g_background_stack[--g_background_count];
        g_background_stack[g_background_count] = OSAURA_TASK_NONE;
        if (id >= OSAURA_TASK_MAX || g_tasks[id].state == OSAURA_TASK_UNUSED ||
            g_tasks[id].role != OSAURA_TASK_ROLE_PROGRAM || !g_tasks[id].background)
            continue;
        g_tasks[id].background = 0u;
        g_tasks[id].terminal_id = terminal_id;
        g_foreground[terminal_id] = id;
        if (task_id) *task_id = id;
        return 0;
    }
    return -3;
}

uint32_t osaura_scheduler_foreground_task(uint8_t terminal_id) {
    return terminal_id < OSAURA_TERMINAL_MAX ? g_foreground[terminal_id] : OSAURA_TASK_NONE;
}

uint32_t osaura_scheduler_background_count(void) {
    return g_background_count;
}

uint32_t osaura_scheduler_background_task(uint32_t stack_index) {
    if (stack_index >= g_background_count) return OSAURA_TASK_NONE;
    return g_background_stack[stack_index];
}

int osaura_scheduler_running(void) {
    return g_initialized && g_running;
}
