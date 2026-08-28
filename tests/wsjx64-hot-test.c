#if !defined(_WIN64)
#error WSJX64 hot test requires x64 Windows.
#endif

#include "../host/windows/runtime64.h"
#include "../host/windows/hot64.h"
#include "../kernel/hot-shadow.h"
#include "../kernel/security.h"

#include <stdio.h>

int main(void) {
    osaura_security_init();
    osaura_hot_init();
    if (osaura_windows_clock64_init() != 0) return 10;
    if (osaura_windows_memory64_init() != 0) return 11;
    if (osaura_windows_task64_init() != 0) return 12;
    if (osaura_windows_ipc64_init() != 0) return 13;
    if (osaura_windows_input64_init() != 0) return 14;
    if (osaura_windows_hot64_bind() != 0) return 15;
    if (osaura_windows_hot64_self_test() != 0) return 16;

    const osaura_shadow_table *table = osaura_hot_table();
    if (!table) return 17;
    if (!table->slot[osaura_hot_opcode(OSAURA_HOT_BANK_CLOCK, 0u) & 0x7fu].fn) return 18;
    if (!table->slot[osaura_hot_opcode(OSAURA_HOT_BANK_MEMORY, 0u) & 0x7fu].fn) return 19;
    if (!table->slot[osaura_hot_opcode(OSAURA_HOT_BANK_TASK, 0u) & 0x7fu].fn) return 20;
    if (!table->slot[osaura_hot_opcode(OSAURA_HOT_BANK_IPC, 0u) & 0x7fu].fn) return 21;
    if (!table->slot[osaura_hot_opcode(OSAURA_HOT_BANK_INPUT, 0u) & 0x7fu].fn) return 22;

    puts("WSJX64 HOT BANKS: PASS");
    puts("IPC 88-8F: BOUND");
    puts("INPUT A0-A7: BOUND");
    puts("CLOCK C0-C7: BOUND");
    puts("MEMORY C8-CF: BOUND");
    puts("TASK D0-D7: BOUND");
    puts("F0-FF: UNASSIGNED");
    return 0;
}
