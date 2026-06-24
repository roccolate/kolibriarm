#include "kernel/boot_program.h"

#include <stdint.h>

extern char __app_hello_start[];
extern char __app_hello_end[];
extern char __app_loop_start[];
extern char __app_loop_end[];
extern char __app_fault_start[];
extern char __app_fault_end[];
extern char __app_shell_start[];
extern char __app_shell_end[];
extern char __app_editor_start[];
extern char __app_editor_end[];
extern char __app_monitor_start[];
extern char __app_monitor_end[];
extern char __app_win_start[];
extern char __app_win_end[];
extern char __app_panel_start[];
extern char __app_panel_end[];
extern char __app_clock_start[];
extern char __app_clock_end[];
extern char __app_kos_hello_start[];
extern char __app_kos_hello_end[];

typedef struct {
    const char *name;
    const uint8_t *image_start;
    const uint8_t *image_end;
} boot_program_source_t;

static const boot_program_source_t g_boot_programs[] = {
    {
        .name = "hello",
        .image_start = (const uint8_t *)(const void *)__app_hello_start,
        .image_end = (const uint8_t *)(const void *)__app_hello_end,
    },
    {
        .name = "loop",
        .image_start = (const uint8_t *)(const void *)__app_loop_start,
        .image_end = (const uint8_t *)(const void *)__app_loop_end,
    },
    {
        .name = "fault",
        .image_start = (const uint8_t *)(const void *)__app_fault_start,
        .image_end = (const uint8_t *)(const void *)__app_fault_end,
    },
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
        .name = "win",
        .image_start = (const uint8_t *)(const void *)__app_win_start,
        .image_end = (const uint8_t *)(const void *)__app_win_end,
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
    {
        .name = "kos_hello",
        .image_start = (const uint8_t *)(const void *)__app_kos_hello_start,
        .image_end = (const uint8_t *)(const void *)__app_kos_hello_end,
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
