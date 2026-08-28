#ifndef OSAURA_SCHEDULER_H
#define OSAURA_SCHEDULER_H

#include <stdint.h>

#define OSAURA_TASK_MAX 16u
#define OSAURA_BOOT_TASK_COUNT 3u
#define OSAURA_JOB_MAX 16u
#define OSAURA_SCHEDULER_QUANTUM_TICKS 5u
#define OSAURA_TASK_NONE UINT32_MAX

typedef enum {
    OSAURA_TASK_UNUSED = 0,
    OSAURA_TASK_RUNNABLE = 1,
    OSAURA_TASK_BLOCKED = 2,
    OSAURA_TASK_STOPPED = 3
} osaura_task_state;

typedef enum {
    OSAURA_TASK_ROLE_KERNEL = 0,
    OSAURA_TASK_ROLE_SERVICE = 1,
    OSAURA_TASK_ROLE_PROGRAM = 2
} osaura_task_role;

void osaura_scheduler_init(void);
void osaura_scheduler_start(void);
uint64_t *osaura_scheduler_on_timer(uint64_t *interrupt_frame);

uint32_t osaura_scheduler_task_count(void);
uint32_t osaura_scheduler_current_task(void);
const char *osaura_scheduler_task_name(uint32_t task_id);
uint64_t osaura_scheduler_task_ticks(uint32_t task_id);
uint64_t osaura_scheduler_task_switches(uint32_t task_id);
osaura_task_state osaura_scheduler_task_state(uint32_t task_id);
osaura_task_role osaura_scheduler_task_role(uint32_t task_id);
int osaura_scheduler_running(void);

/*
 * Kernel job-control boundary.
 *
 * A program task may be attached to a terminal foreground or placed in the
 * background while remaining runnable. bg pushes onto a LIFO stack; fg pops
 * the most recently backgrounded valid program. Terminal switching never
 * changes scheduling state.
 */
int osaura_scheduler_attach_program(uint32_t task_id, uint8_t terminal_id);
int osaura_scheduler_background(uint8_t terminal_id, uint32_t *task_id);
int osaura_scheduler_foreground(uint8_t terminal_id, uint32_t *task_id);
uint32_t osaura_scheduler_foreground_task(uint8_t terminal_id);
uint32_t osaura_scheduler_background_count(void);
uint32_t osaura_scheduler_background_task(uint32_t stack_index);
int osaura_scheduler_set_task_state(uint32_t task_id, osaura_task_state state);

#endif
