#ifdef _WIN64

#include "processor-bus-win64.h"
#include "runtime64.h"
#include "../../kernel/processor-bus.h"
#include "../../kernel/security.h"

#include <stdint.h>

static uint32_t task64_count(void *context) {
    (void)context;
    return osaura_windows_task64_count();
}

static int task64_is_program(uint32_t pid, void *context) {
    (void)context;
    osaura_windows_task64_info info = {0};
    if (osaura_windows_task64_get_info(pid, &info) != 0) return 0;
    return info.role == OSAURA_TASK_ROLE_PROGRAM;
}

static uint32_t task64_foreground(void *context) {
    (void)context;
    return osaura_windows_job64_foreground();
}

static int task64_is_awake(uint32_t pid, void *context) {
    (void)context;
    osaura_windows_task64_info info = {0};
    if (osaura_windows_task64_get_info(pid, &info) != 0) return -1;
    return info.state == OSAURA_TASK_RUNNABLE ? 1 : 0;
}

static int task64_wake(uint32_t pid, void *context) {
    (void)context;
    return osaura_windows_task64_set_state_as(OSAURA_SECURITY_KERNEL_SUBJECT,
                                              pid,
                                              OSAURA_TASK_RUNNABLE);
}

static int task64_sleep(uint32_t pid, void *context) {
    (void)context;
    return osaura_windows_task64_set_state_as(OSAURA_SECURITY_KERNEL_SUBJECT,
                                              pid,
                                              OSAURA_TASK_BLOCKED);
}

int osaura_windows_processor_bus64_bind(void) {
    static const osaura_processor_bus_backend backend = {
        task64_count,
        task64_is_program,
        task64_foreground,
        task64_is_awake,
        task64_wake,
        task64_sleep
    };
    return osaura_processor_bus_init(&backend, 0);
}

#endif
