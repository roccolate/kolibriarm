#ifndef KOLIBRIARM_KERNEL_DTB_H
#define KOLIBRIARM_KERNEL_DTB_H

#include <stdint.h>

typedef struct {
    uint64_t base;
    uint64_t size;
} dtb_memory_t;

int dtb_get_memory(uint64_t dtb_addr, dtb_memory_t *memory);
uint32_t dtb_total_size(uint64_t dtb_addr);

#endif
