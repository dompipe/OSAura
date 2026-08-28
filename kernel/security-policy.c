#include "security-policy.h"

#include "e1000.h"
#include "scheduler.h"
#include "security.h"
#include "storage.h"
#include "usb-hot.h"
#include "vfs.h"
#include "wifi-hot.h"
#include "jx-runtime.h"

#include <stdint.h>

int osaura_security_policy_self_test(void) {
    const uint32_t jx = OSAURA_SECURITY_JX_SUBJECT;
    const uint32_t untrusted = 2u;

    const uint64_t jx_expected =
        OSAURA_CAP_STORAGE_READ |
        OSAURA_CAP_NETWORK |
        OSAURA_CAP_USB |
        OSAURA_CAP_WIFI |
        OSAURA_CAP_VFS_READ |
        OSAURA_CAP_BOOK_LOAD;
    const uint64_t jx_forbidden =
        OSAURA_CAP_STORAGE_WRITE |
        OSAURA_CAP_TASK_CONTROL |
        OSAURA_CAP_VFS_WRITE |
        OSAURA_CAP_ADMIN;

    if (!osaura_security_check(OSAURA_SECURITY_KERNEL_SUBJECT, OSAURA_CAP_ALL)) return 0;
    if (!osaura_security_check(jx, jx_expected)) return 0;
    if (osaura_security_check(jx, jx_forbidden)) return 0;

    /* A non-admin subject may not promote itself. */
    if (osaura_security_grant_as(jx, jx, OSAURA_CAP_TASK_CONTROL) == 0) return 0;
    if (osaura_security_check(jx, OSAURA_CAP_TASK_CONTROL)) return 0;

    /* Denials occur before mutation or hardware/device lookup. */
    if (osaura_scheduler_set_task_state_as(jx, 1u, OSAURA_TASK_STOPPED) == 0) return 0;
    if (osaura_scheduler_task_state(1u) != OSAURA_TASK_RUNNABLE) return 0;

    if (osaura_block_write_as(jx, 0u, 0u, 1u, 0) != -9) return 0;

    uint32_t handle = OSAURA_VFS_HANDLE_NONE;
    if (osaura_vfs_open_device_as(jx, 0u,
                                  OSAURA_VFS_OPEN_READ | OSAURA_VFS_OPEN_WRITE,
                                  &handle) != -10) return 0;
    if (handle != OSAURA_VFS_HANDLE_NONE) return 0;

    if (osaura_usb_hot_reinit_as(untrusted) != -2) return 0;
    if (osaura_wifi_hot_connect_as(untrusted, 0u, 0, 0u) != -2) return 0;
    if (osaura_e1000_transmit_as(untrusted, 0, 0u) != -2) return 0;
    if (osaura_jx_runtime_queue_book_as(untrusted, (const void *)(uintptr_t)1u, 1u) != -2) return 0;

    return 1;
}
