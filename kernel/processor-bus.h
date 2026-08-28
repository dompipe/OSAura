#ifndef OSAURA_PROCESSOR_BUS_H
#define OSAURA_PROCESSOR_BUS_H

#include <stdint.h>
#include <stddef.h>

#define OSAURA_PROCESSOR_BUS_MAX_PROGRAMS 16u
#define OSAURA_PROCESSOR_BUS_BUFFER_BYTES 4096u
#define OSAURA_PROCESSOR_BUS_PID_NONE UINT32_MAX

typedef enum {
    OSAURA_PROCESSOR_BUS_IDLE = 0,
    OSAURA_PROCESSOR_BUS_CHECK = 1,
    OSAURA_PROCESSOR_BUS_RETURN = 2
} osaura_processor_bus_phase;

typedef struct {
    uint64_t generation;
    uint64_t route_bits;
    uint16_t changed_bits;
    uint8_t route_count;
    uint8_t route_cursor;
    uint8_t phase;
    uint32_t source_pid;
    uint32_t foreground_pid;
    uint32_t active_pid;
    uint32_t payload_bytes;
    uint32_t response_bytes;
} osaura_processor_bus_info;

typedef struct {
    uint64_t generation;
    uint8_t phase;
    uint32_t source_pid;
    const uint8_t *payload;
    uint32_t payload_bytes;
} osaura_processor_bus_view;

typedef struct {
    uint32_t (*task_count)(void *context);
    int (*is_program)(uint32_t pid, void *context);
    uint32_t (*foreground_pid)(void *context);
    int (*wake)(uint32_t pid, void *context);
    int (*sleep)(uint32_t pid, void *context);
} osaura_processor_bus_backend;

/* The buffer is processor-owned. Publishers provide pointed data; publish copies it
 * into the global processor buffer before any program is woken. */
int osaura_processor_bus_init(const osaura_processor_bus_backend *backend, void *context);
void osaura_processor_bus_reset(void);
int osaura_processor_bus_publish(uint32_t source_pid, const void *pointed_data, uint32_t bytes);
int osaura_processor_bus_view_for(uint32_t pid, osaura_processor_bus_view *view);

/* Only the currently bookmarked PID may acknowledge. changed=0 means the program
 * inspected the generation and found nothing to return. changed=1 copies its
 * pointed response into processor-owned response storage. The bus then sleeps
 * that PID and wakes the next PID in the precomputed route. */
int osaura_processor_bus_ack(uint32_t pid,
                             int changed,
                             const void *pointed_response,
                             uint32_t response_bytes);

/* After the check sweep, the processor may consume the response aggregate and
 * optionally send one resolved return payload through the same bookmarked route. */
int osaura_processor_bus_response(const uint8_t **data, uint32_t *bytes, uint16_t *changed_bits);
int osaura_processor_bus_publish_return(const void *pointed_data, uint32_t bytes);

const osaura_processor_bus_info *osaura_processor_bus_info(void);
uint32_t osaura_processor_bus_route_pid(uint32_t order_index);

#endif
