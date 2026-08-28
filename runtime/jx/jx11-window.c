#include "jx11-window.h"
#include "jx11-surface.h"

#include <string.h>
#include <stdint.h>

typedef struct {
    uint32_t owner;
    uint32_t parent;
    uint32_t surface;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    int32_t z;
    uint8_t visible;
    uint8_t used;
} jx11_window_slot;

static jx11_window_slot g_window[OSAURA_JX11_WINDOW_MAX];
static osaura_jx11_event g_event[OSAURA_JX11_EVENT_QUEUE_MAX];
static uint32_t g_event_head;
static uint32_t g_event_tail;
static uint32_t g_event_count;
static uint32_t g_focus = OSAURA_JX11_WINDOW_NONE;
static int32_t g_cursor_x;
static int32_t g_cursor_y;
static uint32_t g_buttons;
static int32_t g_z_top;
static int32_t g_z_bottom;
static uint8_t g_ready;

static int valid_window(uint32_t id, jx11_window_slot **out) {
    if (!g_ready || id >= OSAURA_JX11_WINDOW_MAX) return -1;
    if (!g_window[id].used) return -2;
    if (out) *out = &g_window[id];
    return 0;
}

static int valid_owned(uint32_t owner, uint32_t id, jx11_window_slot **out) {
    int rc = valid_window(id, out);
    if (rc != 0) return rc;
    if (g_window[id].owner != owner) return -3;
    return 0;
}

static void absolute_position(uint32_t id, int32_t *x, int32_t *y) {
    int64_t ax = 0;
    int64_t ay = 0;
    uint32_t guard = 0u;
    while (id != OSAURA_JX11_WINDOW_NONE && id < OSAURA_JX11_WINDOW_MAX && g_window[id].used && guard++ < OSAURA_JX11_WINDOW_MAX) {
        ax += g_window[id].x;
        ay += g_window[id].y;
        id = g_window[id].parent;
    }
    if (ax > INT32_MAX) ax = INT32_MAX;
    if (ax < INT32_MIN) ax = INT32_MIN;
    if (ay > INT32_MAX) ay = INT32_MAX;
    if (ay < INT32_MIN) ay = INT32_MIN;
    *x = (int32_t)ax;
    *y = (int32_t)ay;
}

static int ancestor_visible(uint32_t id) {
    uint32_t guard = 0u;
    while (id != OSAURA_JX11_WINDOW_NONE && id < OSAURA_JX11_WINDOW_MAX && g_window[id].used && guard++ < OSAURA_JX11_WINDOW_MAX) {
        if (!g_window[id].visible) return 0;
        id = g_window[id].parent;
    }
    return 1;
}

static int enqueue(uint32_t type, uint32_t id, int32_t x, int32_t y, uint32_t code, uint32_t value) {
    if (g_event_count >= OSAURA_JX11_EVENT_QUEUE_MAX) return -1;
    osaura_jx11_event *e = &g_event[g_event_tail];
    e->type = type;
    e->window_id = id;
    e->owner_subject = (id < OSAURA_JX11_WINDOW_MAX && g_window[id].used) ? g_window[id].owner : 0u;
    e->x = x;
    e->y = y;
    e->code = code;
    e->value = value;
    g_event_tail = (g_event_tail + 1u) % OSAURA_JX11_EVENT_QUEUE_MAX;
    ++g_event_count;
    return 0;
}

static void sync_surface(uint32_t id) {
    jx11_window_slot *w = &g_window[id];
    int32_t ax = 0, ay = 0;
    absolute_position(id, &ax, &ay);
    (void)osaura_jx11_surface_move_as(w->owner, w->surface, ax, ay);
    (void)osaura_jx11_surface_set_z_as(w->owner, w->surface, w->z);
    (void)osaura_jx11_surface_set_visible_as(w->owner, w->surface, ancestor_visible(id));
}

static void sync_descendants(uint32_t parent) {
    for (uint32_t i = 0; i < OSAURA_JX11_WINDOW_MAX; ++i) {
        if (g_window[i].used && g_window[i].parent == parent) {
            sync_surface(i);
            sync_descendants(i);
        }
    }
}

int osaura_jx11_window_init(void) {
    memset(g_window, 0, sizeof g_window);
    memset(g_event, 0, sizeof g_event);
    g_event_head = g_event_tail = g_event_count = 0u;
    g_focus = OSAURA_JX11_WINDOW_NONE;
    g_cursor_x = g_cursor_y = 0;
    g_buttons = 0u;
    g_z_top = 0;
    g_z_bottom = 0;
    g_ready = 1u;
    return 0;
}

