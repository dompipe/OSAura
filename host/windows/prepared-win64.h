#ifndef OSAURA_WINDOWS_PREPARED64_H
#define OSAURA_WINDOWS_PREPARED64_H

#include "../../runtime/jx/jx-prepared.h"

#include <stdint.h>

typedef struct {
    osaura_jx64b_admission admission;
    osaura_jxl_result execution;
    uint32_t book_bytes;
} osaura_windows_prepared64_result;

/*
 * Read one .64B through VFS64, require BOOK_LOAD authority, structurally
 * admit it, and execute CODE/program.jxl under a bounded instruction budget.
 * allow_untrusted is for explicit development use only.
 */
int osaura_windows_prepared64_run_as(uint32_t subject,
                                     const char *jx_path,
                                     uint64_t budget,
                                     int allow_untrusted,
                                     osaura_jx_book_trust_fn trust_fn,
                                     void *trust_context,
                                     osaura_windows_prepared64_result *result);

#endif
