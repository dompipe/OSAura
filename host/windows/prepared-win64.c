#ifdef _WIN32

#include "prepared-win64.h"
#include "runtime64.h"
#include "vfs64.h"
#include "../../kernel/security.h"

#include <limits.h>
#include <string.h>

int osaura_windows_prepared64_run_as(uint32_t subject,
                                     const char *jx_path,
                                     uint64_t budget,
                                     int allow_untrusted,
                                     osaura_jx_book_trust_fn trust_fn,
                                     void *trust_context,
                                     osaura_windows_prepared64_result *result) {
    uint32_t handle = OSAURA_WINDOWS_VFS64_NONE;
    uint32_t allocation = OSAURA_WINDOWS_MEMORY64_NONE;
    uint32_t loaded = 0u;
    osaura_windows_vfs64_info info;
    osaura_jx64b_admission admission;
    uint8_t *book;
    int rc;

    if (!jx_path || !result) return OSAURA_JXL_EINVAL;
    memset(result, 0, sizeof *result);
    if ((osaura_security_snapshot(subject) & OSAURA_CAP_BOOK_LOAD) == 0u)
        return OSAURA_JXL_ETRUST;

    rc = osaura_windows_vfs64_open_as(subject, jx_path,
                                      OSAURA_WINDOWS_VFS64_READ, &handle);
    if (rc != 0) return rc;
    memset(&info, 0, sizeof info);
    rc = osaura_windows_vfs64_stat_as(subject, handle, &info);
    {
        int close_rc = osaura_windows_vfs64_close_as(subject, handle);
        if (rc == 0 && close_rc != 0) rc = close_rc;
    }
    if (rc != 0) return rc;
    if (info.size == 0u || info.size > UINT32_MAX) return OSAURA_JXL_EBOOK;

    rc = osaura_windows_memory64_alloc_as(subject, info.size, &allocation);
    if (rc != 0) return rc;
    book = (uint8_t *)osaura_windows_memory64_map_as(subject, allocation);
    if (!book) {
        (void)osaura_windows_memory64_free_as(subject, allocation);
        return OSAURA_JXL_EINVAL;
    }

    rc = osaura_windows_vfs64_load_as(subject, jx_path, book,
                                      (uint32_t)info.size, &loaded);
    if (rc == 0 && loaded != (uint32_t)info.size) rc = OSAURA_JXL_ETRUNC;
    if (rc == 0)
        rc = osaura_jx64b_admit(book, loaded, allow_untrusted ? 0 : 1,
                                trust_fn, trust_context, &admission);
    if (rc == 0) {
        result->admission = admission;
        result->book_bytes = loaded;
        rc = osaura_jxl_run(admission.code, admission.code_bytes,
                            budget, &result->execution);
    }

    {
        int free_rc = osaura_windows_memory64_free_as(subject, allocation);
        if (rc == 0 && free_rc != 0) rc = free_rc;
    }
    /* Borrowed admission pointers die with the temporary Book allocation. */
    result->admission.code = 0;
    result->admission.code_bytes = 0u;
    result->admission.manifest = 0;
    result->admission.manifest_bytes = 0u;
    return rc;
}

#endif
