#ifndef OSAURA_JX11_LISTENER_EVENTS_H
#define OSAURA_JX11_LISTENER_EVENTS_H

#include "jx11-window.h"

#include <stdint.h>

#define OSAURA_JX11_LISTENER_EVENT_CACHE_MAX OSAURA_JX11_EVENT_QUEUE_MAX

/*
 * Listener routing sits above the security-owned JX11 event queue.
 * Callers retain the owner subject boundary while selecting the program PID
 * that should receive the event first.
 */
int osaura_jx11_listener_events_init(void);
void osaura_jx11_listener_events_shutdown(void);

/*
 * Pop the oldest event for listener_pid within owner_subject.
 * Events for sibling listeners are retained in router order, not discarded.
 * Returns 1 for an event, 0 when none is available, or a negative error.
 */
int osaura_jx11_listener_event_pop(uint32_t owner_subject,
                                   uint32_t listener_pid,
                                   osaura_jx11_event *event);

uint32_t osaura_jx11_listener_event_cached(void);

#endif
