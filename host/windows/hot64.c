#ifdef _WIN64

#include "hot64.h"
#include "runtime64.h"
#include "../../kernel/hot-shadow.h"

#include <string.h>

static int hot_clock_now_ticks(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    *(uint64_t *)request = osaura_windows_clock64_ticks();
    return 1;
}

static int hot_clock_now_ms(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    *(uint64_t *)request = osaura_windows_clock64_ms();
    return 1;
}

static int hot_clock_delta(void *context, void *request) {
    (void)context;
    osaura_clock_request *q = (osaura_clock_request *)request;
    if (!q) return -1;
    q->result = q->b - q->a;
    return 1;
}

static int hot_clock_deadline_after(void *context, void *request) {
    (void)context;
    osaura_clock_request *q = (osaura_clock_request *)request;
    if (!q) return -1;
    q->result = q->a + q->b;
    return 1;
}

static int hot_clock_deadline_reached(void *context, void *request) {
    (void)context;
    osaura_clock_request *q = (osaura_clock_request *)request;
    if (!q) return -1;
    return (int64_t)(q->a - q->b) >= 0 ? 1 : 0;
}

static int hot_clock_ticks_to_ms(void *context, void *request) {
    (void)context;
    osaura_clock_request *q = (osaura_clock_request *)request;
    if (!q) return -1;
    q->result = osaura_windows_clock64_ticks_to_ms(q->a);
    return 1;
}

static int hot_clock_ms_to_ticks(void *context, void *request) {
    (void)context;
    osaura_clock_request *q = (osaura_clock_request *)request;
    if (!q) return -1;
    q->result = osaura_windows_clock64_ms_to_ticks(q->a);
    return 1;
}

static int hot_clock_wake(void *context, void *request) {
    (void)context;
    (void)request;
    return 1;
}

static int hot_memory_zero(void *context, void *request) {
    (void)context;
    osaura_memory_request *q = (osaura_memory_request *)request;
    if (!q || (!q->dst && q->bytes)) return -1;
    memset(q->dst, 0, (size_t)q->bytes);
    return 1;
}

static int hot_memory_zero64(void *context, void *request) {
    (void)context;
    osaura_memory_request *q = (osaura_memory_request *)request;
    if (!q || (!q->dst && q->bytes)) return -1;
    memset(q->dst, 0, (size_t)(q->bytes * sizeof(uint64_t)));
    return 1;
}

static int hot_memory_copy(void *context, void *request) {
    (void)context;
    osaura_memory_request *q = (osaura_memory_request *)request;
    if (!q || (!q->dst && q->bytes) || (!q->src && q->bytes)) return -1;
    memcpy(q->dst, q->src, (size_t)q->bytes);
    return 1;
}

static int hot_memory_copy64(void *context, void *request) {
    (void)context;
    osaura_memory_request *q = (osaura_memory_request *)request;
    if (!q || (!q->dst && q->bytes) || (!q->src && q->bytes)) return -1;
    memcpy(q->dst, q->src, (size_t)(q->bytes * sizeof(uint64_t)));
    return 1;
}

static int hot_memory_move(void *context, void *request) {
    (void)context;
    osaura_memory_request *q = (osaura_memory_request *)request;
    if (!q || (!q->dst && q->bytes) || (!q->src && q->bytes)) return -1;
    memmove(q->dst, q->src, (size_t)q->bytes);
    return 1;
}

static int hot_memory_fill8(void *context, void *request) {
    (void)context;
    osaura_memory_request *q = (osaura_memory_request *)request;
    if (!q || (!q->dst && q->bytes)) return -1;
    memset(q->dst, (int)(uint8_t)q->value, (size_t)q->bytes);
    return 1;
}

static int hot_memory_compare(void *context, void *request) {
    (void)context;
    osaura_memory_request *q = (osaura_memory_request *)request;
    if (!q || (!q->dst && q->bytes) || (!q->src && q->bytes)) return -1;
    q->result = memcmp(q->dst, q->src, (size_t)q->bytes);
    return 1;
}

