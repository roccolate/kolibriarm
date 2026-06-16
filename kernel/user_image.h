#ifndef KOLIBRIARM_KERNEL_USER_IMAGE_H
#define KOLIBRIARM_KERNEL_USER_IMAGE_H

#include <stdint.h>

#include "kernel/process.h"

#define USER_IMAGE_MAGIC       0x31494c4bU
#define USER_IMAGE_MAX_ENTRIES 4U

typedef struct {
    uint32_t magic;
    uint16_t header_size;
    uint16_t entry_count;
    uint64_t image_size;
    uint64_t entry_offsets[USER_IMAGE_MAX_ENTRIES];
} user_flat_image_header_t;

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
