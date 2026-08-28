#include "../kernel/display.h"
#include "../runtime/jx/jx11-surface.h"
#include "../runtime/jx/jx11-window.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t framebuffer[80u * 60u];

static void *test_alloc(size_t bytes, void *context) {
    (void)context;
    return calloc(1u, bytes);
}

static void test_free(void *ptr, size_t bytes, void *context) {
    (void)bytes;
    (void)context;
    free(ptr);
}

static int fail(const char *message) {
    fprintf(stderr, "JX11 WINDOW TEST FAIL: %s\n", message);
    return 1;
}

int main(void) {
    osaura_display_surface primary = {80u, 60u, 80u, OSAURA_DISPLAY_PIXEL_BGRX8,
                                      (uint64_t)(uintptr_t)framebuffer, sizeof framebuffer};
    if (osaura_display_init_surface(&primary) != 0) return fail("display init");
    if (osaura_jx11_surface_init(test_alloc, test_free, 0) != 0) return fail("surface init");
    if (osaura_jx11_window_init() != 0) return fail("window init");

    uint32_t root = OSAURA_JX11_WINDOW_NONE;
    uint32_t child = OSAURA_JX11_WINDOW_NONE;
    uint32_t other = OSAURA_JX11_WINDOW_NONE;
    if (osaura_jx11_window_create(1u, OSAURA_JX11_WINDOW_NONE, 5, 5, 30u, 20u, &root) != 0)
        return fail("root create");
    if (osaura_jx11_window_create(1u, root, 4, 3, 10u, 8u, &child) != 0)
        return fail("child create");
    if (osaura_jx11_window_create(2u, OSAURA_JX11_WINDOW_NONE, 40, 4, 20u, 20u, &other) != 0)
        return fail("other create");
    if (osaura_jx11_window_count() != 3u) return fail("window count");

    osaura_jx11_window_info root_info = {0};
    osaura_jx11_window_info child_info = {0};
    if (osaura_jx11_window_get_info(root, &root_info) != 0 ||
        osaura_jx11_window_get_info(child, &child_info) != 0)
        return fail("window info");
    if (child_info.parent_id != root) return fail("parent relationship");
    if (osaura_jx11_window_move_as(2u, root, 1, 1) != -3) return fail("ownership move denial");

    osaura_jx11_surface_fill root_fill = {0u, 0u, 30u, 20u, 0xffff0000u};
    osaura_jx11_surface_fill child_fill = {0u, 0u, 10u, 8u, 0xff00ff00u};
    if (osaura_jx11_surface_fill_as(1u, root_info.surface_id, &root_fill) != 0) return fail("root fill");
    if (osaura_jx11_surface_fill_as(1u, child_info.surface_id, &child_fill) != 0) return fail("child fill");
    if (osaura_jx11_window_compose() != 0) return fail("compose");

    if (osaura_jx11_window_hit_test(10, 9) != child) return fail("child hit test");
    if (osaura_jx11_window_pointer(10, 9, 1u, 1u, 1) != 0) return fail("pointer down");
    if (osaura_jx11_window_focused() != child) return fail("pointer focus");

    osaura_jx11_event event = {0};
    int saw_focus = 0, saw_down = 0;
    while (osaura_jx11_window_event_pop(1u, &event) > 0) {
        if (event.type == OSAURA_JX11_EVENT_FOCUS && event.window_id == child) saw_focus = 1;
        if (event.type == OSAURA_JX11_EVENT_POINTER_DOWN && event.window_id == child && event.x == 1 && event.y == 1)
            saw_down = 1;
    }
    if (!saw_focus || !saw_down) return fail("pointer event routing");

    if (osaura_jx11_window_key(65u, 1) != 0) return fail("key route");
    if (osaura_jx11_window_event_pop(1u, &event) != 1 ||
        event.type != OSAURA_JX11_EVENT_KEY_DOWN || event.window_id != child || event.code != 65u)
        return fail("key event");

    if (osaura_jx11_window_move_as(1u, root, 15, 10) != 0) return fail("root move");
    if (osaura_jx11_window_hit_test(20, 14) != child) return fail("child moved with parent");

    uint32_t old_surface = child_info.surface_id;
    if (osaura_jx11_window_resize_as(1u, child, 12u, 9u) != 0) return fail("resize");
    if (osaura_jx11_window_get_info(child, &child_info) != 0 || child_info.surface_id == old_surface)
        return fail("resize surface replacement");

    if (osaura_jx11_window_set_visible_as(1u, root, 0) != 0) return fail("hide root");
    if (osaura_jx11_window_hit_test(20, 14) != OSAURA_JX11_WINDOW_NONE) return fail("visibility inheritance");
    if (osaura_jx11_window_set_visible_as(1u, root, 1) != 0) return fail("show root");

    if (osaura_jx11_window_raise_as(1u, root) != 0) return fail("raise");
    if (osaura_jx11_window_lower_as(1u, root) != 0) return fail("lower");
    if (osaura_jx11_window_focus_as(2u, other) != 0) return fail("focus other");
    if (osaura_jx11_window_focused() != other) return fail("focus state");

    if (osaura_jx11_window_destroy_as(1u, root) != 0) return fail("recursive destroy");
    if (osaura_jx11_window_count() != 1u) return fail("recursive child destroy");
    if (osaura_jx11_window_destroy_as(2u, other) != 0) return fail("other destroy");
    if (osaura_jx11_window_count() != 0u) return fail("final count");

    osaura_jx11_window_shutdown();
    osaura_jx11_surface_shutdown();
    puts("JX11 WINDOW MANAGER: PASS");
    return 0;
}
