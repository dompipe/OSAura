#ifndef OSAURA_SCHEDULER_H
#define OSAURA_SCHEDULER_H

#include <stdint.h>

#define OSAURA_TASK_MAX 16u
#define OSAURA_BOOT_TASK_COUNT 3u
#define OSAURA_JOB_MAX 16u
#define OSAURA_SCHEDULER_QUANTUM_TICKS 5u
#define OSAURA_TASK_NONE UINT32_MAX

/* Bank 3 / opcodes 0x98..0x9f. */
#define OSAURA_JOB_HOT_ATTACH       0u
#define OSAURA_JOB_HOT_BACKGROUND   1u
#define OSAURA_JOB_HOT_FOREGROUND   2u
#define OSAURA_JOB_HOT_SET_STATE    3u
#define OSAURA_JOB_HOT_GET_FG       4u
#define OSAURA_JOB_HOT_BG_COUNT     5u
#define OSAURA_JOB_HOT_BG_AT        6u
#define OSAURA_JOB_HOT_WAKE         7u

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

typedef struct {
    uint32_t subject;
    uint32_t task_id;
    uint32_t value;
    uint32_t stack_index;
    osaura_task_state state;
    uint8_t terminal_id;
} osaura_job_hot_request;

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
uint32_t osaura_scheduler_task_subject(uint32_t task_id);
int osaura_scheduler_running(void);

/* Subject-aware job-control APIs. Non-kernel callers require TASK_CONTROL. */
int osaura_scheduler_attach_program_as(uint32_t subject, uint32_t task_id, uint8_t terminal_id);
int osaura_scheduler_background_as(uint32_t subject, uint8_t terminal_id, uint32_t *task_id);
int osaura_scheduler_foreground_as(uint32_t subject, uint8_t terminal_id, uint32_t *task_id);
int osaura_scheduler_set_task_state_as(uint32_t subject, uint32_t task_id, osaura_task_state state);
int osaura_scheduler_wake_as(uint32_t subject, uint32_t task_id);

/* Read-only job queries remain safe for all callers. */
uint32_t osaura_scheduler_foreground_task(uint8_t terminal_id);
uint32_t osaura_scheduler_background_count(void);
uint32_t osaura_scheduler_background_task(uint32_t stack_index);

/* Kernel-subject compatibility wrappers. */
int osaura_scheduler_attach_program(uint32_t task_id, uint8_t terminal_id);
int osaura_scheduler_background(uint8_t terminal_id, uint32_t *task_id);
int osaura_scheduler_foreground(uint8_t terminal_id, uint32_t *task_id);
int osaura_scheduler_set_task_state(uint32_t task_id, osaura_task_state state);
int osaura_scheduler_wake(uint32_t task_id);

#endif
