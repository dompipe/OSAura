#ifdef _WIN64

#include "runtime64.h"
#include "../../kernel/security.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define TASK64_MAX 16u

typedef struct {
    void *base;
    uint64_t bytes;
    uint32_t subject;
    uint8_t used;
} memory64_slot;

typedef struct {
    uint32_t subject;
    osaura_task_state state;
    osaura_task_role role;
    uint64_t ticks;
    uint64_t switches;
    const char *name;
    uint8_t used;
    uint8_t background;
} task64_slot;

typedef struct {
    osaura_ipc_message queue[OSAURA_IPC_QUEUE_DEPTH];
    uint32_t owner_task;
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    uint8_t used;
} ipc64_channel;

static LARGE_INTEGER g_qpc_frequency;
static memory64_slot g_memory[OSAURA_WINDOWS_MEMORY64_MAX];
static task64_slot g_tasks[TASK64_MAX];
static uint32_t g_task_count;
static uint32_t g_foreground_task;
static ipc64_channel g_ipc[OSAURA_IPC_CHANNEL_MAX];
static HANDLE g_input;
static uint8_t g_input_console;

int osaura_windows_clock64_init(void) {
    return QueryPerformanceFrequency(&g_qpc_frequency) && g_qpc_frequency.QuadPart > 0 ? 0 : -1;
}

uint64_t osaura_windows_clock64_ticks(void) {
    LARGE_INTEGER now;
    if (!QueryPerformanceCounter(&now)) return 0u;
    return (uint64_t)now.QuadPart;
}

uint64_t osaura_windows_clock64_ticks_to_ms(uint64_t ticks) {
    if (g_qpc_frequency.QuadPart <= 0) return 0u;
    uint64_t freq = (uint64_t)g_qpc_frequency.QuadPart;
    uint64_t whole = ticks / freq;
    uint64_t rem = ticks % freq;
    return whole * 1000u + (rem * 1000u) / freq;
}

uint64_t osaura_windows_clock64_ms_to_ticks(uint64_t ms) {
    if (g_qpc_frequency.QuadPart <= 0) return 0u;
    uint64_t freq = (uint64_t)g_qpc_frequency.QuadPart;
    uint64_t whole = ms / 1000u;
    uint64_t rem = ms % 1000u;
    return whole * freq + (rem * freq) / 1000u;
}

uint64_t osaura_windows_clock64_ms(void) {
    return osaura_windows_clock64_ticks_to_ms(osaura_windows_clock64_ticks());
}

int osaura_windows_memory64_init(void) {
    memset(g_memory, 0, sizeof g_memory);
    return 0;
}

int osaura_windows_memory64_alloc_as(uint32_t subject, uint64_t bytes, uint32_t *id_out) {
    if (!id_out || bytes == 0u || bytes > (1ull << 34)) return -1;
    for (uint32_t i = 0u; i < OSAURA_WINDOWS_MEMORY64_MAX; ++i) {
        if (g_memory[i].used) continue;
        void *base = VirtualAlloc(0, (SIZE_T)bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!base) return -(int)GetLastError();
        g_memory[i].base = base;
        g_memory[i].bytes = bytes;
        g_memory[i].subject = subject;
        g_memory[i].used = 1u;
        *id_out = i;
        return 0;
    }
    return -2;
}

int osaura_windows_memory64_free_as(uint32_t subject, uint32_t id) {
    if (id >= OSAURA_WINDOWS_MEMORY64_MAX || !g_memory[id].used || g_memory[id].subject != subject) return -1;
    if (!VirtualFree(g_memory[id].base, 0, MEM_RELEASE)) return -(int)GetLastError();
    memset(&g_memory[id], 0, sizeof g_memory[id]);
    return 0;
}

int osaura_windows_memory64_info_as(uint32_t subject, uint32_t id,
                                    osaura_windows_memory64_info *info) {
    if (!info || id >= OSAURA_WINDOWS_MEMORY64_MAX || !g_memory[id].used ||
        g_memory[id].subject != subject) return -1;
    info->id = id;
    info->subject = subject;
    info->bytes = g_memory[id].bytes;
    info->used = 1u;
    return 0;
}

