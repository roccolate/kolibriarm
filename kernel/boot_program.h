#ifndef KOLIBRIARM_KERNEL_BOOT_PROGRAM_H
#define KOLIBRIARM_KERNEL_BOOT_PROGRAM_H

#include <stdint.h>

/*
 * Descriptor for an app image embedded in the kernel bootfs image.
 *
 * The returned image starts at a KLI1 header and spans exactly the linked
 * blob range. boot_program_find returns a stable pointer for each registered
 * program; callers may keep it across later lookups.
 */
typedef struct {
    const char *name;
    const uint8_t *image;
    uint64_t size;
} boot_program_t;

const boot_program_t *boot_program_find(const char *name);

#endif
