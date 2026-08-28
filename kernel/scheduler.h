#ifndef OSAURA_SCHEDULER_H
#define OSAURA_SCHEDULER_H

#include <stdint.h>

#define OSAURA_TASK_MAX 3u
#define OSAURA_SCHEDULER_QUANTUM_TICKS 5u

void osaura_scheduler_init(void);
void osaura_scheduler_start(void);
uint64_t *osaura_scheduler_on_timer(uint64_t *interrupt_frame);

uint32_t osaura_scheduler_task_count(void);
uint32_t osaura_scheduler_current_task(void);
const char *osaura_scheduler_task_name(uint32_t task_id);
uint64_t osaura_scheduler_task_ticks(uint32_t task_id);
uint64_t osaura_scheduler_task_switches(uint32_t task_id);
int osaura_scheduler_running(void);

#endif
