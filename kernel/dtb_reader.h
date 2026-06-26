#ifndef KOLIBRIARM_KERNEL_DTB_READER_H
#define KOLIBRIARM_KERNEL_DTB_READER_H

#include <stddef.h>
#include <stdint.h>

#include "kernel/kernel_compiler.h"

#define FDT_MAGIC      0xd00dfeedU
#define FDT_BEGIN_NODE 0x00000001U
#define FDT_END_NODE   0x00000002U
#define FDT_PROP       0x00000003U
#define FDT_NOP        0x00000004U
#define FDT_END        0x00000009U

typedef struct {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
} fdt_header_t;

typedef struct {
    const char *strings;
    uintptr_t cursor;
    uintptr_t struct_end;
    uint32_t total_size;
} fdt_view_t;

static inline uint32_t fdt_be32(const void *ptr) {
    const uint8_t *bytes = ptr;

    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static inline uint64_t fdt_read_cells(const uint32_t *cells, uint32_t count) {
    uint64_t value = 0;

    for (uint32_t i = 0; i < count; i++) {
        value = (value << 32) | fdt_be32(&cells[i]);
    }

    return value;
}

static inline uintptr_t fdt_align4(uintptr_t value) {
    return (value + 3U) & ~(uintptr_t)3U;
}

static inline int fdt_streq(const char *a, const char *b) {
    while (*a == *b) {
        if (*a == '\0') {
            return 1;
        }

        a++;
        b++;
    }

    return 0;
}

static inline int fdt_starts_with(const char *value, const char *prefix) {
    while (*prefix != '\0') {
        if (*value != *prefix) {
            return 0;
        }

        value++;
        prefix++;
    }

    return 1;
}

static KERNEL_ALWAYS_INLINE int fdt_view_init(uint64_t dtb_addr,
                                              fdt_view_t *view) {
    const fdt_header_t *header = (const fdt_header_t *)(uintptr_t)dtb_addr;
    const uint8_t *base = (const uint8_t *)(uintptr_t)dtb_addr;
    uintptr_t cursor;

    if (dtb_addr == 0 || view == NULL ||
        fdt_be32(&header->magic) != FDT_MAGIC) {
        return -1;
    }

    cursor = (uintptr_t)(base + fdt_be32(&header->off_dt_struct));
    view->strings = (const char *)(base + fdt_be32(&header->off_dt_strings));
    view->cursor = cursor;
    view->struct_end = cursor + fdt_be32(&header->size_dt_struct);
    view->total_size = fdt_be32(&header->totalsize);
    return 0;
}

#endif
