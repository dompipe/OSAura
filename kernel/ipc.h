#ifndef OSAURA_IPC_H
#define OSAURA_IPC_H

#include <stdint.h>

#define OSAURA_IPC_CHANNEL_MAX 32u
#define OSAURA_IPC_QUEUE_DEPTH 16u
#define OSAURA_IPC_PAYLOAD_MAX 64u
#define OSAURA_IPC_NONE UINT32_MAX

typedef struct {
    uint32_t type;
    uint32_t sender_task;
    uint32_t bytes;
    uint8_t payload[OSAURA_IPC_PAYLOAD_MAX];
} osaura_ipc_message;

void osaura_ipc_init(void);
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
