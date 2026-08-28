#include "../kernel/processor-bus.h"

#include <stdint.h>
#include <stdio.h>

static uint8_t g_program[16];
static uint8_t g_awake[16];
static uint32_t g_foreground = 5u;

static uint32_t task_count(void *ctx) { (void)ctx; return 10u; }
static int is_program(uint32_t pid, void *ctx) { (void)ctx; return pid < 16u && g_program[pid] != 0u; }
static uint32_t foreground_pid(void *ctx) { (void)ctx; return g_foreground; }
static int is_awake(uint32_t pid, void *ctx) { (void)ctx; return pid < 16u ? (int)g_awake[pid] : -1; }
static int wake(uint32_t pid, void *ctx) {
    (void)ctx;
    if (pid >= 16u || !g_program[pid]) return -1;
    g_awake[pid] = 1u;
    return 0;
}
static int sleep_task(uint32_t pid, void *ctx) {
    (void)ctx;
    if (pid >= 16u || !g_awake[pid]) return -1;
    g_awake[pid] = 0u;
    return 0;
}

static int expect(int ok, const char *label) {
    if (!ok) { fprintf(stderr, "FAIL %s\n", label); return 0; }
    return 1;
}

int main(void) {
    g_program[1] = 1u; g_program[3] = 1u; g_program[5] = 1u; g_program[8] = 1u;
    g_awake[5] = 1u;

    osaura_processor_bus_backend backend = {
        task_count, is_program, foreground_pid, is_awake, wake, sleep_task
    };
    if (!expect(osaura_processor_bus_init(&backend, 0) == 0, "init")) return 1;

    static const uint64_t bag_value = 0x1122334455667788ull;
    osaura_processor_bus_change change = {0};
    change.bag_id = 37ull;
    change.bag_generation = 53ull;
    change.pointed_ref = (uintptr_t)&bag_value;
    change.pointed_bytes = (uint32_t)sizeof(bag_value);
    change.change_kind = OSAURA_PROCESSOR_BUS_CHANGE_VALUE;
    change.source_pid = 3u;

    /* A visible/focused listener can outrank scheduler foreground for bus attention. */
    if (!expect(osaura_processor_bus_set_priority_pid(3u) == 0, "set listener priority")) return 1;
    if (!expect(osaura_processor_bus_priority_pid() == 3u, "listener bookmark retained")) return 1;
    if (!expect(osaura_processor_bus_publish(&change) == 1, "priority publish")) return 1;
    const osaura_processor_bus_info *info = osaura_processor_bus_get_info();
    if (!expect(info && info->foreground_pid == 3u, "listener becomes route bookmark")) return 1;
    if (!expect(osaura_processor_bus_route_pid(0) == 3u &&
                osaura_processor_bus_route_pid(1) == 1u &&
                osaura_processor_bus_route_pid(2) == 5u &&
                osaura_processor_bus_route_pid(3) == 8u,
                "listener first then PID order")) return 1;
    if (!expect(osaura_processor_bus_set_priority_pid(1u) == -2,
                "in-flight route cannot be reprioritized")) return 1;
    if (!expect(osaura_processor_bus_ack(3u, 0, 0, 0u) == 1, "priority pass PID1")) return 1;
    if (!expect(osaura_processor_bus_ack(1u, 0, 0, 0u) == 1, "priority pass foreground")) return 1;
    if (!expect(osaura_processor_bus_ack(5u, 0, 0, 0u) == 1, "priority pass PID8")) return 1;
    if (!expect(osaura_processor_bus_ack(8u, 0, 0, 0u) == 0, "priority check complete")) return 1;
    if (!expect(osaura_processor_bus_complete(osaura_processor_bus_get_info()->generation) == 0,
                "priority generation complete")) return 1;
    if (!expect(osaura_processor_bus_set_priority_pid(OSAURA_PROCESSOR_BUS_PID_NONE) == 0,
                "clear listener priority")) return 1;

    if (!expect(osaura_processor_bus_publish(&change) == 1, "publish selects foreground")) return 1;
    info = osaura_processor_bus_get_info();
    if (!expect(info && info->foreground_pid == 5u, "foreground bookmark")) return 1;
    if (!expect(info->route_count == 4u, "route count")) return 1;
    if (!expect(osaura_processor_bus_route_pid(0) == 5u && osaura_processor_bus_route_pid(1) == 1u &&
                osaura_processor_bus_route_pid(2) == 3u && osaura_processor_bus_route_pid(3) == 8u,
                "foreground first then PID order")) return 1;
    if (!expect((info->bus_woke_bits & 1u) == 0u, "already-awake foreground not owned by bus")) return 1;

    osaura_processor_bus_view view = {0};
    if (!expect(osaura_processor_bus_view_for(1u, &view) == -2, "only active PID observes")) return 1;
    if (!expect(osaura_processor_bus_view_for(5u, &view) == 0, "foreground observes")) return 1;
    if (!expect(view.bag_id == 37ull && view.bag_generation == 53ull &&
                view.pointed_data == &bag_value && view.pointed_bytes == sizeof(bag_value),
                "borrowed Bag reference")) return 1;

    static const uint16_t foreground_change = 0x55aau;
    if (!expect(osaura_processor_bus_ack(5u, 1, &foreground_change, sizeof(foreground_change)) == 1,
                "foreground changed and PID1 selected")) return 1;
    if (!expect(g_awake[5] == 1u, "foreground remains awake after ack")) return 1;
    if (!expect(g_awake[1] == 1u, "bus woke sleeping PID1")) return 1;
    if (!expect(osaura_processor_bus_ack(1u, 0, 0, 0u) == 1, "PID1 sleeps and PID3 wakes")) return 1;
    if (!expect(g_awake[1] == 0u, "bus-owned PID1 returned to sleep")) return 1;

    static const uint32_t pid3_change = 0xa5a55a5au;
    if (!expect(osaura_processor_bus_ack(3u, 1, &pid3_change, sizeof(pid3_change)) == 1,
                "PID3 changed and PID8 wakes")) return 1;
    if (!expect(osaura_processor_bus_ack(8u, 0, 0, 0u) == 0, "check pass complete")) return 1;

    const void *response = 0; uint32_t response_bytes = 0u; uint16_t changed_bits = 0u;
    uint32_t response_pid = OSAURA_PROCESSOR_BUS_PID_NONE;
    if (!expect(osaura_processor_bus_response(&response, &response_bytes, &changed_bits, &response_pid) == 0,
                "processor consumes response summary")) return 1;
    if (!expect(response == &pid3_change && response_bytes == sizeof(pid3_change) && response_pid == 3u,
                "summary remains latest pointed response")) return 1;
    if (!expect(changed_bits == (uint16_t)((1u << 0) | (1u << 2)), "changed bits preserve route positions")) return 1;

    const void *ordered = 0; uint32_t ordered_bytes = 0u; uint32_t ordered_pid = OSAURA_PROCESSOR_BUS_PID_NONE;
    if (!expect(osaura_processor_bus_response_at(0u, &ordered, &ordered_bytes, &ordered_pid) == 1 &&
                ordered == &foreground_change && ordered_bytes == sizeof(foreground_change) && ordered_pid == 5u,
                "foreground response retained")) return 1;
    if (!expect(osaura_processor_bus_response_at(1u, &ordered, &ordered_bytes, &ordered_pid) == 0,
                "unchanged route has no response")) return 1;
    if (!expect(osaura_processor_bus_response_at(2u, &ordered, &ordered_bytes, &ordered_pid) == 1 &&
                ordered == &pid3_change && ordered_bytes == sizeof(pid3_change) && ordered_pid == 3u,
                "later response retained independently")) return 1;

    static const uint32_t dealt = 0x55aa00ffu;
    if (!expect(osaura_processor_bus_publish_return(&dealt, sizeof(dealt)) == 1, "return selects foreground")) return 1;
    if (!expect(osaura_processor_bus_view_for(5u, &view) == 0 && view.phase == OSAURA_PROCESSOR_BUS_RETURN &&
                view.pointed_data == &dealt, "foreground sees processor return")) return 1;
    if (!expect(osaura_processor_bus_ack(5u, 0, 0, 0u) == 1, "return PID1")) return 1;
    if (!expect(osaura_processor_bus_ack(1u, 0, 0, 0u) == 1, "return PID3")) return 1;
    if (!expect(osaura_processor_bus_ack(3u, 0, 0, 0u) == 1, "return PID8")) return 1;
    if (!expect(osaura_processor_bus_ack(8u, 0, 0, 0u) == 0, "return complete")) return 1;
    if (!expect(g_awake[5] == 1u, "foreground still runnable after return pass")) return 1;

    uint64_t generation = osaura_processor_bus_get_info()->generation;
    if (!expect(osaura_processor_bus_complete(generation) == 0, "complete generation")) return 1;
    if (!expect(osaura_processor_bus_get_info()->phase == OSAURA_PROCESSOR_BUS_IDLE, "idle after complete")) return 1;

    puts("PROCESSOR MULTIPLEX BUS: PASS");
    return 0;
}
