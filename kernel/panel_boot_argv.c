#include "kernel/panel_boot_argv.h"

#include <stdint.h>

/*
 * Pure argv stack packer for the EL0 launch path.
 *
 * The syscall layer validates the argv pointer array itself. This helper owns
 * the stack layout: argv pointers first, copied NUL-terminated strings after
 * them, a NULL sentinel at argv[argc], and a 16-byte-aligned returned argv/SP.
 */

static void store_u64(uint8_t *dst, uint64_t value) {
    for (uint32_t i = 0; i < sizeof(uint64_t); i++) {
        dst[i] = (uint8_t)(value >> (i * 8U));
    }
}

int panel_boot_place_argv_on_stack(uint8_t *stack, uint64_t stack_base,
                                   uint64_t stack_size,
                                   const uint64_t *argv_ptr,
                                   uint32_t argc,
                                   uint64_t *out_argv_vaddr) {
    uint64_t stack_top;
    uint64_t argv_vaddr;
    uint64_t cursor;
    uint64_t string_bytes;

    if (stack == 0 || out_argv_vaddr == 0 || stack_size == 0) {
        return -1;
    }

    if (argc == 0) {
        *out_argv_vaddr = 0;
        return 0;
    }
    if (argv_ptr == 0 || argc > PANEL_BOOT_ARGV_MAX_STRINGS) {
        return -1;
    }

    /*
     * Measure each input string up front while enforcing the aggregate budget.
     * The bounded walk prevents a malformed argv string from turning into an
     * unbounded kernel read.
     */
    string_bytes = 0;
    for (uint32_t i = 0; i < argc; i++) {
        const char *str = (const char *)(uintptr_t)argv_ptr[i];
        uint64_t len = 0;

        if (str == 0) {
            return -1;
        }

        for (;;) {
            char c = str[len];

            len++;
            string_bytes++;
            if (string_bytes > PANEL_BOOT_ARGV_MAX_BYTES) {
                return -1;
            }
            if (c == '\0') {
                break;
            }
        }
    }

    {
        uint64_t total =
            string_bytes + (uint64_t)(argc + 1U) * sizeof(uint64_t);

        total = (total + 15U) & ~(uint64_t)15U;
        if (total > stack_size || stack_base + stack_size < stack_base) {
            return -1;
        }

        stack_top = stack_base + stack_size;
        argv_vaddr = stack_top - total;
    }

    cursor = argv_vaddr + (uint64_t)(argc + 1U) * sizeof(uint64_t);

    for (uint32_t i = 0; i < argc; i++) {
        const char *src = (const char *)(uintptr_t)argv_ptr[i];
        uint8_t *dst = stack + (cursor - stack_base);
        uint64_t len = 0;

        for (;;) {
            char c = src[len];

            dst[len] = (uint8_t)c;
            len++;
            if (c == '\0') {
                break;
            }
        }

        store_u64(stack + (argv_vaddr - stack_base) +
                      (uint64_t)i * sizeof(uint64_t),
                  cursor);
        cursor += len;
    }
    store_u64(stack + (argv_vaddr - stack_base) +
                  (uint64_t)argc * sizeof(uint64_t),
              0);

    *out_argv_vaddr = argv_vaddr;
    return 0;
}
