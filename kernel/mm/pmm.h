#ifndef KOLIBRIARM_KERNEL_MM_PMM_H
#define KOLIBRIARM_KERNEL_MM_PMM_H

#include <stdint.h>

#define PAGE_SIZE 4096ULL

/*
 * Fixed-range physical page allocator.
 *
 * pmm_init seeds a bitmap for page-aligned physical memory. Reserve calls mark
 * boot-owned ranges used, allocation returns physical page addresses, and free
 * calls are intentionally idempotent for invalid or already-free pages.
 */

void pmm_init(uint64_t mem_base, uint64_t mem_size);
void pmm_reserve_range(uint64_t start, uint64_t size);
uint64_t pmm_alloc_page(void);
uint64_t pmm_alloc_pages(uint64_t count);
void pmm_free_page(uint64_t paddr);
uint64_t pmm_free_count(void);
uint64_t pmm_total_count(void);

#endif
