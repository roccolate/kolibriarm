#ifndef KOLIBRIARM_KERNEL_MM_VMM_H
#define KOLIBRIARM_KERNEL_MM_VMM_H

#include <stdint.h>

#define VMM_FLAG_READ    (1ULL << 0)
#define VMM_FLAG_WRITE   (1ULL << 1)
#define VMM_FLAG_EXEC    (1ULL << 2)
#define VMM_FLAG_DEVICE  (1ULL << 3)
#define VMM_FLAG_USER    (1ULL << 4)

/*
 * Page-table ownership API.
 *
 * vmm_free_table releases only the page-table pages allocated by
 * vmm_new_table/next_table. It does not free the physical pages mapped by leaf
 * descriptors; callers keep owning those pages through process user-region
 * metadata or driver-specific storage.
 */
uint64_t *vmm_new_table(void);
void vmm_free_table(uint64_t *pgd);
int vmm_map_page(uint64_t *pgd, uint64_t vaddr, uint64_t paddr, uint64_t flags);
int vmm_map_range(uint64_t *pgd, uint64_t vaddr, uint64_t paddr,
                  uint64_t size, uint64_t flags);
int vmm_unmap_page(uint64_t *pgd, uint64_t vaddr);
int vmm_unmap_range(uint64_t *pgd, uint64_t vaddr, uint64_t size);
uint64_t vmm_virt_to_phys(uint64_t *pgd, uint64_t vaddr);
uint64_t vmm_leaf_entry(uint64_t *pgd, uint64_t vaddr);

#endif