void *osaura_windows_memory64_map_as(uint32_t subject, uint32_t id) {
    if (id >= OSAURA_WINDOWS_MEMORY64_MAX || !g_memory[id].used || g_memory[id].subject != subject) return 0;
    return g_memory[id].base;
}

int osaura_windows_task64_init(void) {
    memset(g_tasks, 0, sizeof g_tasks);
    g_tasks[0].subject = OSAURA_SECURITY_KERNEL_SUBJECT;
    g_tasks[0].state = OSAURA_TASK_RUNNABLE;
    g_tasks[0].role = OSAURA_TASK_ROLE_KERNEL;
    g_tasks[0].name = "WSJX64-KERNEL";
    g_tasks[0].used = 1u;
    g_tasks[1].subject = OSAURA_SECURITY_JX_SUBJECT;
    g_tasks[1].state = OSAURA_TASK_RUNNABLE;
    g_tasks[1].role = OSAURA_TASK_ROLE_SERVICE;
    g_tasks[1].name = "JX64-RUNTIME";
    g_tasks[1].used = 1u;
    g_tasks[2].subject = OSAURA_SECURITY_JX_SUBJECT;
    g_tasks[2].state = OSAURA_TASK_RUNNABLE;
    g_tasks[2].role = OSAURA_TASK_ROLE_PROGRAM;
    g_tasks[2].name = "WSJX64-SHELL";
    g_tasks[2].used = 1u;
    g_task_count = 3u;
    g_foreground_task = 2u;
    return 0;
}

uint32_t osaura_windows_task64_count(void) { return g_task_count; }

int osaura_windows_task64_info(uint32_t task_id, osaura_windows_task64_info *info) {
    if (!info || task_id >= TASK64_MAX || !g_tasks[task_id].used) return -1;
    info->task_id = task_id;
    info->subject = g_tasks[task_id].subject;
    info->state = g_tasks[task_id].state;
    info->role = g_tasks[task_id].role;
    info->ticks = g_tasks[task_id].ticks;
    info->switches = g_tasks[task_id].switches;
    info->name = g_tasks[task_id].name;
    return 0;
}

int osaura_windows_task64_set_state_as(uint32_t subject, uint32_t task_id,
                                       osaura_task_state state) {
    if (task_id >= TASK64_MAX || !g_tasks[task_id].used) return -1;
    if (subject != OSAURA_SECURITY_KERNEL_SUBJECT &&
        !osaura_security_check(subject, OSAURA_CAP_TASK_CONTROL)) return -2;
    g_tasks[task_id].state = state;
    ++g_tasks[task_id].switches;
    return 0;
}

int osaura_windows_job64_background_as(uint32_t subject, uint32_t task_id) {
    if (task_id >= TASK64_MAX || !g_tasks[task_id].used) return -1;
    if (subject != OSAURA_SECURITY_KERNEL_SUBJECT &&
        !osaura_security_check(subject, OSAURA_CAP_TASK_CONTROL)) return -2;
    g_tasks[task_id].background = 1u;
    if (g_foreground_task == task_id) g_foreground_task = OSAURA_TASK_NONE;
    return 0;
}

int osaura_windows_job64_foreground_as(uint32_t subject, uint32_t task_id) {
    if (task_id >= TASK64_MAX || !g_tasks[task_id].used) return -1;
    if (subject != OSAURA_SECURITY_KERNEL_SUBJECT &&
        !osaura_security_check(subject, OSAURA_CAP_TASK_CONTROL)) return -2;
    for (uint32_t i = 0u; i < TASK64_MAX; ++i) if (g_tasks[i].used) g_tasks[i].background = 0u;
    g_foreground_task = task_id;
    return 0;
}

uint32_t osaura_windows_job64_foreground(void) { return g_foreground_task; }

uint32_t osaura_windows_job64_background_count(void) {
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < TASK64_MAX; ++i)
        if (g_tasks[i].used && g_tasks[i].background) ++count;
    return count;
}