void osaura_jx11_window_shutdown(void) {
    if (g_ready) {
        for (uint32_t i = 0; i < OSAURA_JX11_WINDOW_MAX; ++i)
            if (g_window[i].used)
                (void)osaura_jx11_surface_destroy(g_window[i].owner, g_window[i].surface);
    }
    memset(g_window, 0, sizeof g_window);
    g_ready = 0u;
    g_focus = OSAURA_JX11_WINDOW_NONE;
    g_event_count = 0u;
}

int osaura_jx11_window_create(uint32_t owner_subject,
                              uint32_t parent_id,
                              int32_t x,
                              int32_t y,
                              uint32_t width,
                              uint32_t height,
                              uint32_t *window_id) {
    if (!g_ready || !window_id || !width || !height) return -1;
    if (parent_id != OSAURA_JX11_WINDOW_NONE) {
        jx11_window_slot *parent = 0;
        if (valid_owned(owner_subject, parent_id, &parent) != 0) return -2;
        (void)parent;
    }
    uint32_t id = OSAURA_JX11_WINDOW_NONE;
    for (uint32_t i = 0; i < OSAURA_JX11_WINDOW_MAX; ++i) if (!g_window[i].used) { id = i; break; }
    if (id == OSAURA_JX11_WINDOW_NONE) return -3;
    uint32_t surface = OSAURA_JX11_SURFACE_NONE;
    if (osaura_jx11_surface_create(owner_subject, width, height, &surface) != 0) return -4;
    jx11_window_slot *w = &g_window[id];
    memset(w, 0, sizeof *w);
    w->owner = owner_subject;
    w->parent = parent_id;
    w->surface = surface;
    w->x = x;
    w->y = y;
    w->width = width;
    w->height = height;
    w->z = ++g_z_top;
    w->visible = 1u;
    w->used = 1u;
    sync_surface(id);
    *window_id = id;
    return 0;
}

int osaura_jx11_window_destroy_as(uint32_t owner_subject, uint32_t window_id) {
    jx11_window_slot *w = 0;
    int rc = valid_owned(owner_subject, window_id, &w);
    if (rc != 0) return rc;
    for (uint32_t i = 0; i < OSAURA_JX11_WINDOW_MAX; ++i)
        if (g_window[i].used && g_window[i].parent == window_id)
            (void)osaura_jx11_window_destroy_as(owner_subject, i);
    if (g_focus == window_id) g_focus = OSAURA_JX11_WINDOW_NONE;
    (void)enqueue(OSAURA_JX11_EVENT_CLOSE, window_id, 0, 0, 0, 0);
    (void)osaura_jx11_surface_destroy(owner_subject, w->surface);
    memset(w, 0, sizeof *w);
    return 0;
}

int osaura_jx11_window_get_info(uint32_t window_id, osaura_jx11_window_info *out) {
    jx11_window_slot *w = 0;
    if (!out || valid_window(window_id, &w) != 0) return -1;
    out->window_id = window_id; out->owner_subject = w->owner; out->parent_id = w->parent; out->surface_id = w->surface;
    out->x = w->x; out->y = w->y; out->width = w->width; out->height = w->height; out->z = w->z;
    out->visible = w->visible; out->focused = g_focus == window_id ? 1u : 0u;
    return 0;
}

int osaura_jx11_window_move_as(uint32_t owner_subject, uint32_t window_id, int32_t x, int32_t y) {
    jx11_window_slot *w = 0;
    int rc = valid_owned(owner_subject, window_id, &w); if (rc != 0) return rc;
    w->x = x; w->y = y; sync_surface(window_id); sync_descendants(window_id);
    return enqueue(OSAURA_JX11_EVENT_MOVE, window_id, x, y, 0, 0);
}

int osaura_jx11_window_resize_as(uint32_t owner_subject, uint32_t window_id, uint32_t width, uint32_t height) {
    jx11_window_slot *w = 0;
    int rc = valid_owned(owner_subject, window_id, &w); if (rc != 0 || !width || !height) return rc ? rc : -4;
    uint32_t new_surface = OSAURA_JX11_SURFACE_NONE;
    if (osaura_jx11_surface_create(owner_subject, width, height, &new_surface) != 0) return -5;
    (void)osaura_jx11_surface_destroy(owner_subject, w->surface);
    w->surface = new_surface; w->width = width; w->height = height; sync_surface(window_id);
    return enqueue(OSAURA_JX11_EVENT_RESIZE, window_id, 0, 0, width, height);
}

int osaura_jx11_window_set_visible_as(uint32_t owner_subject, uint32_t window_id, int visible) {
    jx11_window_slot *w = 0; int rc = valid_owned(owner_subject, window_id, &w); if (rc != 0) return rc;
    w->visible = visible ? 1u : 0u; sync_surface(window_id); sync_descendants(window_id); return 0;
}

