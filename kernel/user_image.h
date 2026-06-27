#ifndef KOLIBRIARM_KERNEL_USER_IMAGE_H
#define KOLIBRIARM_KERNEL_USER_IMAGE_H

#include <stdint.h>

#include "kernel/process.h"
#include "kernel/user_image_format.h"

/*
 * Loaded KLI1 image descriptor.
 *
 * base/size describe the copied flat image in the process's future virtual
 * address space. entry_offset is relative to base and must point inside that
 * copied image. Stack registration is separate because stacks are loader-owned
 * memory, not bytes from the image blob.
 */
typedef struct {
    const char *name;
    uint64_t base;
    uint64_t size;
    uint64_t entry_offset;
} user_image_t;

uint64_t user_image_entry(const user_image_t *image);
int user_image_load_copy(user_image_t *image, const char *name,
                         uint64_t load_base, uint64_t load_capacity,
                         uint64_t source_base, uint64_t source_size,
                         uint64_t source_entry);
int user_image_load_flat(user_image_t *image, const char *name,
                         uint64_t load_base, uint64_t load_capacity,
                         uint64_t source_base, uint64_t source_capacity,
                         uint32_t entry_index);
int user_image_load_bootfs_flat(user_image_t *image, const char *image_name,
                                 const char *bootfs_name, uint64_t load_base,
                                 uint64_t load_capacity,
                                 uint32_t entry_index);
int user_image_prepare_process(process_t *process, const user_image_t *image,
                               uint64_t stack_start, uint64_t stack_size,
                               uint64_t pstate);

#endif
