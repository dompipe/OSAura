#include "processor-bus-scheduler.h"
#include "processor-bus.h"
#include "scheduler.h"

#include <stdint.h>

static uint8_t g_foreground_terminal;

static uint32_t scheduler_task_count(void *context) {
    (void)context;
    uint32_t count = osaura_scheduler_task_count();
    return count > OSAURA_TASK_MAX ? OSAURA_TASK_MAX : count;
}

static int scheduler_is_program(uint32_t pid, void *context) {
    (void)context;
    if (pid >= OSAURA_TASK_MAX) return 0;
    if (osaura_scheduler_task_state(pid) == OSAURA_TASK_UNUSED) return 0;
    return osaura_scheduler_task_role(pid) == OSAURA_TASK_ROLE_PROGRAM;
}

static uint32_t scheduler_foreground(void *context) {
    (void)context;
    return osaura_scheduler_foreground_task(g_foreground_terminal);
}

static int scheduler_wake(uint32_t pid, void *context) {
    (void)context;
    return osaura_scheduler_wake(pid);
}

static int scheduler_sleep(uint32_t pid, void *context) {
    (void)context;
    return osaura_scheduler_set_task_state(pid, OSAURA_TASK_BLOCKED);
}

int osaura_processor_bus_bind_scheduler(uint8_t foreground_terminal) {
    static const osaura_processor_bus_backend backend = {
        scheduler_task_count,
        scheduler_is_program,
        scheduler_foreground,
        scheduler_wake,
        scheduler_sleep
    };
    g_foreground_terminal = foreground_terminal;
    return osaura_processor_bus_init(&backend, 0);
}

uint8_t osaura_processor_bus_foreground_terminal(void) {
    return g_foreground_terminal;
}
