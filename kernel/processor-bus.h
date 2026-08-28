#ifndef OSAURA_PROCESSOR_BUS_H
#define OSAURA_PROCESSOR_BUS_H

#include <stdint.h>
#include <stddef.h>

#define OSAURA_PROCESSOR_BUS_MAX_PROGRAMS 16u
#define OSAURA_PROCESSOR_BUS_PID_NONE UINT32_MAX
#define OSAURA_PROCESSOR_BUS_ROUTE_BITS_PER_PID 4u

typedef enum {
    OSAURA_PROCESSOR_BUS_IDLE = 0,
    OSAURA_PROCESSOR_BUS_CHECK = 1,
    OSAURA_PROCESSOR_BUS_RETURN = 2
} osaura_processor_bus_phase;

typedef enum {
    OSAURA_PROCESSOR_BUS_CHANGE_NONE = 0,
    OSAURA_PROCESSOR_BUS_CHANGE_VALUE = 1,
    OSAURA_PROCESSOR_BUS_CHANGE_LAYOUT = 2,
    OSAURA_PROCESSOR_BUS_CHANGE_SOURCE = 3,
    OSAURA_PROCESSOR_BUS_CHANGE_STATE = 4,
    OSAURA_PROCESSOR_BUS_CHANGE_STYLE = 5,
    OSAURA_PROCESSOR_BUS_CHANGE_CONTENT = 6,
    OSAURA_PROCESSOR_BUS_CHANGE_CUSTOM = 255
} osaura_processor_bus_change_kind;

/*
 * Processor-owned descriptor for borrowed canonical state.
 * pointed_ref is never copied by the bus. The publisher guarantees that the
 * referenced storage remains stable until this generation is completed.
 */
typedef struct {
    uint64_t bag_id;
    uint64_t bag_generation;
    uintptr_t pointed_ref;
    uint32_t pointed_bytes;
    uint32_t change_kind;
    uint32_t source_pid;
    uint32_t flags;
} osaura_processor_bus_change;

typedef struct {
    uint64_t generation;
    uint64_t route_bits;
    uint16_t changed_bits;       /* route-index bits, not PID bits */
    uint8_t route_count;
    uint8_t route_cursor;
    uint8_t phase;
    uint8_t reserved0;
    uint32_t foreground_pid;     /* bookmarked route[0] when it is a live program */
    uint32_t active_pid;
    osaura_processor_bus_change change;
    uintptr_t response_ref;      /* most recent borrowed response, kept for compatibility */
    uint32_t response_bytes;
    uint32_t response_pid;
} osaura_processor_bus_info;

typedef struct {
    uint64_t generation;
    uint8_t phase;
    uint32_t source_pid;
    uint32_t foreground_pid;
    uint64_t bag_id;
    uint64_t bag_generation;
    uint32_t change_kind;
    const void *pointed_data;
    uint32_t pointed_bytes;
} osaura_processor_bus_view;

typedef struct {
    uint32_t (*task_count)(void *context);
    int (*is_program)(uint32_t pid, void *context);
    uint32_t (*foreground_pid)(void *context);
    int (*wake)(uint32_t pid, void *context);
    int (*sleep)(uint32_t pid, void *context);
} osaura_processor_bus_backend;

int osaura_processor_bus_init(const osaura_processor_bus_backend *backend, void *context);
void osaura_processor_bus_reset(void);

/* Begin CHECK traversal. Foreground first, then every other live program by PID. */
int osaura_processor_bus_publish(const osaura_processor_bus_change *change);

/* Only the currently woken PID may observe or acknowledge the generation. */
int osaura_processor_bus_view_for(uint32_t pid, osaura_processor_bus_view *view);
int osaura_processor_bus_ack(uint32_t pid,
                             int changed,
                             const void *pointed_response,
                             uint32_t response_bytes);

/* Compatibility summary: returns the most recent changed response. */
int osaura_processor_bus_response(const void **data,
                                  uint32_t *bytes,
                                  uint16_t *changed_bits,
                                  uint32_t *response_pid);

/* Read every changed response by traversal position without copying it. */
int osaura_processor_bus_response_at(uint32_t order_index,
                                     const void **data,
                                     uint32_t *bytes,
                                     uint32_t *response_pid);

/* Processor publishes its dealt result back through the identical route. */
int osaura_processor_bus_publish_return(const void *pointed_data, uint32_t bytes);

/* Complete releases all borrowed references and returns the bus to IDLE. */
int osaura_processor_bus_complete(uint64_t generation);

const osaura_processor_bus_info *osaura_processor_bus_get_info(void);
uint32_t osaura_processor_bus_route_pid(uint32_t order_index);

#endif
