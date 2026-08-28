#include "mm.h"

#include <stddef.h>
#include <stdint.h>

#define PT_ENTRIES 512u
#define VM_PD_COUNT ((uint32_t)OSAURA_VM_DIRECT_GIB)

#define PTE_PRESENT (1ull << 0)
#define PTE_WRITE   (1ull << 1)
#define PDE_LARGE   (1ull << 7)
#define ENTRY_ADDR_MASK 0x000ffffffffff000ull

extern uint64_t osaura_arch_read_cr3(void);
extern void osaura_arch_write_cr3(uint64_t root);

/*
 * One PML4 page + one PDPT page + 64 page-directory pages. Each page-directory
 * entry maps one 2 MiB frame, producing a simple 64 GiB direct map.
 *
 * These arrays belong to the loaded OSAura image, so the firmware memory map
 * keeps them out of EfiConventionalMemory. The physical-frame allocator must
 * therefore never hand them out as free RAM.
 */
static uint64_t g_pml4[PT_ENTRIES] __attribute__((aligned(4096)));
static uint64_t g_pdpt[PT_ENTRIES] __attribute__((aligned(4096)));
static uint64_t g_pd[VM_PD_COUNT][PT_ENTRIES] __attribute__((aligned(4096)));
static uint64_t g_owned_cr3;

static void zero_entries(uint64_t *entries, size_t count) {
    for (size_t i = 0; i < count; ++i) entries[i] = 0;
}

int osaura_vm_contains_phys(uint64_t address, uint64_t size) {
    if (address >= OSAURA_VM_DIRECT_LIMIT) return 0;
    if (size > OSAURA_VM_DIRECT_LIMIT - address) return 0;
    return 1;
}

static int critical_addresses_fit(const osaura_boot_info *boot) {
    uintptr_t stack_marker = (uintptr_t)&boot;
    uintptr_t code = (uintptr_t)&osaura_vm_init;
    uintptr_t root = (uintptr_t)g_pml4;
    uintptr_t pdpt = (uintptr_t)g_pdpt;
    uintptr_t pds = (uintptr_t)g_pd;

    if (!osaura_vm_contains_phys((uint64_t)stack_marker, sizeof stack_marker)) return 0;
    if (!osaura_vm_contains_phys((uint64_t)code, 1u)) return 0;
    if (!osaura_vm_contains_phys((uint64_t)root, sizeof g_pml4)) return 0;
    if (!osaura_vm_contains_phys((uint64_t)pdpt, sizeof g_pdpt)) return 0;
    if (!osaura_vm_contains_phys((uint64_t)pds, sizeof g_pd)) return 0;

    if (boot) {
        if (!osaura_vm_contains_phys(boot->framebuffer_base, boot->framebuffer_size)) return 0;
        if (!osaura_vm_contains_phys(boot->memory_map, boot->memory_map_size)) return 0;
        if (!boot->jx_book || !boot->jx_book_size) return 0;
        if (!osaura_vm_contains_phys(boot->jx_book, boot->jx_book_size)) return 0;
    }
    return 1;
}

int osaura_vm_init(const osaura_boot_info *boot) {
    if (!boot || !critical_addresses_fit(boot)) return 0;

    zero_entries(g_pml4, PT_ENTRIES);
    zero_entries(g_pdpt, PT_ENTRIES);
    zero_entries(&g_pd[0][0], (size_t)VM_PD_COUNT * PT_ENTRIES);

    for (uint32_t gib = 0; gib < VM_PD_COUNT; ++gib) {
        uint64_t pd_addr = ((uint64_t)(uintptr_t)&g_pd[gib][0]) & ENTRY_ADDR_MASK;
        g_pdpt[gib] = pd_addr | PTE_PRESENT | PTE_WRITE;

        uint64_t gib_base = ((uint64_t)gib) << 30;
        for (uint32_t entry = 0; entry < PT_ENTRIES; ++entry) {
            uint64_t physical = gib_base + ((uint64_t)entry * OSAURA_VM_PAGE_2M);
            g_pd[gib][entry] = physical | PTE_PRESENT | PTE_WRITE | PDE_LARGE;
        }
    }

    uint64_t pdpt_addr = ((uint64_t)(uintptr_t)g_pdpt) & ENTRY_ADDR_MASK;
    g_pml4[0] = pdpt_addr | PTE_PRESENT | PTE_WRITE;

    uint64_t root = ((uint64_t)(uintptr_t)g_pml4) & ENTRY_ADDR_MASK;
    osaura_arch_write_cr3(root);
    g_owned_cr3 = osaura_arch_read_cr3() & ENTRY_ADDR_MASK;

    return g_owned_cr3 == root;
}

uint64_t osaura_vm_cr3(void) {
    return g_owned_cr3;
}

uint64_t osaura_vm_direct_limit(void) {
    return OSAURA_VM_DIRECT_LIMIT;
}
