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

#define COM1 0x3f8u

static inline uint8_t policy_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void policy_out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void policy_serial_char(char c) {
    uint32_t spin = 100000u;
    while (!(policy_in8(COM1 + 5u) & 0x20u) && spin--) __asm__ volatile("pause");
    policy_out8(COM1, (uint8_t)c);
}

static void policy_serial_text(const char *s) {
    while (s && *s) policy_serial_char(*s++);
}

static void policy_serial_u32(uint32_t value) {
    char digits[10];
    uint32_t count = 0u;
    if (!value) {
        policy_serial_char('0');
        return;
    }
    while (value && count < (uint32_t)sizeof digits) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count) policy_serial_char(digits[--count]);
}

static int policy_fail(uint32_t stage) {
    policy_serial_text("SECURITY POLICY: FAIL ");
    policy_serial_u32(stage);
    policy_serial_char('\n');
    return 0;
}

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
        OSAURA_CAP_WIFI_CREDENTIAL |
        OSAURA_CAP_ADMIN;

    if (!osaura_security_check(OSAURA_SECURITY_KERNEL_SUBJECT, OSAURA_CAP_ALL)) return policy_fail(1u);
    if (!osaura_security_check(jx, jx_expected)) return policy_fail(2u);
    if ((osaura_security_snapshot(jx) & jx_forbidden) != 0u) return policy_fail(3u);

    /* A non-admin subject may not promote itself. */
    if (osaura_security_grant_as(jx, jx, OSAURA_CAP_TASK_CONTROL) == 0) return policy_fail(4u);
    if (osaura_security_check(jx, OSAURA_CAP_TASK_CONTROL)) return policy_fail(5u);

    /* Denials occur before mutation or hardware/device lookup. */
    if (osaura_scheduler_set_task_state_as(jx, 1u, OSAURA_TASK_STOPPED) == 0) return policy_fail(6u);
    if (osaura_scheduler_task_state(1u) != OSAURA_TASK_RUNNABLE) return policy_fail(7u);

    if (osaura_block_write_as(jx, 0u, 0u, 1u, 0) != -9) return policy_fail(8u);

    uint32_t handle = OSAURA_VFS_HANDLE_NONE;
    if (osaura_vfs_open_device_as(jx, 0u,
                                  OSAURA_VFS_OPEN_READ | OSAURA_VFS_OPEN_WRITE,
                                  &handle) != -10) return policy_fail(9u);
    if (handle != OSAURA_VFS_HANDLE_NONE) return policy_fail(10u);

    if (osaura_usb_hot_reinit_as(untrusted) != -2) return policy_fail(11u);
    if (osaura_wifi_hot_connect_as(untrusted, 0u, 0, 0u) != -2) return policy_fail(12u);
    if (osaura_e1000_transmit_as(untrusted, 0, 0u) != -2) return policy_fail(13u);
    if (osaura_jx_runtime_queue_book_as(untrusted, (const void *)(uintptr_t)1u, 1u) != -2) return policy_fail(14u);

    /* General Wi-Fi use does not grant access to stored secret material. */
    osaura_wifi_credential credential = {0};
    if (osaura_wifi_hot_credentials_find_as(jx, "self-test", &credential) != -2) return policy_fail(15u);
    if (osaura_wifi_hot_credentials_save_as(jx, &credential) != -2) return policy_fail(16u);

    policy_serial_text("SECURITY POLICY: PASS\n");
    return 1;
}
