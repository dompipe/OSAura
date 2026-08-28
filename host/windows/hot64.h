#ifndef OSAURA_WINDOWS_HOT64_H
#define OSAURA_WINDOWS_HOT64_H

#include <stdint.h>
#include "../../kernel/clock-hot.h"
#include "../../kernel/memory-hot.h"
#include "../../kernel/task-hot.h"
#include "../../kernel/ipc.h"
#include "../../kernel/scheduler.h"

#define OSAURA_WINDOWS_INPUT64_HOT_POLL 0u
#define OSAURA_WINDOWS_INPUT64_HOT_POP 1u
#define OSAURA_WINDOWS_INPUT64_HOT_PUSH 2u
#define OSAURA_WINDOWS_INPUT64_HOT_PS2 3u
#define OSAURA_WINDOWS_INPUT64_HOT_USB 4u
#define OSAURA_WINDOWS_INPUT64_HOT_MODIFIERS 5u
#define OSAURA_WINDOWS_INPUT64_HOT_READY 6u
#define OSAURA_WINDOWS_INPUT64_HOT_WAKE 7u

typedef struct {
    uint16_t virtual_key;
    uint32_t codepoint;
    uint32_t value;
} osaura_windows_input64_hot_request;

int osaura_windows_hot64_bind(void);
int osaura_windows_hot64_self_test(void);

#endif
