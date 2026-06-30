#include "kernel/user_vm.h"

#include <stdint.h>

#include "kernel/mm/pmm.h"
#include "kernel/mm/vmm.h"

/*
 * User virtual-memory mapping helpers.
 *
 * The syscall layer uses this file for anonymous mmap/munmap. The panel
 * loader also uses user_vm_map_physical for fixed image and stack regions
 * that were registered on the process before mapping. Anonymous mappings are
 * process-owned and must carry PROCESS_USER_REGION_OWNED_PAGES so
 * process_release can return their PMM pages. Page-table installation is
 * delegated to VMM, whose range mapping rolls back partial leaf mappings before
 * returning failure.
 */

#define USER_VM_SUPPORTED_FLAGS \
    (USER_VM_PROT_READ | USER_VM_PROT_WRITE | USER_VM_PROT_EXEC)

static uint64_t page_align_up(uint64_t value) {
    if (value == 0 || value > UINT64_MAX - (PAGE_SIZE - 1ULL)) {
        return 0;
    }

    return (value + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
}

static int flags_to_vmm(uint64_t flags, uint64_t *vmm_flags) {
    uint64_t result = VMM_FLAG_USER;

    if (vmm_flags == 0 || (flags & ~USER_VM_SUPPORTED_FLAGS) != 0) {
        return -1;
    }

    if (flags == 0) {
        flags = USER_VM_PROT_READ | USER_VM_PROT_WRITE;
    }

    /* AArch64 stage-1 page permissions do not provide a write-only user page. */
    if ((flags & USER_VM_PROT_WRITE) != 0) {
        result |= VMM_FLAG_READ | VMM_FLAG_WRITE;
    } else if ((flags & USER_VM_PROT_READ) != 0) {
        result |= VMM_FLAG_READ;
    }

    if ((flags & USER_VM_PROT_EXEC) != 0) {
        result |= VMM_FLAG_EXEC;
    }

    *vmm_flags = result;
    return 0;
}

static void free_page_range(uint64_t paddr, uint64_t size) {
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        pmm_free_page(paddr + offset);
    }
}

int64_t user_vm_map_anonymous(process_t *process, uint64_t hint,
                              uint64_t size, uint64_t flags) {
    uint64_t addr;
    uint64_t aligned_size = page_align_up(size);
    uint64_t paddr;
    uint64_t vmm_flags;
    uint64_t page_count;

    if (process == 0 || process->page_table == 0 || hint != 0 ||
        aligned_size == 0 || flags_to_vmm(flags, &vmm_flags) != 0) {
        return USER_VM_ERR_INVAL;
    }

    if (process_alloc_user_region(process, aligned_size, &addr) != 0) {
        return USER_VM_ERR_INVAL;
    }

    page_count = aligned_size / PAGE_SIZE;
    paddr = pmm_alloc_pages(page_count);
    if (paddr == 0) {
        (void)process_remove_user_region(process, addr, aligned_size);
        return USER_VM_ERR_NOMEM;
    }

    /* Zero the pages to prevent information leaks between processes. */
    {
        uint8_t *p = (uint8_t *)(uintptr_t)paddr;
        for (uint64_t i = 0; i < aligned_size; i++) {
            p[i] = 0;
        }
    }

    if (vmm_map_range(process->page_table, addr, paddr, aligned_size,
                      vmm_flags) != 0) {
        (void)vmm_unmap_range(process->page_table, addr, aligned_size);
        (void)process_remove_user_region(process, addr, aligned_size);
        free_page_range(paddr, aligned_size);
        return USER_VM_ERR_NOMEM;
    }

    if (process_set_user_region_mapping(process, addr, aligned_size, paddr,
                                        PROCESS_USER_REGION_OWNED_PAGES) != 0) {
        (void)vmm_unmap_range(process->page_table, addr, aligned_size);
        (void)process_remove_user_region(process, addr, aligned_size);
        free_page_range(paddr, aligned_size);
        return USER_VM_ERR_INVAL;
    }

    return (int64_t)addr;
}

int64_t user_vm_unmap_anonymous(process_t *process, uint64_t addr,
                                uint64_t size) {
    uint64_t aligned_size = page_align_up(size);
    process_user_region_t region;

    if (process == 0 || process->page_table == 0 || aligned_size == 0 ||
        process_find_user_region(process, addr, aligned_size, &region) != 0 ||
        (region.flags & PROCESS_USER_REGION_OWNED_PAGES) == 0 ||
        region.paddr == 0) {
        return USER_VM_ERR_INVAL;
    }

    if (vmm_unmap_range(process->page_table, addr, aligned_size) != 0) {
        return USER_VM_ERR_INVAL;
    }

    if (process_remove_user_region(process, addr, aligned_size) != 0) {
        return USER_VM_ERR_INVAL;
    }

    free_page_range(region.paddr, aligned_size);

    return 0;
}

int user_vm_map_physical(process_t *process, uint64_t vaddr, uint64_t paddr,
                         uint64_t size, uint64_t flags) {
    uint64_t vmm_flags;

    if (process == 0 || process->page_table == 0 || vaddr == 0 ||
        paddr == 0 || size == 0 || flags_to_vmm(flags, &vmm_flags) != 0 ||
        process_find_user_region(process, vaddr, size, 0) != 0) {
        return -1;
    }

    if (vmm_map_range(process->page_table, vaddr, paddr, size,
                      vmm_flags) != 0) {
        (void)vmm_unmap_range(process->page_table, vaddr, size);
        return -1;
    }

    if (process_set_user_region_mapping(process, vaddr, size, paddr, 0) != 0) {
        (void)vmm_unmap_range(process->page_table, vaddr, size);
        return -1;
    }

    return 0;
}
