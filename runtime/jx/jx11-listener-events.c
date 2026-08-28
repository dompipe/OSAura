#include "jx11-listener-events.h"

#include <string.h>

typedef struct {
    osaura_jx11_event event;
    uint32_t owner_subject;
    uint32_t listener_pid;
    uint64_t sequence;
    uint8_t used;
} jx11_listener_cached_event;

static jx11_listener_cached_event g_cache[OSAURA_JX11_LISTENER_EVENT_CACHE_MAX];
static uint64_t g_sequence;
static uint32_t g_cached;
static uint8_t g_ready;

static uint32_t resolve_listener(uint32_t window_id) {
    uint32_t guard = 0u;
    while (window_id != OSAURA_JX11_WINDOW_NONE &&
           window_id < OSAURA_JX11_WINDOW_MAX &&
           guard++ < OSAURA_JX11_WINDOW_MAX) {
        osaura_jx11_window_info info = {0};
        if (osaura_jx11_window_get_info(window_id, &info) != 0)
            return OSAURA_JX11_LISTENER_NONE;
        if (info.listener_pid != OSAURA_JX11_LISTENER_NONE)
            return info.listener_pid;
        window_id = info.parent_id;
    }
    return OSAURA_JX11_LISTENER_NONE;
}

static int cache_event(uint32_t owner_subject,
                       uint32_t listener_pid,
                       const osaura_jx11_event *event) {
    if (g_cached >= OSAURA_JX11_LISTENER_EVENT_CACHE_MAX) return -1;
    for (uint32_t i = 0u; i < OSAURA_JX11_LISTENER_EVENT_CACHE_MAX; ++i) {
        if (g_cache[i].used) continue;
        g_cache[i].event = *event;
        g_cache[i].owner_subject = owner_subject;
        g_cache[i].listener_pid = listener_pid;
        g_cache[i].sequence = ++g_sequence;
        if (g_sequence == 0ull) g_sequence = ++g_sequence;
        g_cache[i].used = 1u;
        ++g_cached;
        return 0;
    }
    return -2;
}

static int pop_cached(uint32_t owner_subject,
                      uint32_t listener_pid,
                      osaura_jx11_event *event) {
    uint32_t best = OSAURA_JX11_LISTENER_EVENT_CACHE_MAX;
    uint64_t best_sequence = 0ull;
    for (uint32_t i = 0u; i < OSAURA_JX11_LISTENER_EVENT_CACHE_MAX; ++i) {
        if (!g_cache[i].used ||
            g_cache[i].owner_subject != owner_subject ||
            g_cache[i].listener_pid != listener_pid)
            continue;
        if (best == OSAURA_JX11_LISTENER_EVENT_CACHE_MAX ||
            g_cache[i].sequence < best_sequence) {
            best = i;
            best_sequence = g_cache[i].sequence;
        }
    }
    if (best == OSAURA_JX11_LISTENER_EVENT_CACHE_MAX) return 0;
    *event = g_cache[best].event;
    memset(&g_cache[best], 0, sizeof g_cache[best]);
    --g_cached;
    return 1;
}

int osaura_jx11_listener_events_init(void) {
    memset(g_cache, 0, sizeof g_cache);
    g_sequence = 0ull;
    g_cached = 0u;
    g_ready = 1u;
    return 0;
}

void osaura_jx11_listener_events_shutdown(void) {
    memset(g_cache, 0, sizeof g_cache);
    g_sequence = 0ull;
    g_cached = 0u;
    g_ready = 0u;
}

int osaura_jx11_listener_event_pop(uint32_t owner_subject,
                                   uint32_t listener_pid,
                                   osaura_jx11_event *event) {
    if (!g_ready || !event || listener_pid == OSAURA_JX11_LISTENER_NONE)
        return -1;

    int rc = pop_cached(owner_subject, listener_pid, event);
    if (rc != 0) return rc;

    for (uint32_t scan = 0u; scan < OSAURA_JX11_EVENT_QUEUE_MAX; ++scan) {
        osaura_jx11_event candidate = {0};
        rc = osaura_jx11_window_event_pop(owner_subject, &candidate);
        if (rc <= 0) return rc;

        uint32_t routed_listener = resolve_listener(candidate.window_id);
        if (routed_listener == listener_pid) {
            *event = candidate;
            return 1;
        }

        if (cache_event(owner_subject, routed_listener, &candidate) != 0)
            return -2;
    }
    return 0;
}

uint32_t osaura_jx11_listener_event_cached(void) {
    return g_ready ? g_cached : 0u;
}
