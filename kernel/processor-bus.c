#include "processor-bus.h"

#include <stddef.h>
#include <stdint.h>

static osaura_processor_bus_backend g_backend;
static void *g_backend_context;
static osaura_processor_bus_info g_bus;
static uintptr_t g_response_ref[OSAURA_PROCESSOR_BUS_MAX_PROGRAMS];
static uint32_t g_response_bytes[OSAURA_PROCESSOR_BUS_MAX_PROGRAMS];
static uint32_t g_response_pid[OSAURA_PROCESSOR_BUS_MAX_PROGRAMS];
static uint8_t g_initialized;

static void clear_responses(void) {
    for (uint32_t i = 0; i < OSAURA_PROCESSOR_BUS_MAX_PROGRAMS; ++i) {
        g_response_ref[i] = (uintptr_t)0u;
        g_response_bytes[i] = 0u;
        g_response_pid[i] = OSAURA_PROCESSOR_BUS_PID_NONE;
    }
}

static void clear_info(void) {
    g_bus.route_bits = 0ull;
    g_bus.changed_bits = 0u;
    g_bus.bus_woke_bits = 0u;
    g_bus.route_count = 0u;
    g_bus.route_cursor = 0u;
    g_bus.phase = OSAURA_PROCESSOR_BUS_IDLE;
    g_bus.reserved0 = 0u;
    g_bus.foreground_pid = OSAURA_PROCESSOR_BUS_PID_NONE;
    g_bus.active_pid = OSAURA_PROCESSOR_BUS_PID_NONE;
    g_bus.change.bag_id = 0ull;
    g_bus.change.bag_generation = 0ull;
    g_bus.change.pointed_ref = (uintptr_t)0u;
    g_bus.change.pointed_bytes = 0u;
    g_bus.change.change_kind = OSAURA_PROCESSOR_BUS_CHANGE_NONE;
    g_bus.change.source_pid = OSAURA_PROCESSOR_BUS_PID_NONE;
    g_bus.change.flags = 0u;
    g_bus.response_ref = (uintptr_t)0u;
    g_bus.response_bytes = 0u;
    g_bus.response_pid = OSAURA_PROCESSOR_BUS_PID_NONE;
    clear_responses();
}

static int backend_valid(const osaura_processor_bus_backend *backend) {
    return backend && backend->task_count && backend->is_program &&
           backend->foreground_pid && backend->is_awake && backend->wake && backend->sleep;
}

static int route_contains(uint32_t pid) {
    for (uint32_t i = 0; i < g_bus.route_count; ++i) {
        if (osaura_processor_bus_route_pid(i) == pid) return 1;
    }
    return 0;
}

static int route_append(uint32_t pid) {
    if (g_bus.route_count >= OSAURA_PROCESSOR_BUS_MAX_PROGRAMS || pid >= 16u) return -1;
    uint32_t shift = (uint32_t)g_bus.route_count * OSAURA_PROCESSOR_BUS_ROUTE_BITS_PER_PID;
    g_bus.route_bits |= ((uint64_t)pid & 0xfull) << shift;
    g_bus.route_count++;
    return 0;
}

static int build_route(void) {
    g_bus.route_bits = 0ull;
    g_bus.route_count = 0u;
    g_bus.route_cursor = 0u;
    g_bus.foreground_pid = OSAURA_PROCESSOR_BUS_PID_NONE;
    uint32_t count = g_backend.task_count(g_backend_context);
    if (count > OSAURA_PROCESSOR_BUS_MAX_PROGRAMS) count = OSAURA_PROCESSOR_BUS_MAX_PROGRAMS;
    uint32_t foreground = g_backend.foreground_pid(g_backend_context);
    if (foreground < count && foreground < 16u && g_backend.is_program(foreground, g_backend_context)) {
        if (route_append(foreground) != 0) return -1;
        g_bus.foreground_pid = foreground;
    }
    for (uint32_t pid = 0; pid < count; ++pid) {
        if (pid >= 16u || pid == foreground) continue;
        if (!g_backend.is_program(pid, g_backend_context) || route_contains(pid)) continue;
        if (route_append(pid) != 0) return -1;
    }
    return 0;
}

