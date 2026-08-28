#include "memory-hot.h"
#include "hot-shadow.h"

#include <stdint.h>

static int raw_zero(void *dst, uint64_t bytes) {
    if (!dst && bytes) return 0;
    uint8_t *d = (uint8_t *)dst;
    while (bytes && ((uintptr_t)d & 7u)) { *d++ = 0u; --bytes; }
    uint64_t *q = (uint64_t *)(void *)d;
    while (bytes >= 32u) {
        q[0] = 0u; q[1] = 0u; q[2] = 0u; q[3] = 0u;
        q += 4; bytes -= 32u;
    }
    while (bytes >= 8u) { *q++ = 0u; bytes -= 8u; }
    d = (uint8_t *)(void *)q;
    while (bytes--) *d++ = 0u;
    return 1;
}

static int raw_zero64(uint64_t *dst, uint64_t words) {
    if (!dst && words) return 0;
    while (words >= 4u) {
        dst[0] = 0u; dst[1] = 0u; dst[2] = 0u; dst[3] = 0u;
        dst += 4; words -= 4u;
    }
    while (words--) *dst++ = 0u;
    return 1;
}

static int raw_copy(void *dst, const void *src, uint64_t bytes) {
    if ((!dst || !src) && bytes) return 0;
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if ((((uintptr_t)d | (uintptr_t)s) & 7u) == 0u) {
        uint64_t *dq = (uint64_t *)(void *)d;
        const uint64_t *sq = (const uint64_t *)(const void *)s;
        while (bytes >= 32u) {
            dq[0] = sq[0]; dq[1] = sq[1]; dq[2] = sq[2]; dq[3] = sq[3];
            dq += 4; sq += 4; bytes -= 32u;
        }
        while (bytes >= 8u) { *dq++ = *sq++; bytes -= 8u; }
        d = (uint8_t *)(void *)dq;
        s = (const uint8_t *)(const void *)sq;
    }
    while (bytes--) *d++ = *s++;
    return 1;
}

static int raw_copy64(uint64_t *dst, const uint64_t *src, uint64_t words) {
    if ((!dst || !src) && words) return 0;
    while (words >= 4u) {
        dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
        dst += 4; src += 4; words -= 4u;
    }
    while (words--) *dst++ = *src++;
    return 1;
}

static int raw_move(void *dst, const void *src, uint64_t bytes) {
    if ((!dst || !src) && bytes) return 0;
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d == s || !bytes) return 1;
    if (d < s || d >= s + bytes) return raw_copy(dst, src, bytes);
    d += bytes;
    s += bytes;
    while (bytes--) *--d = *--s;
    return 1;
}

static int raw_fill8(void *dst, uint8_t value, uint64_t bytes) {
    if (!dst && bytes) return 0;
    uint8_t *d = (uint8_t *)dst;
    while (bytes--) *d++ = value;
    return 1;
}

static int raw_compare(const void *a, const void *b, uint64_t bytes) {
    if ((!a || !b) && bytes) return (!a && !b) ? 0 : (!a ? -1 : 1);
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    while (bytes--) {
        if (*x != *y) return *x < *y ? -1 : 1;
        ++x; ++y;
    }
    return 0;
}

static int hot_zero(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    osaura_memory_request *q = (osaura_memory_request *)request;
    return raw_zero(q->dst, q->bytes);
}

static int hot_zero64(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    osaura_memory_request *q = (osaura_memory_request *)request;
    return raw_zero64((uint64_t *)q->dst, q->bytes);
}

static int hot_copy(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    osaura_memory_request *q = (osaura_memory_request *)request;
    return raw_copy(q->dst, q->src, q->bytes);
}

static int hot_copy64(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    osaura_memory_request *q = (osaura_memory_request *)request;
    return raw_copy64((uint64_t *)q->dst, (const uint64_t *)q->src, q->bytes);
}

static int hot_move(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    osaura_memory_request *q = (osaura_memory_request *)request;
    return raw_move(q->dst, q->src, q->bytes);
}

static int hot_fill8(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    osaura_memory_request *q = (osaura_memory_request *)request;
    return raw_fill8(q->dst, (uint8_t)q->value, q->bytes);
}

static int hot_compare(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    osaura_memory_request *q = (osaura_memory_request *)request;
    q->result = raw_compare(q->dst, q->src, q->bytes);
    return 1;
}

static int hot_equal(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    osaura_memory_request *q = (osaura_memory_request *)request;
    q->result = raw_compare(q->dst, q->src, q->bytes) == 0;
    return 1;
}

int osaura_memory_hot_bind(void) {
    int rc = 0;
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_MEMORY, OSAURA_MEMORY_HOT_ZERO, hot_zero, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_MEMORY, OSAURA_MEMORY_HOT_ZERO64, hot_zero64, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_MEMORY, OSAURA_MEMORY_HOT_COPY, hot_copy, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_MEMORY, OSAURA_MEMORY_HOT_COPY64, hot_copy64, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_MEMORY, OSAURA_MEMORY_HOT_MOVE, hot_move, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_MEMORY, OSAURA_MEMORY_HOT_FILL8, hot_fill8, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_MEMORY, OSAURA_MEMORY_HOT_COMPARE, hot_compare, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_MEMORY, OSAURA_MEMORY_HOT_EQUAL, hot_equal, 0);
    return rc == 0 ? 1 : 0;
}

int osaura_memory_zero(void *dst, uint64_t bytes) {
    osaura_memory_request q = { dst, 0, bytes, 0u, 0 };
    return osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_MEMORY, OSAURA_MEMORY_HOT_ZERO), &q) > 0;
}

int osaura_memory_zero64(uint64_t *dst, uint64_t words) {
    osaura_memory_request q = { dst, 0, words, 0u, 0 };
    return osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_MEMORY, OSAURA_MEMORY_HOT_ZERO64), &q) > 0;
}

int osaura_memory_copy(void *dst, const void *src, uint64_t bytes) {
    osaura_memory_request q = { dst, src, bytes, 0u, 0 };
    return osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_MEMORY, OSAURA_MEMORY_HOT_COPY), &q) > 0;
}

int osaura_memory_copy64(uint64_t *dst, const uint64_t *src, uint64_t words) {
    osaura_memory_request q = { dst, src, words, 0u, 0 };
    return osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_MEMORY, OSAURA_MEMORY_HOT_COPY64), &q) > 0;
}

int osaura_memory_move(void *dst, const void *src, uint64_t bytes) {
    osaura_memory_request q = { dst, src, bytes, 0u, 0 };
    return osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_MEMORY, OSAURA_MEMORY_HOT_MOVE), &q) > 0;
}

int osaura_memory_fill8(void *dst, uint8_t value, uint64_t bytes) {
    osaura_memory_request q = { dst, 0, bytes, value, 0 };
    return osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_MEMORY, OSAURA_MEMORY_HOT_FILL8), &q) > 0;
}

int osaura_memory_compare(const void *a, const void *b, uint64_t bytes) {
    osaura_memory_request q = { (void *)a, b, bytes, 0u, 0 };
    (void)osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_MEMORY, OSAURA_MEMORY_HOT_COMPARE), &q);
    return q.result;
}

int osaura_memory_equal(const void *a, const void *b, uint64_t bytes) {
    osaura_memory_request q = { (void *)a, b, bytes, 0u, 0 };
    (void)osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_MEMORY, OSAURA_MEMORY_HOT_EQUAL), &q);
    return q.result;
}
