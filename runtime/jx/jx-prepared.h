#ifndef OSAURA_JX_PREPARED_H
#define OSAURA_JX_PREPARED_H

#include <stddef.h>
#include <stdint.h>

#define OSAURA_JXL_STACK_MAX 256u
#define OSAURA_JXL_LOCALS_MAX 256u
#define OSAURA_JXL_FRAMES_MAX 32u
#define OSAURA_JXL_DEFAULT_BUDGET 1000000ull

#define OSAURA_JX64B_CODE_PATH "CODE/program.jxl"

typedef enum {
    OSAURA_JXL_OK = 0,
    OSAURA_JXL_EINVAL = -1,
    OSAURA_JXL_ETRUNC = -2,
    OSAURA_JXL_EBYTE = -3,
    OSAURA_JXL_EOP = -4,
    OSAURA_JXL_ESTACK = -5,
    OSAURA_JXL_EFRAME = -6,
    OSAURA_JXL_EJUMP = -7,
    OSAURA_JXL_EDIV0 = -8,
    OSAURA_JXL_ESHIFT = -9,
    OSAURA_JXL_EBUDGET = -10,
    OSAURA_JXL_EBOOK = -11,
    OSAURA_JXL_ECRC = -12,
    OSAURA_JXL_ETRUST = -13
} osaura_jxl_status;

typedef struct {
    int64_t result;
    uint64_t instructions;
    uint32_t max_stack;
    uint32_t max_frames;
} osaura_jxl_result;

typedef int (*osaura_jx_book_trust_fn)(const uint8_t *book,
                                        size_t book_bytes,
                                        void *context);

typedef struct {
    const uint8_t *code;       /* borrowed from admitted Book bytes */
    size_t code_bytes;
    const uint8_t *manifest;   /* borrowed UTF-8 manifest bytes */
    size_t manifest_bytes;
    uint32_t section_count;
    uint16_t major;
    uint16_t minor;
    uint8_t structurally_valid;
    uint8_t trusted;
} osaura_jx64b_admission;

/* Execute prepared JXL. budget=0 selects OSAURA_JXL_DEFAULT_BUDGET. */
int osaura_jxl_run(const uint8_t *code,
                   size_t code_bytes,
                   uint64_t budget,
                   osaura_jxl_result *result);

/*
 * Structural admission validates the deterministic ZIP-STORE envelope,
 * JX64B001 header/version/count, entry CRCs, and CODE/program.jxl presence.
 * Cryptographic trust is deliberately separate: when require_trust != 0,
 * trust_fn must exist and approve the complete Book bytes.
 */
int osaura_jx64b_admit(const uint8_t *book,
                       size_t book_bytes,
                       int require_trust,
                       osaura_jx_book_trust_fn trust_fn,
                       void *trust_context,
                       osaura_jx64b_admission *out);

#endif
