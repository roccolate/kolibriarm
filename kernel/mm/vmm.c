#include "kernel/mm/vmm.h"

#include <stdint.h>

#include "kernel/mm/pmm.h"

#define TABLE_ENTRIES       512ULL
#define TABLE_INDEX_MASK    0x1ffULL
#define DESC_VALID          (1ULL << 0)
#define DESC_TABLE          (1ULL << 1)
#define DESC_PAGE           (1ULL << 1)
#define DESC_AF             (1ULL << 10)
#define DESC_SH_INNER       (3ULL << 8)
#define DESC_AP_RO          (1ULL << 7)
#define DESC_UXN            (1ULL << 54)
#define DESC_PXN            (1ULL << 53)
#define DESC_ATTR_NORMAL    (0ULL << 2)
#define DESC_ATTR_DEVICE    (1ULL << 2)
#define DESC_ADDR_MASK      0x0000fffffffff000ULL

static uint64_t page_align_down(uint64_t value) {
    return value & ~(PAGE_SIZE - 1ULL);
}

static uint64_t page_align_up(uint64_t value) {
    return (value + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
}

static uint64_t table_index(uint64_t vaddr, uint32_t level) {
    return (vaddr >> (39U - level * 9U)) & TABLE_INDEX_MASK;
}

static void zero_table(uint64_t *table) {
    for (uint64_t i = 0; i < TABLE_ENTRIES; i++) {
        table[i] = 0;
    }
}

uint64_t *vmm_new_table(void) {
    uint64_t paddr = pmm_alloc_page();

    if (paddr == 0) {
        return 0;
    }

    uint64_t *table = (uint64_t *)(uintptr_t)paddr;
    zero_table(table);

    return table;
}

static uint64_t *next_table(uint64_t *table, uint64_t index) {
    uint64_t entry = table[index];

    if ((entry & DESC_VALID) != 0) {
        if ((entry & DESC_TABLE) == 0) {
            return 0;
        }

        return (uint64_t *)(uintptr_t)(entry & DESC_ADDR_MASK);
    }

    uint64_t *child = vmm_new_table();
    if (child == 0) {
        return 0;
    }

    table[index] = ((uint64_t)(uintptr_t)child & DESC_ADDR_MASK) | DESC_VALID | DESC_TABLE;

    return child;
}

static uint64_t page_descriptor(uint64_t paddr, uint64_t flags) {
    uint64_t descriptor = (paddr & DESC_ADDR_MASK) | DESC_VALID | DESC_PAGE | DESC_AF;

    if ((flags & VMM_FLAG_DEVICE) != 0) {
        descriptor |= DESC_ATTR_DEVICE | DESC_UXN | DESC_PXN;
    } else {
        descriptor |= DESC_ATTR_NORMAL | DESC_SH_INNER;
    }

    if ((flags & VMM_FLAG_WRITE) == 0) {
        descriptor |= DESC_AP_RO;
    }

    if ((flags & VMM_FLAG_EXEC) == 0) {
        descriptor |= DESC_UXN | DESC_PXN;
    } else {
        descriptor |= DESC_UXN;
    }

    return descriptor;
}

int vmm_map_page(uint64_t *pgd, uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    uint64_t *level1;
    uint64_t *level2;
    uint64_t *level3;
    uint64_t index3;

    if (pgd == 0 || (vaddr & (PAGE_SIZE - 1ULL)) != 0 ||
        (paddr & (PAGE_SIZE - 1ULL)) != 0) {
        return -1;
    }

    level1 = next_table(pgd, table_index(vaddr, 0));
    if (level1 == 0) {
        return -1;
    }

    level2 = next_table(level1, table_index(vaddr, 1));
    if (level2 == 0) {
        return -1;
    }

    level3 = next_table(level2, table_index(vaddr, 2));
    if (level3 == 0) {
        return -1;
    }

    index3 = table_index(vaddr, 3);
    if ((level3[index3] & DESC_VALID) != 0) {
        return -1;
    }

    level3[index3] = page_descriptor(paddr, flags);

    return 0;
}

int vmm_map_range(uint64_t *pgd, uint64_t vaddr, uint64_t paddr,
                  uint64_t size, uint64_t flags) {
    uint64_t aligned_vaddr;
    uint64_t aligned_paddr;
    uint64_t offset;
    uint64_t end;

    if (pgd == 0 || ((vaddr ^ paddr) & (PAGE_SIZE - 1ULL)) != 0) {
        return -1;
    }

    if (size == 0) {
        return 0;
    }

    aligned_vaddr = page_align_down(vaddr);
    aligned_paddr = page_align_down(paddr);
    offset = vaddr - aligned_vaddr;
    end = page_align_up(offset + size);

    for (uint64_t mapped = 0; mapped < end; mapped += PAGE_SIZE) {
        if (vmm_map_page(pgd, aligned_vaddr + mapped, aligned_paddr + mapped, flags) != 0) {
            return -1;
        }
    }

    return 0;
}

uint64_t vmm_leaf_entry(uint64_t *pgd, uint64_t vaddr) {
    uint64_t *table = pgd;

    if (table == 0) {
        return 0;
    }

    for (uint32_t level = 0; level < 3; level++) {
        uint64_t entry = table[table_index(vaddr, level)];

        if ((entry & DESC_VALID) == 0 || (entry & DESC_TABLE) == 0) {
            return 0;
        }

        table = (uint64_t *)(uintptr_t)(entry & DESC_ADDR_MASK);
    }

    return table[table_index(vaddr, 3)];
}

uint64_t vmm_virt_to_phys(uint64_t *pgd, uint64_t vaddr) {
    uint64_t entry = vmm_leaf_entry(pgd, vaddr);

    if ((entry & DESC_VALID) == 0 || (entry & DESC_PAGE) == 0) {
        return 0;
    }

    return (entry & DESC_ADDR_MASK) | (vaddr & (PAGE_SIZE - 1ULL));
}
