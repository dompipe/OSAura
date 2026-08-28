#include "jx11-window-bag.h"
#include "jx11-window.h"
#include "jx-bag-bus.h"
#include "../../kernel/processor-bus.h"

#include <string.h>

static osaura_jx11_window_bag_view g_view[OSAURA_JX11_WINDOW_MAX];
static uint8_t g_ready;

static int validate_window(uint32_t owner_subject, uint32_t window_id) {
    osaura_jx11_window_info info = {0};
    if (!g_ready || window_id >= OSAURA_JX11_WINDOW_MAX) return -1;
    if (osaura_jx11_window_get_info(window_id, &info) != 0) return -2;
    if (info.owner_subject != owner_subject) return -3;
    return 0;
}

int osaura_jx11_window_bag_init(void) {
    memset(g_view, 0, sizeof g_view);
    g_ready = 1u;
    return 0;
}

void osaura_jx11_window_bag_shutdown(void) {
    memset(g_view, 0, sizeof g_view);
    g_ready = 0u;
}

int osaura_jx11_window_bag_bind_as(uint32_t owner_subject,
                                   uint32_t window_id,
                                   uint64_t bag_id,
                                   uint64_t bag_generation,
                                   const void *borrowed_data,
                                   uint32_t borrowed_bytes) {
    int rc = validate_window(owner_subject, window_id);
    if (rc != 0) return rc;
    if (bag_id == OSAURA_JX11_WINDOW_BAG_NONE || bag_generation == 0ull) return -4;
    if (borrowed_bytes && !borrowed_data) return -5;

    osaura_jx11_window_bag_view *view = &g_view[window_id];
    view->window_id = window_id;
    view->owner_subject = owner_subject;
    view->bag_id = bag_id;
    view->bag_generation = bag_generation;
    view->borrowed_data = borrowed_data;
    view->borrowed_bytes = borrowed_bytes;
    view->bound = 1u;
    return 0;
}

int osaura_jx11_window_bag_rebind_as(uint32_t owner_subject,
                                     uint32_t window_id,
                                     uint64_t bag_generation,
                                     const void *borrowed_data,
                                     uint32_t borrowed_bytes) {
    int rc = validate_window(owner_subject, window_id);
    if (rc != 0) return rc;
    osaura_jx11_window_bag_view *view = &g_view[window_id];
    if (!view->bound || view->owner_subject != owner_subject) return -4;
    if (bag_generation <= view->bag_generation) return -5;
    if (borrowed_bytes && !borrowed_data) return -6;
    view->bag_generation = bag_generation;
    view->borrowed_data = borrowed_data;
    view->borrowed_bytes = borrowed_bytes;
    return 0;
}

int osaura_jx11_window_bag_unbind_as(uint32_t owner_subject, uint32_t window_id) {
    int rc = validate_window(owner_subject, window_id);
    if (rc != 0) return rc;
    osaura_jx11_window_bag_view *view = &g_view[window_id];
    if (!view->bound) return 0;
    if (view->owner_subject != owner_subject) return -4;
    memset(view, 0, sizeof *view);
    return 0;
}

int osaura_jx11_window_bag_get(uint32_t window_id, osaura_jx11_window_bag_view *out) {
    osaura_jx11_window_info info = {0};
    if (!g_ready || !out || window_id >= OSAURA_JX11_WINDOW_MAX) return -1;
    if (!g_view[window_id].bound) return 0;

    if (osaura_jx11_window_get_info(window_id, &info) != 0 ||
        info.owner_subject != g_view[window_id].owner_subject) {
        memset(&g_view[window_id], 0, sizeof g_view[window_id]);
        return 0;
    }

    *out = g_view[window_id];
    return 1;
}

int osaura_jx11_window_bag_publish_as(uint32_t owner_subject,
                                      uint32_t window_id,
                                      uint32_t change_kind,
                                      uint32_t source_pid,
                                      uint32_t flags) {
    int rc = validate_window(owner_subject, window_id);
    if (rc != 0) return rc;
    osaura_jx11_window_bag_view *view = &g_view[window_id];
    if (!view->bound || view->owner_subject != owner_subject) return -4;

    /* Visual attention becomes bus attention without changing Bag identity. */
    uint32_t listener = osaura_jx11_window_primary_listener();
    rc = osaura_processor_bus_set_priority_pid(
        listener == OSAURA_JX11_LISTENER_NONE ? OSAURA_PROCESSOR_BUS_PID_NONE : listener);
    if (rc != 0) return -5;

    osaura_jx_bag_bus_publication publication = {0};
    publication.bag_id = view->bag_id;
    publication.bag_generation = view->bag_generation;
    publication.pointed_data = view->borrowed_data;
    publication.pointed_bytes = view->borrowed_bytes;
    publication.change_kind = change_kind;
    publication.source_pid = source_pid;
    publication.flags = flags;
    return osaura_jx_bag_bus_publish(&publication);
}
