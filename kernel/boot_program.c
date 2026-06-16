#include "kernel/boot_program.h"

#include <stdint.h>

extern char __user_demo_start[];
extern char __user_demo_end[];

typedef struct {
    const char *name;
    const uint8_t *image_start;
    const uint8_t *image_end;
} boot_program_source_t;

static const boot_program_source_t g_boot_programs[] = {
    {
        .name = "user_demo",
        .image_start = (const uint8_t *)(const void *)__user_demo_start,
        .image_end = (const uint8_t *)(const void *)__user_demo_end,
    },
};

static boot_program_t g_found_program;

static int boot_program_name_equals(const char *left, const char *right) {
    if (left == 0 || right == 0) {
        return 0;
    }

    while (*left != '\0' && *right != '\0') {
        if (*left != *right) {
            return 0;
        }
        left++;
        right++;
    }

    return *left == *right;
}

const boot_program_t *boot_program_find(const char *name) {
    if (name == 0 || name[0] == '\0') {
        return 0;
    }

    for (uint32_t i = 0; i < sizeof(g_boot_programs) / sizeof(g_boot_programs[0]);
         i++) {
        const boot_program_source_t *program = &g_boot_programs[i];
        uint64_t size = (uint64_t)((uintptr_t)program->image_end -
                                   (uintptr_t)program->image_start);

        if (program->image_start != 0 && size != 0 &&
            boot_program_name_equals(program->name, name)) {
            g_found_program.name = program->name;
            g_found_program.image = program->image_start;
            g_found_program.size = size;
            return &g_found_program;
        }
    }

    return 0;
}
