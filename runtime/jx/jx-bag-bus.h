#ifndef OSAURA_JX_BAG_BUS_H
#define OSAURA_JX_BAG_BUS_H

#include <stdint.h>
#include <stddef.h>

/*
 * Host-neutral publication descriptor. The pointed data remains owned by the
 * Bag/view; the processor bus only borrows it for one bus generation.
 */
typedef struct {
    uint64_t bag_id;
    uint64_t bag_generation;
    const void *pointed_data;
    uint32_t pointed_bytes;
    uint32_t change_kind;
    uint32_t source_pid;
    uint32_t flags;
} osaura_jx_bag_bus_publication;

int osaura_jx_bag_bus_publish(const osaura_jx_bag_bus_publication *publication);

#endif
