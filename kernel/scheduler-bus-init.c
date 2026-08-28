#include "scheduler.h"
#include "processor-bus-scheduler.h"

/* scheduler.c is compiled with its init symbol renamed to this core entry. */
void osaura_scheduler_core_init(void);

void osaura_scheduler_init(void) {
    osaura_scheduler_core_init();
    (void)osaura_processor_bus_bind_scheduler(0u);
}
