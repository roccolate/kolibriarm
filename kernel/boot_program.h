#ifndef KOLIBRIARM_KERNEL_BOOT_PROGRAM_H
#define KOLIBRIARM_KERNEL_BOOT_PROGRAM_H

#include <stdint.h>

typedef struct {
    const char *name;
    const uint8_t *image;
    uint64_t size;
} boot_program_t;

const boot_program_t *boot_program_find(const char *name);

#endif
