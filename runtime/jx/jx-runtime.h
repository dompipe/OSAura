#ifndef OSAURA_JX_RUNTIME_H
#define OSAURA_JX_RUNTIME_H

#include <stdint.h>

/* Mirrors dompipe/jx AppliedBytecode::VERSION = jx.applied-bytecode/1. */
#define OSAURA_JX_APPLIED_ABI 1u
#define OSAURA_JX_RUNTIME_PAGE_BYTES 6u

__attribute__((noreturn)) void osaura_jx_runtime_task(void);

int osaura_jx_runtime_active(void);
uint64_t osaura_jx_runtime_heartbeat(void);
uint64_t osaura_jx_runtime_bus_ticks(void);
uint64_t osaura_jx_runtime_bus_collects(void);
uint64_t osaura_jx_runtime_errors(void);

#endif
