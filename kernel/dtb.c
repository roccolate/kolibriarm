#include "kernel/dtb.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/dtb_reader.h"

int dtb_get_memory(uint64_t dtb_addr, dtb_memory_t *memory) {
    fdt_view_t view;
    const char *strings;
    uintptr_t cursor;
    uintptr_t struct_end;
    int depth = -1;
    int in_memory = 0;
    uint32_t root_address_cells = 2;
    uint32_t root_size_cells = 1;

    if (memory == NULL || fdt_view_init(dtb_addr, &view) != 0) {
        return -1;
    }

    strings = view.strings;
    cursor = view.cursor;
    struct_end = view.struct_end;

    while (cursor < struct_end) {
        uint32_t token = fdt_be32((const void *)cursor);
        cursor += sizeof(uint32_t);

        if (token == FDT_BEGIN_NODE) {
            const char *name = (const char *)cursor;

            depth++;
            in_memory = depth == 1 && fdt_starts_with(name, "memory");

            while (cursor < struct_end && *(const char *)cursor != '\0') {
                cursor++;
            }
            if (cursor >= struct_end) {
                return -1;
            }
            cursor = fdt_align4(cursor + 1U);
        } else if (token == FDT_END_NODE) {
            if (in_memory && depth == 1) {
                in_memory = 0;
            }

            depth--;
        } else if (token == FDT_PROP) {
            if (cursor + sizeof(uint32_t) * 2U > struct_end) {
                return -1;
            }
            uint32_t len = fdt_be32((const void *)cursor);
            uint32_t nameoff = fdt_be32((const void *)(cursor + sizeof(uint32_t)));
            const char *name = strings + nameoff;
            const uint32_t *value = (const uint32_t *)(cursor + sizeof(uint32_t) * 2U);

            cursor += sizeof(uint32_t) * 2U;

            if (cursor + len > struct_end) {
                return -1;
            }

            if (depth == 0 && fdt_streq(name, "#address-cells") && len >= sizeof(uint32_t)) {
                root_address_cells = fdt_be32(value);
            } else if (depth == 0 && fdt_streq(name, "#size-cells") && len >= sizeof(uint32_t)) {
                root_size_cells = fdt_be32(value);
            } else if (in_memory && fdt_streq(name, "reg")) {
                uint32_t needed = (root_address_cells + root_size_cells) * sizeof(uint32_t);

                if (len < needed || root_address_cells > 2 || root_size_cells > 2) {
                    return -1;
                }

                memory->base = fdt_read_cells(value, root_address_cells);
                memory->size = fdt_read_cells(value + root_address_cells, root_size_cells);
                return 0;
            }

            cursor = fdt_align4(cursor + len);
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            break;
        } else {
            return -1;
        }
    }

    return -1;
}

uint32_t dtb_total_size(uint64_t dtb_addr) {
    fdt_view_t view;

    if (fdt_view_init(dtb_addr, &view) != 0) {
        return 0;
    }

    return view.total_size;
}