int osaura_jx11_window_raise_as(uint32_t owner_subject, uint32_t window_id) {
    jx11_window_slot *w = 0; int rc = valid_owned(owner_subject, window_id, &w); if (rc != 0) return rc;
    w->z = ++g_z_top; sync_surface(window_id); return 0;
}

int osaura_jx11_window_lower_as(uint32_t owner_subject, uint32_t window_id) {
    jx11_window_slot *w = 0; int rc = valid_owned(owner_subject, window_id, &w); if (rc != 0) return rc;
    w->z = --g_z_bottom; sync_surface(window_id); return 0;
}

int osaura_jx11_window_focus_as(uint32_t owner_subject, uint32_t window_id) {
    jx11_window_slot *w = 0; int rc = valid_owned(owner_subject, window_id, &w); if (rc != 0) return rc;
    if (g_focus == window_id) return 0;
    uint32_t old = g_focus;
    g_focus = window_id;
    if (old != OSAURA_JX11_WINDOW_NONE && old < OSAURA_JX11_WINDOW_MAX && g_window[old].used)
        (void)enqueue(OSAURA_JX11_EVENT_BLUR, old, 0, 0, 0, 0);
    (void)enqueue(OSAURA_JX11_EVENT_FOCUS, window_id, 0, 0, 0, 0);
    return osaura_jx11_window_raise_as(owner_subject, window_id);
}

uint32_t osaura_jx11_window_focused(void) { return g_focus; }

uint32_t osaura_jx11_window_hit_test(int32_t x, int32_t y) {
    uint32_t best = OSAURA_JX11_WINDOW_NONE;
    int32_t best_z = INT32_MIN;
    for (uint32_t i = 0; i < OSAURA_JX11_WINDOW_MAX; ++i) {
        jx11_window_slot *w = &g_window[i];
        if (!w->used || !ancestor_visible(i)) continue;
        int32_t ax = 0, ay = 0; absolute_position(i, &ax, &ay);
        int64_t right = (int64_t)ax + w->width, bottom = (int64_t)ay + w->height;
        if (x < ax || y < ay || (int64_t)x >= right || (int64_t)y >= bottom) continue;
        if (best == OSAURA_JX11_WINDOW_NONE || w->z > best_z || (w->z == best_z && i > best)) { best = i; best_z = w->z; }
    }
    return best;
}

int osaura_jx11_window_pointer(int32_t x, int32_t y, uint32_t buttons, uint32_t changed_button, int pressed) {
    if (!g_ready) return -1;
    g_cursor_x = x; g_cursor_y = y; g_buttons = buttons;
    uint32_t hit = osaura_jx11_window_hit_test(x, y);
    if (hit == OSAURA_JX11_WINDOW_NONE) return 0;
    int32_t ax = 0, ay = 0; absolute_position(hit, &ax, &ay);
    if (changed_button && pressed) (void)osaura_jx11_window_focus_as(g_window[hit].owner, hit);
    return enqueue(changed_button ? (pressed ? OSAURA_JX11_EVENT_POINTER_DOWN : OSAURA_JX11_EVENT_POINTER_UP) : OSAURA_JX11_EVENT_POINTER_MOVE,
                   hit, x - ax, y - ay, changed_button, buttons);
}

int osaura_jx11_window_key(uint32_t code, int pressed) {
    if (!g_ready || g_focus == OSAURA_JX11_WINDOW_NONE || !g_window[g_focus].used) return 0;
    return enqueue(pressed ? OSAURA_JX11_EVENT_KEY_DOWN : OSAURA_JX11_EVENT_KEY_UP, g_focus, 0, 0, code, 0);
}

int osaura_jx11_window_event_pop(uint32_t owner_subject, osaura_jx11_event *event) {
    if (!event) return -1;
    uint32_t scans = g_event_count;
    while (scans--) {
        osaura_jx11_event e = g_event[g_event_head];
        g_event_head = (g_event_head + 1u) % OSAURA_JX11_EVENT_QUEUE_MAX;
        --g_event_count;
        if (e.owner_subject == owner_subject) { *event = e; return 1; }
        if (g_event_count < OSAURA_JX11_EVENT_QUEUE_MAX) {
            g_event[g_event_tail] = e;
            g_event_tail = (g_event_tail + 1u) % OSAURA_JX11_EVENT_QUEUE_MAX;
            ++g_event_count;
        }
    }
    return 0;
}

int osaura_jx11_window_compose(void) { return osaura_jx11_surface_compose(); }

uint32_t osaura_jx11_window_count(void) {
    uint32_t count = 0u; for (uint32_t i = 0; i < OSAURA_JX11_WINDOW_MAX; ++i) if (g_window[i].used) ++count; return count;
}
