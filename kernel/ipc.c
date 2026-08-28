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
static osaura_shadow_table g_ipc_shadows;

static void zero_bytes(void *ptr, size_t bytes) {
    uint8_t *p = (uint8_t *)ptr;
    while (bytes--) *p++ = 0u;
}

static void copy_bytes(uint8_t *dst, const uint8_t *src, uint32_t bytes) {
    while (bytes--) *dst++ = *src++;
}

static int raw_create(uint32_t owner_task, uint32_t *channel_id) {
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

static int raw_close(uint32_t owner_task, uint32_t channel_id) {
    if (channel_id >= OSAURA_IPC_CHANNEL_MAX || !g_channels[channel_id].used) return -1;
    if (g_channels[channel_id].owner_task != owner_task) return -2;
    zero_bytes(&g_channels[channel_id], sizeof g_channels[channel_id]);
    return 0;
}

static int raw_send(uint32_t sender_task, uint32_t channel_id, uint32_t type,
                    const void *payload, uint32_t bytes) {
    if (channel_id >= OSAURA_IPC_CHANNEL_MAX || !g_channels[channel_id].used) return -1;
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

static int raw_receive(uint32_t receiver_task, uint32_t channel_id,
                       osaura_ipc_message *message) {
    if (!message || channel_id >= OSAURA_IPC_CHANNEL_MAX || !g_channels[channel_id].used) return -1;
    osaura_ipc_channel *channel = &g_channels[channel_id];
    if (channel->owner_task != receiver_task) return -2;
    if (!channel->count) return 0;
    *message = channel->queue[channel->tail];
    zero_bytes(&channel->queue[channel->tail], sizeof channel->queue[channel->tail]);
    channel->tail = (uint8_t)((channel->tail + 1u) % OSAURA_IPC_QUEUE_DEPTH);
    --channel->count;
    return 1;
}

static int shadow_send(void *context, void *opaque) {
    (void)context;
    osaura_ipc_request *r = (osaura_ipc_request *)opaque;
    return r ? raw_send(r->actor_task, r->channel_id, r->type, r->payload, r->bytes) : -1;
}

static int shadow_receive(void *context, void *opaque) {
    (void)context;
    osaura_ipc_request *r = (osaura_ipc_request *)opaque;
    return r ? raw_receive(r->actor_task, r->channel_id, r->message) : -1;
}

static int shadow_pending(void *context, void *opaque) {
    (void)context;
    osaura_ipc_request *r = (osaura_ipc_request *)opaque;
    if (!r || r->channel_id >= OSAURA_IPC_CHANNEL_MAX || !g_channels[r->channel_id].used) return -1;
    r->value = g_channels[r->channel_id].count;
    return 0;
}

static int shadow_owner(void *context, void *opaque) {
    (void)context;
    osaura_ipc_request *r = (osaura_ipc_request *)opaque;
    if (!r || r->channel_id >= OSAURA_IPC_CHANNEL_MAX || !g_channels[r->channel_id].used) return -1;
    r->value = g_channels[r->channel_id].owner_task;
    return 0;
}

static int shadow_create(void *context, void *opaque) {
    (void)context;
    osaura_ipc_request *r = (osaura_ipc_request *)opaque;
    return r ? raw_create(r->actor_task, &r->channel_id) : -1;
}

static int shadow_close(void *context, void *opaque) {
    (void)context;
    osaura_ipc_request *r = (osaura_ipc_request *)opaque;
    return r ? raw_close(r->actor_task, r->channel_id) : -1;
}

static int shadow_sendrecv(void *context, void *opaque) {
    (void)context;
    osaura_ipc_request *r = (osaura_ipc_request *)opaque;
    if (!r) return -1;
    int rc = raw_send(r->actor_task, r->channel_id, r->type, r->payload, r->bytes);
    if (rc != 0) return rc;
    return raw_receive(r->actor_task, r->channel_id, r->message);
}

static int shadow_poll(void *context, void *opaque) {
    return shadow_pending(context, opaque);
}

void osaura_ipc_init(void) {
    zero_bytes(g_channels, sizeof g_channels);
    osaura_shadow_table_init(&g_ipc_shadows);
    (void)osaura_shadow_bind(&g_ipc_shadows, OSAURA_HOT_BANK_IPC, OSAURA_IPC_SEND, shadow_send, 0);
    (void)osaura_shadow_bind(&g_ipc_shadows, OSAURA_HOT_BANK_IPC, OSAURA_IPC_RECEIVE, shadow_receive, 0);
    (void)osaura_shadow_bind(&g_ipc_shadows, OSAURA_HOT_BANK_IPC, OSAURA_IPC_PENDING, shadow_pending, 0);
    (void)osaura_shadow_bind(&g_ipc_shadows, OSAURA_HOT_BANK_IPC, OSAURA_IPC_OWNER, shadow_owner, 0);
    (void)osaura_shadow_bind(&g_ipc_shadows, OSAURA_HOT_BANK_IPC, OSAURA_IPC_CREATE, shadow_create, 0);
    (void)osaura_shadow_bind(&g_ipc_shadows, OSAURA_HOT_BANK_IPC, OSAURA_IPC_CLOSE, shadow_close, 0);
    (void)osaura_shadow_bind(&g_ipc_shadows, OSAURA_HOT_BANK_IPC, OSAURA_IPC_SENDRECV, shadow_sendrecv, 0);
    (void)osaura_shadow_bind(&g_ipc_shadows, OSAURA_HOT_BANK_IPC, OSAURA_IPC_POLL, shadow_poll, 0);
}

int osaura_ipc_dispatch(uint8_t selector, osaura_ipc_request *request) {
    return osaura_shadow_dispatch(&g_ipc_shadows, OSAURA_HOT_BANK_IPC, selector, request);
}

int osaura_ipc_dispatch_opcode(uint8_t opcode, osaura_ipc_request *request) {
    return osaura_shadow_dispatch_opcode(&g_ipc_shadows, opcode, request);
}

const osaura_shadow_table *osaura_ipc_shadows(void) {
    return &g_ipc_shadows;
}

int osaura_ipc_channel_create(uint32_t owner_task, uint32_t *channel_id) {
    osaura_ipc_request request = {0};
    request.actor_task = owner_task;
    int rc = osaura_ipc_dispatch_opcode(
        osaura_hot_opcode(OSAURA_HOT_BANK_IPC, OSAURA_IPC_CREATE), &request);
    if (rc == 0 && channel_id) *channel_id = request.channel_id;
    return rc;
}

int osaura_ipc_channel_close(uint32_t owner_task, uint32_t channel_id) {
    osaura_ipc_request request = {0};
    request.actor_task = owner_task;
    request.channel_id = channel_id;
    return osaura_ipc_dispatch_opcode(
        osaura_hot_opcode(OSAURA_HOT_BANK_IPC, OSAURA_IPC_CLOSE), &request);
}

int osaura_ipc_send(uint32_t sender_task, uint32_t channel_id, uint32_t type,
                    const void *payload, uint32_t bytes) {
    osaura_ipc_request request = {0};
    request.actor_task = sender_task;
    request.channel_id = channel_id;
    request.type = type;
    request.payload = payload;
    request.bytes = bytes;
    return osaura_ipc_dispatch_opcode(
        osaura_hot_opcode(OSAURA_HOT_BANK_IPC, OSAURA_IPC_SEND), &request);
}

int osaura_ipc_receive(uint32_t receiver_task, uint32_t channel_id,
                       osaura_ipc_message *message) {
    osaura_ipc_request request = {0};
    request.actor_task = receiver_task;
    request.channel_id = channel_id;
    request.message = message;
    return osaura_ipc_dispatch_opcode(
        osaura_hot_opcode(OSAURA_HOT_BANK_IPC, OSAURA_IPC_RECEIVE), &request);
}

uint32_t osaura_ipc_pending(uint32_t channel_id) {
    osaura_ipc_request request = {0};
    request.channel_id = channel_id;
    return osaura_ipc_dispatch_opcode(
        osaura_hot_opcode(OSAURA_HOT_BANK_IPC, OSAURA_IPC_PENDING), &request) == 0
        ? request.value : 0u;
}

uint32_t osaura_ipc_channel_owner(uint32_t channel_id) {
    osaura_ipc_request request = {0};
    request.channel_id = channel_id;
    return osaura_ipc_dispatch_opcode(
        osaura_hot_opcode(OSAURA_HOT_BANK_IPC, OSAURA_IPC_OWNER), &request) == 0
        ? request.value : OSAURA_IPC_NONE;
}
