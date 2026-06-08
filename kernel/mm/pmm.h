#ifndef KOLIBRIARM_KERNEL_MM_PMM_H
#define KOLIBRIARM_KERNEL_MM_PMM_H

#include <stdint.h>

#define PAGE_SIZE 4096ULL

void pmm_init(uint64_t mem_base, uint64_t mem_size);
void pmm_reserve_range(uint64_t start, uint64_t size);
uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t paddr);
uint64_t pmm_free_count(void);

#endif
