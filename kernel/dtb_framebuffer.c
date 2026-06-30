#include "kernel/dtb.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/dtb_reader.h"

static int compatible_has(const char *value, uint32_t len, const char *needle) {
    uint32_t start = 0;

    for (uint32_t i = 0; i < len; i++) {
        if (value[i] == '\0') {
            if (fdt_streq(value + start, needle)) {
                return 1;
            }

            start = i + 1U;
        }
    }

    return 0;
}

int dtb_get_framebuffer(uint64_t dtb_addr, dtb_framebuffer_t *framebuffer) {
    fdt_view_t view;
    const char *strings;
    uintptr_t cursor;
    uintptr_t struct_end;
    int depth = -1;
    int in_framebuffer = 0;
    int have_reg = 0;
    int have_width = 0;
    int have_height = 0;
    int have_stride = 0;
    uint32_t root_address_cells = 2;
    uint32_t root_size_cells = 1;

    if (framebuffer == NULL || fdt_view_init(dtb_addr, &view) != 0) {
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
            if (depth == 1) {
                in_framebuffer = fdt_starts_with(name, "framebuffer");
                have_reg = 0;
                have_width = 0;
                have_height = 0;
                have_stride = 0;
            }

            while (cursor < struct_end && *(const char *)cursor != '\0') {
                cursor++;
            }
            if (cursor >= struct_end) {
                return -1;
            }
            cursor = fdt_align4(cursor + 1U);
        } else if (token == FDT_END_NODE) {
            if (depth == 1 && in_framebuffer && have_reg != 0 && have_width != 0 &&
                have_height != 0 && have_stride != 0) {
                return 0;
            }

            if (depth == 1) {
                in_framebuffer = 0;
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

            if (depth == 0 && fdt_streq(name, "#address-cells") &&
                len >= sizeof(uint32_t)) {
                root_address_cells = fdt_be32(value);
            } else if (depth == 0 && fdt_streq(name, "#size-cells") &&
                       len >= sizeof(uint32_t)) {
                root_size_cells = fdt_be32(value);
            } else if (depth == 1 && fdt_streq(name, "compatible") &&
                       compatible_has((const char *)value, len, "simple-framebuffer")) {
                in_framebuffer = 1;
            } else if (depth == 1 && in_framebuffer && fdt_streq(name, "reg")) {
                uint32_t needed = (root_address_cells + root_size_cells) * sizeof(uint32_t);

                if (len < needed || root_address_cells > 2 || root_size_cells > 2) {
                    return -1;
                }

                framebuffer->base = fdt_read_cells(value, root_address_cells);
                framebuffer->size = fdt_read_cells(value + root_address_cells,
                                                   root_size_cells);
                have_reg = 1;
            } else if (depth == 1 && in_framebuffer && fdt_streq(name, "width") &&
                       len >= sizeof(uint32_t)) {
                framebuffer->width = fdt_be32(value);
                have_width = 1;
            } else if (depth == 1 && in_framebuffer && fdt_streq(name, "height") &&
                       len >= sizeof(uint32_t)) {
                framebuffer->height = fdt_be32(value);
                have_height = 1;
            } else if (depth == 1 && in_framebuffer && fdt_streq(name, "stride") &&
                       len >= sizeof(uint32_t)) {
                framebuffer->stride_bytes = fdt_be32(value);
                have_stride = 1;
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
