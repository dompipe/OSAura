#include "clock-hot.h"
#include "hot-shadow.h"

#include <stdint.h>

#define OSAURA_CLOCK_HZ 100u

extern volatile uint64_t osaura_ticks;

static int hot_now_ticks(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    *(uint64_t *)request = osaura_ticks;
    return 1;
}

static int hot_now_ms(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    *(uint64_t *)request = (osaura_ticks * 1000u) / OSAURA_CLOCK_HZ;
    return 1;
}

static int hot_delta(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    osaura_clock_request *q = (osaura_clock_request *)request;
    q->result = q->b - q->a;
    return 1;
}

static int hot_deadline_after(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    osaura_clock_request *q = (osaura_clock_request *)request;
    q->result = q->a + q->b;
    return 1;
}

static int hot_deadline_reached(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    osaura_clock_request *q = (osaura_clock_request *)request;
    return (int64_t)(q->a - q->b) >= 0;
}

static int hot_ticks_to_ms(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    osaura_clock_request *q = (osaura_clock_request *)request;
    q->result = (q->a * 1000u) / OSAURA_CLOCK_HZ;
    return 1;
}

static int hot_ms_to_ticks(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    osaura_clock_request *q = (osaura_clock_request *)request;
    q->result = (q->a * OSAURA_CLOCK_HZ + 999u) / 1000u;
    return 1;
}

static int hot_wake(void *context, void *request) {
    (void)context;
    (void)request;
    return 1;
}

int osaura_clock_hot_bind(void) {
    int rc = 0;
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_NOW_TICKS, hot_now_ticks, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_NOW_MS, hot_now_ms, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_DELTA, hot_delta, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_DEADLINE_AFTER, hot_deadline_after, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_DEADLINE_REACHED, hot_deadline_reached, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_TICKS_TO_MS, hot_ticks_to_ms, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_MS_TO_TICKS, hot_ms_to_ticks, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_WAKE, hot_wake, 0);
    return rc == 0 ? 1 : 0;
}

uint64_t osaura_clock_now_ticks(void) {
    uint64_t out = 0u;
    (void)osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_NOW_TICKS), &out);
    return out;
}

uint64_t osaura_clock_now_ms(void) {
    uint64_t out = 0u;
    (void)osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_NOW_MS), &out);
    return out;
}

uint64_t osaura_clock_delta(uint64_t start, uint64_t end) {
    osaura_clock_request q = { start, end, 0u };
    (void)osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_DELTA), &q);
    return q.result;
}

uint64_t osaura_clock_deadline_after(uint64_t base, uint64_t delta) {
    osaura_clock_request q = { base, delta, 0u };
    (void)osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_DEADLINE_AFTER), &q);
    return q.result;
}

int osaura_clock_deadline_reached(uint64_t now, uint64_t deadline) {
    osaura_clock_request q = { now, deadline, 0u };
    return osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_DEADLINE_REACHED), &q) > 0;
}

uint64_t osaura_clock_ticks_to_ms(uint64_t ticks) {
    osaura_clock_request q = { ticks, 0u, 0u };
    (void)osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_TICKS_TO_MS), &q);
    return q.result;
}

uint64_t osaura_clock_ms_to_ticks(uint64_t ms) {
    osaura_clock_request q = { ms, 0u, 0u };
    (void)osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_MS_TO_TICKS), &q);
    return q.result;
}
