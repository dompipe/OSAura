#ifndef OSAURA_IPC_H
#define OSAURA_IPC_H

#include <stdint.h>
#include "hot-shadow.h"

#define OSAURA_IPC_CHANNEL_MAX 32u
#define OSAURA_IPC_QUEUE_DEPTH 16u
#define OSAURA_IPC_PAYLOAD_MAX 64u
#define OSAURA_IPC_NONE UINT32_MAX

/* 3-bit IPC hot ABI: exactly eight shadows. */
typedef enum {
    OSAURA_IPC_SEND       = 0u,
    OSAURA_IPC_RECEIVE    = 1u,
    OSAURA_IPC_PENDING    = 2u,
    OSAURA_IPC_OWNER      = 3u,
    OSAURA_IPC_CREATE     = 4u,
    OSAURA_IPC_CLOSE      = 5u,
    OSAURA_IPC_SENDRECV   = 6u,
    OSAURA_IPC_POLL       = 7u
} osaura_ipc_shadow;

typedef struct {
    uint32_t type;
    uint32_t sender_task;
    uint32_t bytes;
    uint8_t payload[OSAURA_IPC_PAYLOAD_MAX];
} osaura_ipc_message;

typedef struct {
    uint32_t actor_task;
    uint32_t channel_id;
    uint32_t type;
    const void *payload;
    uint32_t bytes;
    osaura_ipc_message *message;
    uint32_t value;
} osaura_ipc_request;

void osaura_ipc_init(void);
int osaura_ipc_dispatch(uint8_t selector, osaura_ipc_request *request);
const osaura_shadow_table *osaura_ipc_shadows(void);
int osaura_ipc_channel_create(uint32_t owner_task, uint32_t *channel_id);
int osaura_ipc_channel_close(uint32_t owner_task, uint32_t channel_id);
int osaura_ipc_send(uint32_t sender_task,
                    uint32_t channel_id,
                    uint32_t type,
                    const void *payload,
                    uint32_t bytes);
int osaura_ipc_receive(uint32_t receiver_task,
                       uint32_t channel_id,
                       osaura_ipc_message *message);
uint32_t osaura_ipc_pending(uint32_t channel_id);
uint32_t osaura_ipc_channel_owner(uint32_t channel_id);

#endif