int osaura_windows_ipc64_init(void) {
    memset(g_ipc, 0, sizeof g_ipc);
    return 0;
}

int osaura_windows_ipc64_create(uint32_t owner_task, uint32_t *channel_id) {
    if (!channel_id) return -1;
    for (uint32_t i = 0u; i < OSAURA_IPC_CHANNEL_MAX; ++i) {
        if (g_ipc[i].used) continue;
        memset(&g_ipc[i], 0, sizeof g_ipc[i]);
        g_ipc[i].owner_task = owner_task;
        g_ipc[i].used = 1u;
        *channel_id = i;
        return 0;
    }
    return -2;
}

int osaura_windows_ipc64_close(uint32_t owner_task, uint32_t channel_id) {
    if (channel_id >= OSAURA_IPC_CHANNEL_MAX || !g_ipc[channel_id].used) return -1;
    if (g_ipc[channel_id].owner_task != owner_task) return -2;
    memset(&g_ipc[channel_id], 0, sizeof g_ipc[channel_id]);
    return 0;
}

int osaura_windows_ipc64_send(uint32_t sender_task, uint32_t channel_id,
                              uint32_t type, const void *payload, uint32_t bytes) {
    if (channel_id >= OSAURA_IPC_CHANNEL_MAX || !g_ipc[channel_id].used) return -1;
    if (bytes > OSAURA_IPC_PAYLOAD_MAX || (bytes && !payload)) return -2;
    ipc64_channel *channel = &g_ipc[channel_id];
    if (channel->count >= OSAURA_IPC_QUEUE_DEPTH) return -3;
    osaura_ipc_message *message = &channel->queue[channel->head];
    memset(message, 0, sizeof *message);
    message->type = type;
    message->sender_task = sender_task;
    message->bytes = bytes;
    if (bytes) memcpy(message->payload, payload, bytes);
    channel->head = (uint8_t)((channel->head + 1u) % OSAURA_IPC_QUEUE_DEPTH);
    ++channel->count;
    return 0;
}

int osaura_windows_ipc64_receive(uint32_t receiver_task, uint32_t channel_id,
                                 osaura_ipc_message *message) {
    if (!message || channel_id >= OSAURA_IPC_CHANNEL_MAX || !g_ipc[channel_id].used) return -1;
    ipc64_channel *channel = &g_ipc[channel_id];
    if (channel->owner_task != receiver_task) return -2;
    if (!channel->count) return 0;
    *message = channel->queue[channel->tail];
    memset(&channel->queue[channel->tail], 0, sizeof channel->queue[channel->tail]);
    channel->tail = (uint8_t)((channel->tail + 1u) % OSAURA_IPC_QUEUE_DEPTH);
    --channel->count;
    return 1;
}

uint32_t osaura_windows_ipc64_pending(uint32_t channel_id) {
    return channel_id < OSAURA_IPC_CHANNEL_MAX && g_ipc[channel_id].used
        ? g_ipc[channel_id].count : 0u;
}

int osaura_windows_input64_init(void) {
    DWORD mode = 0u;
    g_input = GetStdHandle(STD_INPUT_HANDLE);
    g_input_console = g_input != INVALID_HANDLE_VALUE && GetConsoleMode(g_input, &mode) ? 1u : 0u;
    return 0;
}

int osaura_windows_input64_is_console(void) { return g_input_console ? 1 : 0; }

int osaura_windows_input64_poll(uint16_t *virtual_key, uint32_t *unicode_codepoint) {
    if (!g_input_console) return 0;
    INPUT_RECORD record;
    DWORD count = 0u;
    while (PeekConsoleInputW(g_input, &record, 1u, &count) && count) {
        if (!ReadConsoleInputW(g_input, &record, 1u, &count)) return -(int)GetLastError();
        if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) continue;
        if (virtual_key) *virtual_key = record.Event.KeyEvent.wVirtualKeyCode;
        if (unicode_codepoint) *unicode_codepoint = (uint32_t)record.Event.KeyEvent.uChar.UnicodeChar;
        return 1;
    }
    return 0;
}

#endif
