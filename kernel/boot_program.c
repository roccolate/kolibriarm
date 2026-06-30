#include "kernel/boot_program.h"

#include <stdint.h>

#include "kernel/kstring.h"
#include "kernel/user_image_format.h"

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
extern char __app_files_start[];
extern char __app_files_end[];
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
        .name = "files",
        .image_start = (const uint8_t *)(const void *)__app_files_start,
        .image_end = (const uint8_t *)(const void *)__app_files_end,
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

static int boot_program_source_is_kli1(const boot_program_source_t *program,
                                       uint64_t *out_size) {
    uintptr_t start;
    uintptr_t end;
    uint64_t size;
    const user_flat_image_header_t *header;

    if (program == 0 || out_size == 0) {
        return 0;
    }

    start = (uintptr_t)program->image_start;
    end = (uintptr_t)program->image_end;
    if (start == 0 || end <= start) {
        return 0;
    }

    size = (uint64_t)(end - start);
    if (size < USER_IMAGE_HEADER_SIZE) {
        return 0;
    }

    header =
        (const user_flat_image_header_t *)(const void *)program->image_start;
    if (header->magic != USER_IMAGE_MAGIC ||
        header->header_size != USER_IMAGE_HEADER_SIZE ||
        header->image_size > size ||
        header->entry_count == 0 ||
        header->entry_count > USER_IMAGE_MAX_ENTRIES) {
        return 0;
    }

    /*
     * The registry only exposes KLI1 images with entry points inside the
     * declared image range. The blob may be larger than image_size because the
     * linked app blob can carry non-runtime metadata after the KLI1 payload.
     */
    for (uint32_t i = 0; i < header->entry_count; i++) {
        if (header->entry_offsets[i] < header->header_size ||
            header->entry_offsets[i] >= header->image_size) {
            return 0;
        }
    }

    *out_size = size;
    return 1;
}

const boot_program_t *boot_program_find(const char *name) {
    if (name == 0 || name[0] == '\0') {
        return 0;
    }

    for (uint32_t i = 0; i < BOOT_PROGRAM_COUNT; i++) {
        const boot_program_source_t *program = &g_boot_programs[i];
        uint64_t size;

        if (kstreq(program->name, name) &&
            boot_program_source_is_kli1(program, &size)) {
            boot_program_t *found = &g_found_programs[i];

            found->name = program->name;
            found->image = program->image_start;
            found->size = size;
            return found;
        }
    }

    return 0;
}