static int wake_current(void) {
    if (g_bus.route_cursor >= g_bus.route_count) {
        g_bus.active_pid = OSAURA_PROCESSOR_BUS_PID_NONE;
        return 0;
    }
    uint32_t pid = osaura_processor_bus_route_pid(g_bus.route_cursor);
    if (pid == OSAURA_PROCESSOR_BUS_PID_NONE) return -1;
    int awake = g_backend.is_awake(pid, g_backend_context);
    if (awake < 0) return -2;
    if (!awake) {
        if (g_backend.wake(pid, g_backend_context) != 0) return -3;
        g_bus.bus_woke_bits |= (uint16_t)(1u << g_bus.route_cursor);
    }
    g_bus.active_pid = pid;
    return 1;
}

int osaura_processor_bus_init(const osaura_processor_bus_backend *backend, void *context) {
    if (!backend_valid(backend)) return -1;
    g_backend = *backend;
    g_backend_context = context;
    g_bus.generation = 0ull;
    clear_info();
    g_initialized = 1u;
    return 0;
}

void osaura_processor_bus_reset(void) {
    uint64_t generation = g_bus.generation;
    clear_info();
    g_bus.generation = generation;
}

int osaura_processor_bus_publish(const osaura_processor_bus_change *change) {
    if (!g_initialized || !change) return -1;
    if (g_bus.phase != OSAURA_PROCESSOR_BUS_IDLE) return -2;
    if (change->pointed_bytes && change->pointed_ref == (uintptr_t)0u) return -3;
    clear_info();
    g_bus.generation++;
    if (g_bus.generation == 0ull) g_bus.generation = 1ull;
    g_bus.change = *change;
    g_bus.phase = OSAURA_PROCESSOR_BUS_CHECK;
    if (build_route() != 0) { osaura_processor_bus_reset(); return -4; }
    return g_bus.route_count == 0u ? 0 : wake_current();
}

int osaura_processor_bus_view_for(uint32_t pid, osaura_processor_bus_view *view) {
    if (!g_initialized || !view) return -1;
    if (g_bus.phase == OSAURA_PROCESSOR_BUS_IDLE || pid != g_bus.active_pid) return -2;
    view->generation = g_bus.generation;
    view->phase = g_bus.phase;
    view->source_pid = g_bus.change.source_pid;
    view->foreground_pid = g_bus.foreground_pid;
    view->bag_id = g_bus.change.bag_id;
    view->bag_generation = g_bus.change.bag_generation;
    view->change_kind = g_bus.change.change_kind;
    if (g_bus.phase == OSAURA_PROCESSOR_BUS_RETURN) {
        view->pointed_data = (const void *)g_bus.response_ref;
        view->pointed_bytes = g_bus.response_bytes;
    } else {
        view->pointed_data = (const void *)g_bus.change.pointed_ref;
        view->pointed_bytes = g_bus.change.pointed_bytes;
    }
    return 0;
}

int osaura_processor_bus_ack(uint32_t pid, int changed, const void *pointed_response, uint32_t response_bytes) {
    if (!g_initialized || g_bus.phase == OSAURA_PROCESSOR_BUS_IDLE) return -1;
    if (pid != g_bus.active_pid || g_bus.route_cursor >= g_bus.route_count) return -2;
    if (response_bytes && !pointed_response) return -3;
    uint32_t index = g_bus.route_cursor;
    if (changed && g_bus.phase == OSAURA_PROCESSOR_BUS_CHECK) {
        g_bus.changed_bits |= (uint16_t)(1u << index);
        g_response_ref[index] = (uintptr_t)pointed_response;
        g_response_bytes[index] = response_bytes;
        g_response_pid[index] = pid;
        g_bus.response_ref = (uintptr_t)pointed_response;
        g_bus.response_bytes = response_bytes;
        g_bus.response_pid = pid;
    }
    if ((g_bus.bus_woke_bits & (uint16_t)(1u << index)) != 0u) {
        if (g_backend.sleep(pid, g_backend_context) != 0) return -4;
        g_bus.bus_woke_bits &= (uint16_t)~(1u << index);
    }
    g_bus.active_pid = OSAURA_PROCESSOR_BUS_PID_NONE;
    g_bus.route_cursor++;
    return wake_current();
}

