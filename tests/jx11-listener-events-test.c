#include "../kernel/display.h"
#include "../runtime/jx/jx11-surface.h"
#include "../runtime/jx/jx11-window.h"
#include "../runtime/jx/jx11-listener-events.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t framebuffer[80u * 50u];

static void *test_alloc(size_t bytes, void *context) {
    (void)context;
    return calloc(1u, bytes);
}

static void test_free(void *ptr, size_t bytes, void *context) {
    (void)bytes;
    (void)context;
    free(ptr);
}

static int expect(int ok, const char *label) {
    if (!ok) {
        fprintf(stderr, "JX11 LISTENER EVENT FAIL: %s\n", label);
        return 0;
    }
    return 1;
}

int main(void) {
    osaura_display_surface primary = {
        80u, 50u, 80u, OSAURA_DISPLAY_PIXEL_BGRX8,
        (uint64_t)(uintptr_t)framebuffer, sizeof framebuffer
    };
    if (!expect(osaura_display_init_surface(&primary) == 0, "display init")) return 1;
    if (!expect(osaura_jx11_surface_init(test_alloc, test_free, 0) == 0, "surface init")) return 1;
    if (!expect(osaura_jx11_window_init() == 0, "window init")) return 1;
    if (!expect(osaura_jx11_listener_events_init() == 0, "listener router init")) return 1;

    uint32_t left = OSAURA_JX11_WINDOW_NONE;
    uint32_t left_child = OSAURA_JX11_WINDOW_NONE;
    uint32_t right = OSAURA_JX11_WINDOW_NONE;
    if (!expect(osaura_jx11_window_create(1u, OSAURA_JX11_WINDOW_NONE,
                                          2, 2, 24u, 22u, &left) == 0,
                "left create")) return 1;
    if (!expect(osaura_jx11_window_create(1u, left, 4, 4, 8u, 8u, &left_child) == 0,
                "left child create")) return 1;
    if (!expect(osaura_jx11_window_create(1u, OSAURA_JX11_WINDOW_NONE,
                                          40, 2, 24u, 22u, &right) == 0,
                "right create")) return 1;
    if (!expect(osaura_jx11_window_bind_listener_as(1u, left, 2u) == 0,
                "left listener")) return 1;
    if (!expect(osaura_jx11_window_bind_listener_as(1u, right, 3u) == 0,
                "right listener")) return 1;

    /* Child inherits PID 2. */
    if (!expect(osaura_jx11_window_pointer(7, 7, 1u, 1u, 1) == 0, "left child down")) return 1;
    if (!expect(osaura_jx11_window_pointer(7, 7, 0u, 1u, 0) == 0, "left child up")) return 1;

    if (!expect(osaura_jx11_window_pointer(44, 6, 1u, 1u, 1) == 0, "right down")) return 1;
    if (!expect(osaura_jx11_window_pointer(44, 6, 0u, 1u, 0) == 0, "right up")) return 1;

    osaura_jx11_event event = {0};
    if (!expect(osaura_jx11_listener_event_pop(1u, 3u, &event) == 1,
                "listener 3 direct pop")) return 1;
    if (!expect(event.window_id == right && event.type == OSAURA_JX11_EVENT_FOCUS,
                "listener 3 receives own oldest event")) return 1;
    if (!expect(osaura_jx11_listener_event_cached() > 0u,
                "listener 2 events retained while listener 3 is served first")) return 1;

    if (!expect(osaura_jx11_listener_event_pop(1u, 2u, &event) == 1,
                "listener 2 cached pop")) return 1;
    if (!expect(event.window_id == left_child && event.type == OSAURA_JX11_EVENT_FOCUS,
                "child event inherits parent listener")) return 1;

    int saw_left_down = 0;
    int saw_right_down = 0;
    for (uint32_t i = 0u; i < 8u; ++i) {
        int rc = osaura_jx11_listener_event_pop(1u, 2u, &event);
        if (rc < 0) return 1;
        if (rc == 0) break;
        if (event.window_id == left_child && event.type == OSAURA_JX11_EVENT_POINTER_DOWN)
            saw_left_down = 1;
    }
    for (uint32_t i = 0u; i < 8u; ++i) {
        int rc = osaura_jx11_listener_event_pop(1u, 3u, &event);
        if (rc < 0) return 1;
        if (rc == 0) break;
        if (event.window_id == right && event.type == OSAURA_JX11_EVENT_POINTER_DOWN)
            saw_right_down = 1;
    }
    if (!expect(saw_left_down, "listener 2 pointer event retained")) return 1;
    if (!expect(saw_right_down, "listener 3 pointer event delivered")) return 1;

    osaura_jx11_listener_events_shutdown();
    osaura_jx11_window_shutdown();
    osaura_jx11_surface_shutdown();
    puts("JX11 LISTENER EVENTS: PASS");
    return 0;
}
