#ifdef _WIN64

#include "../host/windows/runtime64.h"
#include "../host/windows/processor-bus-autobind-win64.h"
#include "../kernel/processor-bus.h"
#include "../kernel/security.h"

#include <stdint.h>
#include <stdio.h>

static int expect(int ok, const char *label) {
    if (!ok) { fprintf(stderr, "FAIL %s\n", label); return 0; }
    return 1;
}

int main(void) {
    if (!expect(osaura_windows_processor_bus64_autobind_status() == 0,
                "processor bus bound by CRT before main")) return 1;
    if (!expect(osaura_processor_bus_get_info() != 0,
                "shared processor bus already initialized")) return 1;

    osaura_security_init();
    if (!expect(osaura_windows_task64_init() == 0, "task64 init")) return 1;

    static const uint64_t value = 0x123456789abcdef0ull;
    osaura_processor_bus_change change = {0};
    change.bag_id = 91ull;
    change.bag_generation = 7ull;
    change.pointed_ref = (uintptr_t)&value;
    change.pointed_bytes = (uint32_t)sizeof(value);
    change.change_kind = OSAURA_PROCESSOR_BUS_CHANGE_VALUE;
    change.source_pid = 2u;

    if (!expect(osaura_processor_bus_publish(&change) == 1, "publish")) return 1;
    const osaura_processor_bus_info *info = osaura_processor_bus_get_info();
    if (!expect(info && info->route_count == 1u && info->foreground_pid == 2u,
                "WSJX64 foreground route")) return 1;
    if (!expect(osaura_processor_bus_route_pid(0u) == 2u, "foreground PID first")) return 1;
    if (!expect(info->bus_woke_bits == 0u, "runnable shell not re-owned by bus")) return 1;

    osaura_processor_bus_view view = {0};
    if (!expect(osaura_processor_bus_view_for(2u, &view) == 0 &&
                view.bag_id == 91ull && view.bag_generation == 7ull && view.pointed_data == &value,
                "borrowed Bag view")) return 1;
    if (!expect(osaura_processor_bus_ack(2u, 0, 0, 0u) == 0, "check complete")) return 1;

    osaura_windows_task64_info task = {0};
    if (!expect(osaura_windows_task64_get_info(2u, &task) == 0 && task.state == OSAURA_TASK_RUNNABLE,
                "foreground task remains runnable")) return 1;

    static const uint32_t dealt = 0xcafebabeu;
    if (!expect(osaura_processor_bus_publish_return(&dealt, sizeof(dealt)) == 1, "return publish")) return 1;
    if (!expect(osaura_processor_bus_view_for(2u, &view) == 0 &&
                view.phase == OSAURA_PROCESSOR_BUS_RETURN && view.pointed_data == &dealt,
                "return view")) return 1;
    if (!expect(osaura_processor_bus_ack(2u, 0, 0, 0u) == 0, "return complete")) return 1;

    uint64_t generation = osaura_processor_bus_get_info()->generation;
    if (!expect(osaura_processor_bus_complete(generation) == 0, "generation complete")) return 1;

    puts("WSJX64 PROCESSOR BUS AUTOBIND: PASS");
    return 0;
}

#endif
