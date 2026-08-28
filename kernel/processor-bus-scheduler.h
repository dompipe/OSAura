#ifndef OSAURA_PROCESSOR_BUS_SCHEDULER_H
#define OSAURA_PROCESSOR_BUS_SCHEDULER_H

#include <stdint.h>

/* Bind the processor multiplex bus to OSAura's task scheduler. */
int osaura_processor_bus_bind_scheduler(uint8_t foreground_terminal);
uint8_t osaura_processor_bus_foreground_terminal(void);

#endif
