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
    uint16_t changed_bits;
    uint16_t bus_woke_bits;
    uint8_t route_count;
    uint8_t route_cursor;
    uint8_t phase;
    uint8_t reserved0;
    uint32_t foreground_pid;
    uint32_t active_pid;
    osaura_processor_bus_change change;
    uintptr_t response_ref;
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
    int (*is_awake)(uint32_t pid, void *context);
    int (*wake)(uint32_t pid, void *context);
    int (*sleep)(uint32_t pid, void *context);
} osaura_processor_bus_backend;

int osaura_processor_bus_init(const osaura_processor_bus_backend *backend, void *context);
void osaura_processor_bus_reset(void);

/*
 * Optional attention bookmark. When it names a live program, it becomes
 * route[0]. Otherwise the scheduler foreground program is used. The bookmark
 * may only change while the bus is idle so an in-flight generation is stable.
 */
int osaura_processor_bus_set_priority_pid(uint32_t pid);
uint32_t osaura_processor_bus_priority_pid(void);

/* Begin CHECK traversal. Preferred listener/foreground first, then PID order. */
int osaura_processor_bus_publish(const osaura_processor_bus_change *change);

int osaura_processor_bus_view_for(uint32_t pid, osaura_processor_bus_view *view);
int osaura_processor_bus_ack(uint32_t pid,
                             int changed,
                             const void *pointed_response,
                             uint32_t response_bytes);

int osaura_processor_bus_response(const void **data,
                                  uint32_t *bytes,
                                  uint16_t *changed_bits,
                                  uint32_t *response_pid);

int osaura_processor_bus_response_at(uint32_t order_index,
                                     const void **data,
                                     uint32_t *bytes,
                                     uint32_t *response_pid);

int osaura_processor_bus_publish_return(const void *pointed_data, uint32_t bytes);
int osaura_processor_bus_complete(uint64_t generation);

const osaura_processor_bus_info *osaura_processor_bus_get_info(void);
uint32_t osaura_processor_bus_route_pid(uint32_t order_index);

#endif
