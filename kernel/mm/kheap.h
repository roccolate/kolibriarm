#ifndef KOLIBRIARM_KERNEL_MM_KHEAP_H
#define KOLIBRIARM_KERNEL_MM_KHEAP_H

#include <stddef.h>
#include <stdint.h>

/*
 * PMM-backed kernel heap.
 *
 * Allocations are 16-byte aligned and remain valid until kfree. The allocator
 * owns its metadata in-band before each returned pointer; callers must only
 * pass pointers returned by kmalloc back to kfree.
 */
void kheap_init(void);

/*
 * kmalloc - Allocate kernel heap memory aligned to 16 bytes.
 *
 * Returns a pointer to the allocated memory, or NULL if unavailable.
 */
void *kmalloc(size_t size);

/*
 * kfree - Free memory previously returned by kmalloc.
 */
void kfree(void *ptr);

/*
 * kheap_total_bytes - Return total bytes managed by the kernel heap.
 */
uint64_t kheap_total_bytes(void);

/*
 * kheap_free_bytes - Return currently free bytes in the kernel heap.
 */
uint64_t kheap_free_bytes(void);

#ifdef KOLIBRIARM_TEST
/*
 * kheap_reset_for_tests - Clear kheap globals between isolated host tests.
 */
void kheap_reset_for_tests(void);
#endif

#endif