static int hot_memory_equal(void *context, void *request) {
    (void)context;
    osaura_memory_request *q = (osaura_memory_request *)request;
    if (!q || (!q->dst && q->bytes) || (!q->src && q->bytes)) return -1;
    q->result = memcmp(q->dst, q->src, (size_t)q->bytes) == 0 ? 1 : 0;
    return q->result;
}

static int hot_task_query(void *context, void *request) {
    uint32_t shadow = (uint32_t)(uintptr_t)context;
    osaura_task_hot_request *q = (osaura_task_hot_request *)request;
    osaura_windows_task64_info info = {0};
    if (!q) return -1;
    if (shadow == OSAURA_TASK_HOT_CURRENT) { q->task_id = osaura_windows_job64_foreground(); return 1; }
    if (shadow == OSAURA_TASK_HOT_COUNT) { q->value32 = osaura_windows_task64_count(); return 1; }
    if (shadow == OSAURA_TASK_HOT_RUNNING) { q->value32 = 1u; return 1; }
    if (osaura_windows_task64_get_info(q->task_id, &info) != 0) return -2;
    switch (shadow) {
        case OSAURA_TASK_HOT_TICKS: q->value64 = info.ticks; break;
        case OSAURA_TASK_HOT_SWITCHES: q->value64 = info.switches; break;
        case OSAURA_TASK_HOT_STATE: q->value32 = (uint32_t)info.state; break;
        case OSAURA_TASK_HOT_ROLE: q->value32 = (uint32_t)info.role; break;
        case OSAURA_TASK_HOT_NAME: q->text = info.name; break;
        default: return -3;
    }
    return 1;
}

static int hot_job(void *context, void *request) {
    uint32_t shadow = (uint32_t)(uintptr_t)context;
    osaura_job_hot_request *q = (osaura_job_hot_request *)request;
    if (!q) return -1;
    switch (shadow) {
        case OSAURA_JOB_HOT_ATTACH: return q->task_id < osaura_windows_task64_count() ? 1 : -2;
        case OSAURA_JOB_HOT_BACKGROUND: return osaura_windows_job64_background_as(q->subject, q->task_id) == 0 ? 1 : -2;
        case OSAURA_JOB_HOT_FOREGROUND: return osaura_windows_job64_foreground_as(q->subject, q->task_id) == 0 ? 1 : -2;
        case OSAURA_JOB_HOT_SET_STATE: return osaura_windows_task64_set_state_as(q->subject, q->task_id, q->state) == 0 ? 1 : -2;
        case OSAURA_JOB_HOT_GET_FG: q->task_id = osaura_windows_job64_foreground(); return 1;
        case OSAURA_JOB_HOT_BG_COUNT: q->value = osaura_windows_job64_background_count(); return 1;
        case OSAURA_JOB_HOT_BG_AT: return -4;
        case OSAURA_JOB_HOT_WAKE: return osaura_windows_task64_set_state_as(q->subject, q->task_id, OSAURA_TASK_RUNNABLE) == 0 ? 1 : -2;
        default: return -3;
    }
}

static int hot_ipc(void *context, void *request) {
    uint32_t shadow = (uint32_t)(uintptr_t)context;
    osaura_ipc_request *q = (osaura_ipc_request *)request;
    if (!q) return -1;
    switch (shadow) {
        case OSAURA_IPC_SEND: return osaura_windows_ipc64_send(q->actor_task, q->channel_id, q->type, q->payload, q->bytes) == 0 ? 1 : -2;
        case OSAURA_IPC_RECEIVE: return osaura_windows_ipc64_receive(q->actor_task, q->channel_id, q->message);
        case OSAURA_IPC_PENDING: q->value = osaura_windows_ipc64_pending(q->channel_id); return 1;
        case OSAURA_IPC_OWNER: return -4;
        case OSAURA_IPC_CREATE: return osaura_windows_ipc64_create(q->actor_task, &q->channel_id) == 0 ? 1 : -2;
        case OSAURA_IPC_CLOSE: return osaura_windows_ipc64_close(q->actor_task, q->channel_id) == 0 ? 1 : -2;
        case OSAURA_IPC_SENDRECV: {
            int rc = osaura_windows_ipc64_send(q->actor_task, q->channel_id, q->type, q->payload, q->bytes);
            return rc == 0 ? osaura_windows_ipc64_receive(q->actor_task, q->channel_id, q->message) : -2;
        }
        case OSAURA_IPC_POLL: q->value = osaura_windows_ipc64_pending(q->channel_id); return 1;
        default: return -3;
    }
}

