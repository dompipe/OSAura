#include "../kernel/display.h"
#include "../kernel/processor-bus.h"
#include "../runtime/jx/jx11-surface.h"
#include "../runtime/jx/jx11-window.h"
#include "../runtime/jx/jx11-window-bag.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t framebuffer[64u * 48u];
static uint8_t awake;

static void *test_alloc(size_t bytes, void *context) { (void)context; return calloc(1u, bytes); }
static void test_free(void *ptr, size_t bytes, void *context) { (void)bytes; (void)context; free(ptr); }
static uint32_t task_count(void *context) { (void)context; return 2u; }
static int is_program(uint32_t pid, void *context) { (void)context; return pid == 1u; }
static uint32_t foreground_pid(void *context) { (void)context; return 1u; }
static int is_awake(uint32_t pid, void *context) { (void)context; return pid == 1u ? (int)awake : -1; }
static int wake(uint32_t pid, void *context) { (void)context; if (pid != 1u) return -1; awake = 1u; return 0; }
static int sleep_task(uint32_t pid, void *context) { (void)context; if (pid != 1u) return -1; awake = 0u; return 0; }

static int expect(int ok, const char *label) {
    if (!ok) { fprintf(stderr, "FAIL %s\n", label); return 0; }
    return 1;
}

int main(void) {
    osaura_display_surface primary = {64u, 48u, 64u, OSAURA_DISPLAY_PIXEL_BGRX8,
                                      (uint64_t)(uintptr_t)framebuffer, sizeof framebuffer};
    if (!expect(osaura_display_init_surface(&primary) == 0, "display init")) return 1;
    if (!expect(osaura_jx11_surface_init(test_alloc, test_free, 0) == 0, "surface init")) return 1;
    if (!expect(osaura_jx11_window_init() == 0, "window init")) return 1;
    if (!expect(osaura_jx11_window_bag_init() == 0, "window Bag init")) return 1;

    osaura_processor_bus_backend backend = {
        task_count, is_program, foreground_pid, is_awake, wake, sleep_task
    };
    if (!expect(osaura_processor_bus_init(&backend, 0) == 0, "bus init")) return 1;

    uint32_t window = OSAURA_JX11_WINDOW_NONE;
    if (!expect(osaura_jx11_window_create(1u, OSAURA_JX11_WINDOW_NONE, 4, 5, 20u, 12u, &window) == 0,
                "window create")) return 1;
    if (!expect(osaura_jx11_window_bind_listener_as(1u, window, 1u) == 0,
                "bind listener PID")) return 1;

    static const struct { uint32_t value; uint32_t selected; } bag_view_v7 = { 42u, 1u };
    if (!expect(osaura_jx11_window_bag_bind_as(1u, window, 37ull, 7ull,
                                               &bag_view_v7, sizeof bag_view_v7) == 0,
                "bind borrowed Bag")) return 1;

    osaura_jx11_window_bag_view before = {0};
    if (!expect(osaura_jx11_window_bag_get(window, &before) == 1, "read binding")) return 1;
    if (!expect(before.bag_id == 37ull && before.bag_generation == 7ull &&
                before.borrowed_data == &bag_view_v7, "initial Bag identity")) return 1;

    if (!expect(osaura_jx11_window_move_as(1u, window, 31, 22) == 0, "move view")) return 1;
    if (!expect(osaura_jx11_window_focus_as(1u, window) == 0, "focus view")) return 1;
    if (!expect(osaura_jx11_window_primary_listener() == 1u,
                "focused window is primary listener")) return 1;
    osaura_jx11_window_bag_view after_move = {0};
    if (!expect(osaura_jx11_window_bag_get(window, &after_move) == 1, "read after move")) return 1;
    if (!expect(after_move.bag_id == before.bag_id &&
                after_move.bag_generation == before.bag_generation &&
                after_move.borrowed_data == before.borrowed_data,
                "moving window does not mutate Bag binding")) return 1;

    static const struct { uint32_t value; uint32_t selected; } bag_view_v8 = { 88u, 0u };
    if (!expect(osaura_jx11_window_bag_rebind_as(1u, window, 7ull,
                                                 &bag_view_v8, sizeof bag_view_v8) == -5,
                "stale Bag generation rejected")) return 1;
    if (!expect(osaura_jx11_window_bag_rebind_as(1u, window, 8ull,
                                                 &bag_view_v8, sizeof bag_view_v8) == 0,
                "new Bag generation accepted")) return 1;
    if (!expect(osaura_jx11_window_bag_bind_as(2u, window, 99ull, 1ull,
                                               &bag_view_v8, sizeof bag_view_v8) == -3,
                "cross-subject Bag binding denied")) return 1;

    if (!expect(osaura_jx11_window_bag_publish_as(1u, window,
                                                  OSAURA_PROCESSOR_BUS_CHANGE_VALUE,
                                                  1u, 0u) == 1,
                "publish borrowed Bag to processor bus")) return 1;
    if (!expect(osaura_processor_bus_priority_pid() == 1u,
                "window listener installed as bus priority")) return 1;
    osaura_processor_bus_view bus_view = {0};
    if (!expect(osaura_processor_bus_view_for(1u, &bus_view) == 0, "processor bus view")) return 1;
    if (!expect(bus_view.foreground_pid == 1u &&
                bus_view.bag_id == 37ull && bus_view.bag_generation == 8ull &&
                bus_view.pointed_data == &bag_view_v8 &&
                bus_view.change_kind == OSAURA_PROCESSOR_BUS_CHANGE_VALUE,
                "window publishes current Bag generation without copy")) return 1;
    if (!expect(osaura_processor_bus_ack(1u, 0, 0, 0u) == 0, "bus check complete")) return 1;
    if (!expect(osaura_processor_bus_complete(osaura_processor_bus_get_info()->generation) == 0,
                "bus generation complete")) return 1;

    uint32_t old_window = window;
    if (!expect(osaura_jx11_window_destroy_as(1u, old_window) == 0, "window destroy")) return 1;
    osaura_jx11_window_bag_view stale = {0};
    if (!expect(osaura_jx11_window_bag_get(old_window, &stale) == 0,
                "destroyed window drops borrowed Bag view")) return 1;

    uint32_t reused = OSAURA_JX11_WINDOW_NONE;
    if (!expect(osaura_jx11_window_create(1u, OSAURA_JX11_WINDOW_NONE, 1, 1, 8u, 8u, &reused) == 0,
                "replacement window create")) return 1;
    if (!expect(reused == old_window, "window slot reused")) return 1;
    if (!expect(osaura_jx11_window_bag_get(reused, &stale) == 0,
                "reused window does not inherit old Bag")) return 1;
    if (!expect(osaura_jx11_window_destroy_as(1u, reused) == 0, "replacement window destroy")) return 1;

    osaura_jx11_window_bag_shutdown();
    osaura_jx11_window_shutdown();
    osaura_jx11_surface_shutdown();
    puts("JX11 WINDOW BAG VIEW: PASS");
    return 0;
}
