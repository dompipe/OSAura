#ifndef OSAURA_WINDOWS_RUNTIME64_H
#define OSAURA_WINDOWS_RUNTIME64_H

#include <stdint.h>
#include "../../kernel/ipc.h"
#include "../../kernel/scheduler.h"

#define OSAURA_WINDOWS_MEMORY64_MAX 32u
#define OSAURA_WINDOWS_MEMORY64_NONE UINT32_MAX

typedef struct {
    uint32_t id;
    uint32_t subject;
    uint64_t bytes;
    uint8_t used;
} osaura_windows_memory64_info;

typedef struct {
    uint32_t task_id;
    uint32_t subject;
    osaura_task_state state;
    osaura_task_role role;
    uint64_t ticks;
    uint64_t switches;
    const char *name;
} osaura_windows_task64_info;

int osaura_windows_clock64_init(void);
uint64_t osaura_windows_clock64_ticks(void);
uint64_t osaura_windows_clock64_ms(void);
uint64_t osaura_windows_clock64_ticks_to_ms(uint64_t ticks);
uint64_t osaura_windows_clock64_ms_to_ticks(uint64_t ms);

int osaura_windows_memory64_init(void);
int osaura_windows_memory64_alloc_as(uint32_t subject, uint64_t bytes, uint32_t *id_out);
int osaura_windows_memory64_free_as(uint32_t subject, uint32_t id);
int osaura_windows_memory64_info_as(uint32_t subject, uint32_t id,
                                    osaura_windows_memory64_info *info);
void *osaura_windows_memory64_map_as(uint32_t subject, uint32_t id);

int osaura_windows_task64_init(void);
uint32_t osaura_windows_task64_count(void);
int osaura_windows_task64_get_info(uint32_t task_id, osaura_windows_task64_info *info);
#define osaura_windows_task64_info(...) osaura_windows_task64_get_info(__VA_ARGS__)
int osaura_windows_task64_set_state_as(uint32_t subject, uint32_t task_id,
                                       osaura_task_state state);
int osaura_windows_job64_background_as(uint32_t subject, uint32_t task_id);
int osaura_windows_job64_foreground_as(uint32_t subject, uint32_t task_id);
uint32_t osaura_windows_job64_foreground(void);
uint32_t osaura_windows_job64_background_count(void);

int osaura_windows_ipc64_init(void);
int osaura_windows_ipc64_create(uint32_t owner_task, uint32_t *channel_id);
int osaura_windows_ipc64_close(uint32_t owner_task, uint32_t channel_id);
int osaura_windows_ipc64_send(uint32_t sender_task, uint32_t channel_id,
                              uint32_t type, const void *payload, uint32_t bytes);
int osaura_windows_ipc64_receive(uint32_t receiver_task, uint32_t channel_id,
                                 osaura_ipc_message *message);
uint32_t osaura_windows_ipc64_pending(uint32_t channel_id);

int osaura_windows_input64_init(void);
int osaura_windows_input64_is_console(void);
int osaura_windows_input64_poll(uint16_t *virtual_key, uint32_t *unicode_codepoint);

#endif
