#include "jx-bag-bus.h"
#include "../../kernel/processor-bus.h"

#include <stdint.h>

int osaura_jx_bag_bus_publish(const osaura_jx_bag_bus_publication *publication) {
    if (!publication || publication->bag_id == 0ull || publication->bag_generation == 0ull) return -1;
    if (publication->pointed_bytes && !publication->pointed_data) return -2;

    osaura_processor_bus_change change = {0};
    change.bag_id = publication->bag_id;
    change.bag_generation = publication->bag_generation;
    change.pointed_ref = (uintptr_t)publication->pointed_data;
    change.pointed_bytes = publication->pointed_bytes;
    change.change_kind = publication->change_kind;
    change.source_pid = publication->source_pid;
    change.flags = publication->flags;
    return osaura_processor_bus_publish(&change);
}
