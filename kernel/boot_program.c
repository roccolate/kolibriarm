#include "kernel/boot_program.h"

#include <stdint.h>

/*
 * Embedded boot-program registry.
 *
 * The linker script places each KLI1 app blob in its own .app_* section and
 * exports start/end symbols for this registry. bootfs is a thin VFS-facing
 * wrapper over these entries; the panel loader eventually consumes the same
 * bytes through user_image_load_bootfs_flat.
 */

extern char __app_shell_start[];
extern char __app_shell_end[];
extern char __app_editor_start[];
extern char __app_editor_end[];
extern char __app_monitor_start[];
extern char __app_monitor_end[];
extern char __app_panel_start[];
extern char __app_panel_end[];
extern char __app_clock_start[];
extern char __app_clock_end[];

typedef struct {
    const char *name;
    const uint8_t *image_start;
    const uint8_t *image_end;
} boot_program_source_t;

#define BOOT_PROGRAM_COUNT \
    (sizeof(g_boot_programs) / sizeof(g_boot_programs[0]))

static const boot_program_source_t g_boot_programs[] = {
    {
        .name = "shell",
        .image_start = (const uint8_t *)(const void *)__app_shell_start,
        .image_end = (const uint8_t *)(const void *)__app_shell_end,
    },
    {
        .name = "editor",
        .image_start = (const uint8_t *)(const void *)__app_editor_start,
        .image_end = (const uint8_t *)(const void *)__app_editor_end,
    },
    {
        .name = "monitor",
        .image_start = (const uint8_t *)(const void *)__app_monitor_start,
        .image_end = (const uint8_t *)(const void *)__app_monitor_end,
    },
    {
        .name = "panel",
        .image_start = (const uint8_t *)(const void *)__app_panel_start,
        .image_end = (const uint8_t *)(const void *)__app_panel_end,
    },
    {
        .name = "clock",
        .image_start = (const uint8_t *)(const void *)__app_clock_start,
        .image_end = (const uint8_t *)(const void *)__app_clock_end,
    },
};

static boot_program_t g_found_programs[
    sizeof(g_boot_programs) / sizeof(g_boot_programs[0])];

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

    for (uint32_t i = 0; i < BOOT_PROGRAM_COUNT; i++) {
        const boot_program_source_t *program = &g_boot_programs[i];
        uintptr_t start = (uintptr_t)program->image_start;
        uintptr_t end = (uintptr_t)program->image_end;

        if (start != 0 && end > start &&
            boot_program_name_equals(program->name, name)) {
            boot_program_t *found = &g_found_programs[i];

            found->name = program->name;
            found->image = program->image_start;
            found->size = (uint64_t)(end - start);
            return found;
        }
    }

    return 0;
}
