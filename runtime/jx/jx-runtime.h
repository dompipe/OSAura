#ifndef OSAURA_JX_RUNTIME_H
#define OSAURA_JX_RUNTIME_H

#include <stdint.h>

/* Mirrors dompipe/jx AppliedBytecode::VERSION = jx.applied-bytecode/1. */
#define OSAURA_JX_APPLIED_ABI 1u
#define OSAURA_JX_RUNTIME_PAGE_BYTES 6u
#define OSAURA_JX_RUNTIME_TICK_OFFSET 0u
#define OSAURA_JX_RUNTIME_COLLECT_OFFSET 3u

/* Load the boot Book and queue a second memory-resident Book for live cutover. */
int osaura_jx_runtime_load_book(const void *bytes, uint64_t size);
int osaura_jx_runtime_queue_book(const void *bytes, uint64_t size);
int osaura_jx_runtime_book_loaded(void);
int osaura_jx_runtime_candidate_queued(void);

__attribute__((noreturn)) void osaura_jx_runtime_task(void);

int osaura_jx_runtime_active(void);
uint64_t osaura_jx_runtime_heartbeat(void);
uint64_t osaura_jx_runtime_bus_ticks(void);
uint64_t osaura_jx_runtime_bus_collects(void);
uint64_t osaura_jx_runtime_bag_revision(void);
uint64_t osaura_jx_runtime_bag_checkpoints(void);
uint64_t osaura_jx_runtime_channel_messages(void);
uint64_t osaura_jx_runtime_channel_deliveries(void);
uint64_t osaura_jx_runtime_channel_switches(void);
uint64_t osaura_jx_runtime_prepared_calls(void);
uint64_t osaura_jx_runtime_hot_dispatches(void);
uint64_t osaura_jx_runtime_reaction_runs(void);
uint64_t osaura_jx_runtime_reaction_value(void);
uint64_t osaura_jx_runtime_generation_swaps(void);
uint64_t osaura_jx_runtime_active_generation(void);
uint64_t osaura_jx_runtime_previous_generation(void);
uint64_t osaura_jx_runtime_live_book_activations(void);
uint64_t osaura_jx_runtime_errors(void);

#endif
