#ifndef OSAURA_TASK_HOT_H
#define OSAURA_TASK_HOT_H

#include <stdint.h>
#include "scheduler.h"

enum {
    OSAURA_TASK_HOT_CURRENT  = 0u, /* D0 */
    OSAURA_TASK_HOT_COUNT    = 1u, /* D1 */
    OSAURA_TASK_HOT_TICKS    = 2u, /* D2 */
    OSAURA_TASK_HOT_SWITCHES = 3u, /* D3 */
    OSAURA_TASK_HOT_STATE    = 4u, /* D4 */
    OSAURA_TASK_HOT_ROLE     = 5u, /* D5 */
    OSAURA_TASK_HOT_RUNNING  = 6u, /* D6 */
    OSAURA_TASK_HOT_NAME     = 7u  /* D7 */
};

typedef struct {
    uint32_t task_id;
    uint32_t value32;
    uint64_t value64;
    const char *text;
} osaura_task_hot_request;

int osaura_task_hot_bind(void);

#endif
