#ifndef KOLIBRIARM_KERNEL_USER_IMAGE_H
#define KOLIBRIARM_KERNEL_USER_IMAGE_H

#include <stdint.h>

#include "kernel/process.h"

#define USER_IMAGE_MAGIC       0x31494c4bU
#define USER_IMAGE_MAX_ENTRIES 8U

/*
 * KOS flat-header magic. KolibriOS programs and tools that emit a "KOS"
 * header use 0x004B4F4B ('KOS\0' in little-endian). The KOS variant is
 * a synonym for our KLI1 header so we can reuse cross-built binaries
 * once the loader accepts both. Phase 10.5 is the second and last piece
 * of the KolibriOS port we need at first (after the 8x8 font).
 */
#define USER_KOS_MAGIC         0x00534F4BU

typedef struct {
    uint32_t magic;
    uint16_t header_size;
    uint16_t entry_count;
    uint64_t image_size;
    uint64_t entry_offsets[USER_IMAGE_MAX_ENTRIES];
} user_flat_image_header_t;

/*
 * KOS flat-header layout. The KOS header is a fixed 24 bytes; programs
 * built with KolibriOS tooling emit this layout as a synonym for our
 * KLI1 header. The reserved field is preserved so future fields can be
 * added without breaking the on-disk shape.
 */
typedef struct {
    uint32_t magic;          /* USER_KOS_MAGIC */
    uint32_t image_size;     /* total image size including this header */
    uint32_t mem_size;       /* required memory size; 0 means image_size */
    uint32_t stack_size;     /* requested stack size; 0 means default */
    uint32_t entry_offset;   /* offset of entry point within the image */
    uint32_t reserved;       /* future use; must be 0 today */
} user_kos_image_header_t;

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
