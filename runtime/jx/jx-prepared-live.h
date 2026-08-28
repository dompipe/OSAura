#ifndef OSAURA_JX_PREPARED_LIVE_H
#define OSAURA_JX_PREPARED_LIVE_H

#include "jx-prepared.h"

#include <stddef.h>
#include <stdint.h>

typedef int (*osaura_jx_live_migrate_fn)(const osaura_jx64b_admission *from,
                                         const osaura_jx64b_admission *to,
                                         void *context);

typedef struct {
    osaura_jx64b_admission active;
    osaura_jx64b_admission previous;
    osaura_jx64b_admission candidate;
    uint64_t active_generation;
    uint64_t previous_generation;
    uint64_t next_generation;
    uint8_t has_active;
    uint8_t has_previous;
    uint8_t has_candidate;
} osaura_jx_live_book;

void osaura_jx_live_book_init(osaura_jx_live_book *live);

/* Candidate and active admissions borrow their original Book storage. */
int osaura_jx_live_book_stage(osaura_jx_live_book *live,
                              const uint8_t *book,
                              size_t book_bytes,
                              int require_trust,
                              osaura_jx_book_trust_fn trust_fn,
                              void *trust_context);

/* Migration runs before the authoritative generation changes. */
int osaura_jx_live_book_activate(osaura_jx_live_book *live,
                                 osaura_jx_live_migrate_fn migrate,
                                 void *context);

int osaura_jx_live_book_rollback(osaura_jx_live_book *live);
const osaura_jx64b_admission *osaura_jx_live_book_active(const osaura_jx_live_book *live);

#endif
