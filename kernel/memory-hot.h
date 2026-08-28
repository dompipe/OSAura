#ifndef OSAURA_MEMORY_HOT_H
#define OSAURA_MEMORY_HOT_H

#include <stdint.h>

/* Memory primitives own bank 9: 0xC8..0xCF. */
enum {
    OSAURA_MEMORY_HOT_ZERO = 0u,      /* 0xC8 */
    OSAURA_MEMORY_HOT_ZERO64 = 1u,    /* 0xC9 */
    OSAURA_MEMORY_HOT_COPY = 2u,      /* 0xCA */
    OSAURA_MEMORY_HOT_COPY64 = 3u,    /* 0xCB */
    OSAURA_MEMORY_HOT_MOVE = 4u,      /* 0xCC */
    OSAURA_MEMORY_HOT_FILL8 = 5u,     /* 0xCD */
    OSAURA_MEMORY_HOT_COMPARE = 6u,   /* 0xCE */
    OSAURA_MEMORY_HOT_EQUAL = 7u      /* 0xCF */
};

typedef struct {
    void *dst;
    const void *src;
    uint64_t bytes;
    uint64_t value;
    int result;
} osaura_memory_request;

int osaura_memory_hot_bind(void);
int osaura_memory_zero(void *dst, uint64_t bytes);
int osaura_memory_zero64(uint64_t *dst, uint64_t words);
int osaura_memory_copy(void *dst, const void *src, uint64_t bytes);
int osaura_memory_copy64(uint64_t *dst, const uint64_t *src, uint64_t words);
int osaura_memory_move(void *dst, const void *src, uint64_t bytes);
int osaura_memory_fill8(void *dst, uint8_t value, uint64_t bytes);
int osaura_memory_compare(const void *a, const void *b, uint64_t bytes);
int osaura_memory_equal(const void *a, const void *b, uint64_t bytes);

#endif
