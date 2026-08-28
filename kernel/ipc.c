#include "ipc.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    osaura_ipc_message queue[OSAURA_IPC_QUEUE_DEPTH];
    uint32_t owner_task;
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    uint8_t used;
} osaura_ipc_channel;

static osaura_ipc_channel g_channels[OSAURA_IPC_CHANNEL_MAX];

static void zero_bytes(void *ptr, size_t bytes) {
    uint8_t *p = (uint8_t *)ptr;
    while (bytes--) *p++ = 0u;
}

static void copy_bytes(uint8_t *dst, const uint8_t *src, uint32_t bytes) {
    while (bytes--) *dst++ = *src++;
}

void osaura_ipc_init(void) {
    zero_bytes(g_channels, sizeof g_channels);
}

int osaura_ipc_channel_create(uint32_t owner_task, uint32_t *channel_id) {
    if (!channel_id) return -1;
    for (uint32_t i = 0u; i < OSAURA_IPC_CHANNEL_MAX; ++i) {
        if (g_channels[i].used) continue;
        zero_bytes(&g_channels[i], sizeof g_channels[i]);
        g_channels[i].used = 1u;
        g_channels[i].owner_task = owner_task;
        *channel_id = i;
        return 0;
    }
    return -2;
}

int osaura_ipc_channel_close(uint32_t owner_task, uint32_t channel_id) {
    if (channel_id >= OSAURA_IPC_CHANNEL_MAX || !g_channels[channel_id].used)
        return -1;
    if (g_channels[channel_id].owner_task != owner_task) return -2;
    zero_bytes(&g_channels[channel_id], sizeof g_channels[channel_id]);
    return 0;
}

int osaura_ipc_send(uint32_t sender_task,
                    uint32_t channel_id,
                    uint32_t type,
                    const void *payload,
                    uint32_t bytes) {
    if (channel_id >= OSAURA_IPC_CHANNEL_MAX || !g_channels[channel_id].used)
        return -1;
    if (bytes > OSAURA_IPC_PAYLOAD_MAX || (bytes && !payload)) return -2;
    osaura_ipc_channel *channel = &g_channels[channel_id];
    if (channel->count >= OSAURA_IPC_QUEUE_DEPTH) return -3;

    osaura_ipc_message *message = &channel->queue[channel->head];
    zero_bytes(message, sizeof *message);
    message->type = type;
    message->sender_task = sender_task;
    message->bytes = bytes;
    if (bytes) copy_bytes(message->payload, (const uint8_t *)payload, bytes);
    channel->head = (uint8_t)((channel->head + 1u) % OSAURA_IPC_QUEUE_DEPTH);
    ++channel->count;
    return 0;
}

int osaura_ipc_receive(uint32_t receiver_task,
                       uint32_t channel_id,
                       osaura_ipc_message *message) {
    if (!message || channel_id >= OSAURA_IPC_CHANNEL_MAX || !g_channels[channel_id].used)
        return -1;
    osaura_ipc_channel *channel = &g_channels[channel_id];
    if (channel->owner_task != receiver_task) return -2;
    if (!channel->count) return 0;

    *message = channel->queue[channel->tail];
    zero_bytes(&channel->queue[channel->tail], sizeof channel->queue[channel->tail]);
    channel->tail = (uint8_t)((channel->tail + 1u) % OSAURA_IPC_QUEUE_DEPTH);
    --channel->count;
    return 1;
}

uint32_t osaura_ipc_pending(uint32_t channel_id) {
    if (channel_id >= OSAURA_IPC_CHANNEL_MAX || !g_channels[channel_id].used) return 0u;
    return g_channels[channel_id].count;
}

uint32_t osaura_ipc_channel_owner(uint32_t channel_id) {
    if (channel_id >= OSAURA_IPC_CHANNEL_MAX || !g_channels[channel_id].used)
        return OSAURA_IPC_NONE;
    return g_channels[channel_id].owner_task;
}
