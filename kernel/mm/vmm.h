#ifndef KOLIBRIARM_KERNEL_MM_VMM_H
#define KOLIBRIARM_KERNEL_MM_VMM_H

#include <stdint.h>

#define VMM_FLAG_READ    (1ULL << 0)
#define VMM_FLAG_WRITE   (1ULL << 1)
#define VMM_FLAG_EXEC    (1ULL << 2)
#define VMM_FLAG_DEVICE  (1ULL << 3)

uint64_t *vmm_new_table(void);
int vmm_map_page(uint64_t *pgd, uint64_t vaddr, uint64_t paddr, uint64_t flags);
int vmm_map_range(uint64_t *pgd, uint64_t vaddr, uint64_t paddr,
                  uint64_t size, uint64_t flags);
uint64_t vmm_virt_to_phys(uint64_t *pgd, uint64_t vaddr);
uint64_t vmm_leaf_entry(uint64_t *pgd, uint64_t vaddr);

#endif
