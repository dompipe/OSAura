#include "../runtime/jx/jx-bag-bus.h"
#include "../kernel/processor-bus.h"

#include <stdint.h>
#include <stdio.h>

static uint8_t awake;

static uint32_t task_count(void *context) { (void)context; return 1u; }
static int is_program(uint32_t pid, void *context) { (void)context; return pid == 0u; }
static uint32_t foreground_pid(void *context) { (void)context; return 0u; }
static int is_awake(uint32_t pid, void *context) { (void)context; return pid == 0u ? (int)awake : -1; }
static int wake(uint32_t pid, void *context) { (void)context; if (pid != 0u) return -1; awake = 1u; return 0; }
static int sleep_task(uint32_t pid, void *context) { (void)context; if (pid != 0u) return -1; awake = 0u; return 0; }

static int expect(int ok, const char *label) {
    if (!ok) { fprintf(stderr, "FAIL %s\n", label); return 0; }
    return 1;
}

int main(void) {
    osaura_processor_bus_backend backend = {
        task_count, is_program, foreground_pid, is_awake, wake, sleep_task
    };
    if (!expect(osaura_processor_bus_init(&backend, 0) == 0, "bus init")) return 1;

    static const struct {
        int32_t x;
        int32_t y;
        uint32_t surface;
    } borrowed_view = { 320, 180, 17u };

    osaura_jx_bag_bus_publication publication = {0};
    publication.bag_id = 37ull;
    publication.bag_generation = 54ull;
    publication.pointed_data = &borrowed_view;
    publication.pointed_bytes = (uint32_t)sizeof(borrowed_view);
    publication.change_kind = OSAURA_PROCESSOR_BUS_CHANGE_LAYOUT;
    publication.source_pid = 0u;

    if (!expect(osaura_jx_bag_bus_publish(&publication) == 1, "Bag publication wakes route")) return 1;

    osaura_processor_bus_view view = {0};
    if (!expect(osaura_processor_bus_view_for(0u, &view) == 0, "program observes Bag generation")) return 1;
    if (!expect(view.bag_id == publication.bag_id, "Bag identity preserved")) return 1;
    if (!expect(view.bag_generation == publication.bag_generation, "Bag generation preserved")) return 1;
    if (!expect(view.change_kind == OSAURA_PROCESSOR_BUS_CHANGE_LAYOUT, "change kind preserved")) return 1;
    if (!expect(view.pointed_data == &borrowed_view, "borrowed pointer preserved without copy")) return 1;
    if (!expect(view.pointed_bytes == sizeof(borrowed_view), "borrowed extent preserved")) return 1;

    if (!expect(osaura_processor_bus_ack(0u, 0, 0, 0u) == 0, "check traversal complete")) return 1;
    static const uint32_t dealt = 0x37u;
    if (!expect(osaura_processor_bus_publish_return(&dealt, sizeof(dealt)) == 1, "processor return published")) return 1;
    if (!expect(osaura_processor_bus_view_for(0u, &view) == 0 &&
                view.phase == OSAURA_PROCESSOR_BUS_RETURN && view.pointed_data == &dealt,
                "same route receives dealt result")) return 1;
    if (!expect(osaura_processor_bus_ack(0u, 0, 0, 0u) == 0, "return traversal complete")) return 1;

    if (!expect(osaura_processor_bus_complete(osaura_processor_bus_get_info()->generation) == 0,
                "generation complete")) return 1;

    puts("JX BAG PROCESSOR BUS: PASS");
    return 0;
}
