#ifndef KOLIBRIARM_KERNEL_LAYOUT_H
#define KOLIBRIARM_KERNEL_LAYOUT_H

#include "kernel/mm/pmm.h"
#include "kernel/process.h"

/*
 * C-owned user address layout for the fixed per-process image and stack
 * slots. These values describe virtual addresses only; backing pages are
 * allocated per process by panel_boot/user_vm code.
 *
 * The kernel link base and boot stack size still live in linker.ld because the
 * linker script is not generated from C headers.
 */
#define KERNEL_USER_IMAGE_VA_BASE 0x400000ULL
#define KERNEL_USER_IMAGE_VA_STRIDE 0x10000ULL
#define KERNEL_USER_IMAGE_SLOT_SIZE 8192ULL
#define KERNEL_USER_IMAGE_SLOT_PAGES \
    ((KERNEL_USER_IMAGE_SLOT_SIZE + PAGE_SIZE - 1ULL) / PAGE_SIZE)

#define KERNEL_USER_STACK_VA_BASE 0x800000ULL
#define KERNEL_USER_STACK_VA_STRIDE 0x10000ULL
#define KERNEL_USER_STACK_SIZE 4096ULL
#define KERNEL_USER_STACK_GUARD_SIZE PAGE_SIZE
#define KERNEL_USER_STACK_PAGES \
    ((KERNEL_USER_STACK_SIZE + PAGE_SIZE - 1ULL) / PAGE_SIZE)

#define KERNEL_USER_IMAGE_VA_LIMIT \
    (KERNEL_USER_IMAGE_VA_BASE + \
     (uint64_t)PROCESS_MAX_PROCESSES * KERNEL_USER_IMAGE_VA_STRIDE)
#define KERNEL_USER_STACK_VA_LIMIT \
    (KERNEL_USER_STACK_VA_BASE + \
     (uint64_t)PROCESS_MAX_PROCESSES * KERNEL_USER_STACK_VA_STRIDE)

_Static_assert(KERNEL_USER_IMAGE_SLOT_SIZE <= KERNEL_USER_IMAGE_VA_STRIDE,
               "user image slot must fit inside its stride");
_Static_assert(KERNEL_USER_STACK_SIZE <= KERNEL_USER_STACK_VA_STRIDE,
               "user stack must fit inside its stride");
_Static_assert(KERNEL_USER_STACK_SIZE + KERNEL_USER_STACK_GUARD_SIZE <=
                   KERNEL_USER_STACK_VA_STRIDE,
               "user stack stride must leave an unmapped guard page");
_Static_assert((KERNEL_USER_IMAGE_VA_BASE & (PAGE_SIZE - 1ULL)) == 0,
               "user image base must be page aligned");
_Static_assert((KERNEL_USER_STACK_VA_BASE & (PAGE_SIZE - 1ULL)) == 0,
               "user stack base must be page aligned");
_Static_assert(KERNEL_USER_IMAGE_VA_LIMIT <= KERNEL_USER_STACK_VA_BASE ||
                   KERNEL_USER_STACK_VA_LIMIT <= KERNEL_USER_IMAGE_VA_BASE,
               "user image and stack slot ranges must not overlap");
_Static_assert(KERNEL_USER_STACK_VA_LIMIT <= PROCESS_USER_MMAP_BASE,
               "fixed user slots must stay below mmap space");

#endif