static int hot_input(void *context, void *request) {
    uint32_t shadow = (uint32_t)(uintptr_t)context;
    osaura_windows_input64_hot_request *q = (osaura_windows_input64_hot_request *)request;
    if (shadow == OSAURA_WINDOWS_INPUT64_HOT_READY) {
        if (q) q->value = osaura_windows_input64_is_console() ? 1u : 0u;
        return 1;
    }
    if (shadow == OSAURA_WINDOWS_INPUT64_HOT_POLL || shadow == OSAURA_WINDOWS_INPUT64_HOT_POP) {
        if (!q) return -1;
        return osaura_windows_input64_poll(&q->virtual_key, &q->codepoint);
    }
    if (shadow == OSAURA_WINDOWS_INPUT64_HOT_WAKE) return 1;
    return 0;
}

int osaura_windows_hot64_bind(void) {
    int rc = 0;
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_NOW_TICKS, hot_clock_now_ticks, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_NOW_MS, hot_clock_now_ms, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_DELTA, hot_clock_delta, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_DEADLINE_AFTER, hot_clock_deadline_after, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_DEADLINE_REACHED, hot_clock_deadline_reached, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_TICKS_TO_MS, hot_clock_ticks_to_ms, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_MS_TO_TICKS, hot_clock_ms_to_ticks, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_WAKE, hot_clock_wake, 0);

    osaura_shadow_fn memory_fn[8] = { hot_memory_zero, hot_memory_zero64, hot_memory_copy, hot_memory_copy64,
                                      hot_memory_move, hot_memory_fill8, hot_memory_compare, hot_memory_equal };
    for (uint8_t i = 0u; i < 8u; ++i) rc |= osaura_hot_bind(OSAURA_HOT_BANK_MEMORY, i, memory_fn[i], 0);
    for (uint8_t i = 0u; i < 8u; ++i) rc |= osaura_hot_bind(OSAURA_HOT_BANK_TASK, i, hot_task_query, (void *)(uintptr_t)i);
    for (uint8_t i = 0u; i < 8u; ++i) rc |= osaura_hot_bind(OSAURA_HOT_BANK_JOBS, i, hot_job, (void *)(uintptr_t)i);
    for (uint8_t i = 0u; i < 8u; ++i) rc |= osaura_hot_bind(OSAURA_HOT_BANK_IPC, i, hot_ipc, (void *)(uintptr_t)i);
    for (uint8_t i = 0u; i < 8u; ++i) rc |= osaura_hot_bind(OSAURA_HOT_BANK_INPUT, i, hot_input, (void *)(uintptr_t)i);
    return rc == 0 ? 0 : -1;
}

int osaura_windows_hot64_self_test(void) {
    uint64_t now = 0u;
    if (osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_CLOCK, OSAURA_CLOCK_HOT_NOW_MS), &now) <= 0) return -1;

    uint8_t src[8] = {1,2,3,4,5,6,7,8};
    uint8_t dst[8] = {0};
    osaura_memory_request m = { dst, src, sizeof src, 0u, 0 };
    if (osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_MEMORY, OSAURA_MEMORY_HOT_COPY), &m) <= 0) return -2;
    if (memcmp(src, dst, sizeof src) != 0) return -3;

    osaura_task_hot_request task = {0};
    if (osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_TASK, OSAURA_TASK_HOT_COUNT), &task) <= 0) return -4;
    if (task.value32 < 3u) return -5;

    osaura_ipc_request ipc = {0};
    ipc.actor_task = 2u;
    if (osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_IPC, OSAURA_IPC_CREATE), &ipc) <= 0) return -6;
    if (osaura_windows_ipc64_close(2u, ipc.channel_id) != 0) return -7;

    osaura_windows_input64_hot_request input = {0};
    if (osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_INPUT, OSAURA_WINDOWS_INPUT64_HOT_READY), &input) <= 0) return -8;
    return 0;
}

#endif