int osaura_processor_bus_response(const void **data, uint32_t *bytes, uint16_t *changed_bits, uint32_t *response_pid) {
    if (!g_initialized || g_bus.phase != OSAURA_PROCESSOR_BUS_CHECK) return -1;
    if (g_bus.active_pid != OSAURA_PROCESSOR_BUS_PID_NONE || g_bus.route_cursor < g_bus.route_count) return -2;
    if (data) *data = (const void *)g_bus.response_ref;
    if (bytes) *bytes = g_bus.response_bytes;
    if (changed_bits) *changed_bits = g_bus.changed_bits;
    if (response_pid) *response_pid = g_bus.response_pid;
    return 0;
}

int osaura_processor_bus_response_at(uint32_t order_index, const void **data, uint32_t *bytes, uint32_t *response_pid) {
    if (!g_initialized || g_bus.phase != OSAURA_PROCESSOR_BUS_CHECK) return -1;
    if (g_bus.active_pid != OSAURA_PROCESSOR_BUS_PID_NONE || g_bus.route_cursor < g_bus.route_count) return -2;
    if (order_index >= g_bus.route_count) return -3;
    if ((g_bus.changed_bits & (uint16_t)(1u << order_index)) == 0u) return 0;
    if (data) *data = (const void *)g_response_ref[order_index];
    if (bytes) *bytes = g_response_bytes[order_index];
    if (response_pid) *response_pid = g_response_pid[order_index];
    return 1;
}

int osaura_processor_bus_publish_return(const void *pointed_data, uint32_t bytes) {
    if (!g_initialized || g_bus.phase != OSAURA_PROCESSOR_BUS_CHECK) return -1;
    if (g_bus.active_pid != OSAURA_PROCESSOR_BUS_PID_NONE || g_bus.route_cursor < g_bus.route_count) return -2;
    if (bytes && !pointed_data) return -3;
    g_bus.phase = OSAURA_PROCESSOR_BUS_RETURN;
    g_bus.route_cursor = 0u;
    g_bus.changed_bits = 0u;
    g_bus.bus_woke_bits = 0u;
    clear_responses();
    g_bus.response_ref = (uintptr_t)pointed_data;
    g_bus.response_bytes = bytes;
    g_bus.response_pid = OSAURA_PROCESSOR_BUS_PID_NONE;
    return g_bus.route_count == 0u ? 0 : wake_current();
}

int osaura_processor_bus_complete(uint64_t generation) {
    if (!g_initialized || generation == 0ull || generation != g_bus.generation) return -1;
    if (g_bus.active_pid != OSAURA_PROCESSOR_BUS_PID_NONE) return -2;
    if (g_bus.phase == OSAURA_PROCESSOR_BUS_RETURN && g_bus.route_cursor < g_bus.route_count) return -3;
    clear_info();
    g_bus.generation = generation;
    return 0;
}

const osaura_processor_bus_info *osaura_processor_bus_get_info(void) { return g_initialized ? &g_bus : 0; }

uint32_t osaura_processor_bus_route_pid(uint32_t order_index) {
    if (!g_initialized || order_index >= g_bus.route_count) return OSAURA_PROCESSOR_BUS_PID_NONE;
    return (uint32_t)((g_bus.route_bits >> (order_index * OSAURA_PROCESSOR_BUS_ROUTE_BITS_PER_PID)) & 0xfull);
}
