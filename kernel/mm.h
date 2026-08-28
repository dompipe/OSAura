#ifndef OSAURA_MM_H
#define OSAURA_MM_H

#include <stdint.h>

#include "boot-info.h"

/*
 * Early OSAura virtual-memory contract.
 *
 * Generation 1 deliberately keeps a direct identity map. The kernel owns the
 * page-table root immediately, while higher-half/user address-space policy is
 * introduced later without changing physical-frame ownership semantics.
 */
#define OSAURA_VM_DIRECT_GIB 64ull
#define OSAURA_VM_DIRECT_LIMIT (OSAURA_VM_DIRECT_GIB << 30)
#define OSAURA_VM_PAGE_2M (2ull << 20)

int osaura_vm_init(const osaura_boot_info *boot);
uint64_t osaura_vm_cr3(void);
uint64_t osaura_vm_direct_limit(void);
int osaura_vm_contains_phys(uint64_t address, uint64_t size);

#endif
