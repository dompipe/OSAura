#include "jx-prepared-live.h"

#include <string.h>

void osaura_jx_live_book_init(osaura_jx_live_book *live) {
    if (!live) return;
    memset(live, 0, sizeof *live);
    live->next_generation = 1u;
}

int osaura_jx_live_book_stage(osaura_jx_live_book *live,
                              const uint8_t *book,
                              size_t book_bytes,
                              int require_trust,
                              osaura_jx_book_trust_fn trust_fn,
                              void *trust_context) {
    osaura_jx64b_admission candidate;
    int rc;
    if (!live) return OSAURA_JXL_EINVAL;
    rc = osaura_jx64b_admit(book, book_bytes, require_trust,
                            trust_fn, trust_context, &candidate);
    if (rc != OSAURA_JXL_OK) return rc;
    live->candidate = candidate;
    live->has_candidate = 1u;
    return OSAURA_JXL_OK;
}

int osaura_jx_live_book_activate(osaura_jx_live_book *live,
                                 osaura_jx_live_migrate_fn migrate,
                                 void *context) {
    int rc;
    if (!live || !live->has_candidate) return OSAURA_JXL_EINVAL;

    /* JX meaning gets a chance to migrate while old state is authoritative. */
    if (migrate) {
        rc = migrate(live->has_active ? &live->active : 0,
                     &live->candidate, context);
        if (rc != 0) return rc;
    }

    if (live->has_active) {
        live->previous = live->active;
        live->previous_generation = live->active_generation;
        live->has_previous = 1u;
    } else {
        memset(&live->previous, 0, sizeof live->previous);
        live->previous_generation = 0u;
        live->has_previous = 0u;
    }

    live->active = live->candidate;
    live->active_generation = live->next_generation++;
    memset(&live->candidate, 0, sizeof live->candidate);
    live->has_candidate = 0u;
    return OSAURA_JXL_OK;
}

int osaura_jx_live_book_rollback(osaura_jx_live_book *live) {
    osaura_jx64b_admission current;
    uint64_t current_generation;
    if (!live || !live->has_previous || !live->has_active)
        return OSAURA_JXL_EINVAL;

    /* Keep the displaced generation as previous, allowing a bounded flip-back. */
    current = live->active;
    current_generation = live->active_generation;
    live->active = live->previous;
    live->active_generation = live->previous_generation;
    live->previous = current;
    live->previous_generation = current_generation;
    return OSAURA_JXL_OK;
}

const osaura_jx64b_admission *osaura_jx_live_book_active(const osaura_jx_live_book *live) {
    return (live && live->has_active) ? &live->active : 0;
}
