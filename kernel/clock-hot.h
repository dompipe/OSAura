#ifndef OSAURA_CLOCK_HOT_H
#define OSAURA_CLOCK_HOT_H

#include <stdint.h>

/* Clock owns bank 8: 0xC0..0xC7. */
enum {
    OSAURA_CLOCK_HOT_NOW_TICKS = 0u,      /* 0xC0 */
    OSAURA_CLOCK_HOT_NOW_MS = 1u,         /* 0xC1 */
    OSAURA_CLOCK_HOT_DELTA = 2u,          /* 0xC2 */
    OSAURA_CLOCK_HOT_DEADLINE_AFTER = 3u, /* 0xC3 */
    OSAURA_CLOCK_HOT_DEADLINE_REACHED = 4u,/* 0xC4 */
    OSAURA_CLOCK_HOT_TICKS_TO_MS = 5u,    /* 0xC5 */
    OSAURA_CLOCK_HOT_MS_TO_TICKS = 6u,    /* 0xC6 */
    OSAURA_CLOCK_HOT_WAKE = 7u            /* 0xC7 */
};

typedef struct {
    uint64_t a;
    uint64_t b;
    uint64_t result;
} osaura_clock_request;

int osaura_clock_hot_bind(void);
uint64_t osaura_clock_now_ticks(void);
uint64_t osaura_clock_now_ms(void);
uint64_t osaura_clock_delta(uint64_t start, uint64_t end);
uint64_t osaura_clock_deadline_after(uint64_t base, uint64_t delta);
int osaura_clock_deadline_reached(uint64_t now, uint64_t deadline);
uint64_t osaura_clock_ticks_to_ms(uint64_t ticks);
uint64_t osaura_clock_ms_to_ticks(uint64_t ms);

#endif
