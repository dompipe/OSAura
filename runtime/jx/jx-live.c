#include "jx-runtime.h"
#include "security.h"

#include <stdint.h>

#define JX64_MAX_BOOK_BYTES (64ull << 20)

static const void *g_candidate_book;
static uint64_t g_candidate_book_size;
static volatile uint8_t g_candidate_queued;
static volatile uint64_t g_live_book_activations;

int osaura_jx_runtime_queue_book_as(uint32_t subject, const void *bytes, uint64_t size) {
    if (subject != 0u && !osaura_security_check(subject, OSAURA_CAP_BOOK_LOAD)) return -2;
    if (!bytes || !size || size > JX64_MAX_BOOK_BYTES ||
        g_candidate_queued || osaura_jx_runtime_active())
        return -1;

    g_candidate_book = bytes;
    g_candidate_book_size = size;
    g_candidate_queued = 1u;
    return 0;
}

int osaura_jx_runtime_queue_book(const void *bytes, uint64_t size) {
    return osaura_jx_runtime_queue_book_as(0u, bytes, size);
}

int osaura_jx_runtime_candidate_queued(void) {
    return g_candidate_queued != 0u;
}

const void *osaura_jx_runtime_candidate_bytes(void) {
    return g_candidate_book;
}

uint64_t osaura_jx_runtime_candidate_size(void) {
    return g_candidate_book_size;
}

void osaura_jx_runtime_candidate_consumed(void) {
    g_candidate_book = 0;
    g_candidate_book_size = 0;
    g_candidate_queued = 0u;
    ++g_live_book_activations;
}

uint64_t osaura_jx_runtime_live_book_activations(void) {
    return g_live_book_activations;
}
