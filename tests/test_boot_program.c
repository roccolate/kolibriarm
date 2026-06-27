#include <stdint.h>

#include "unity/unity.h"
#include "../kernel/boot_program.h"

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

__asm__(
    ".section .app_shell_blob, \"a\"\n"
    ".balign 8\n"
    ".global __app_shell_start\n"
    "__app_shell_start:\n"
    ".long 0x31494c4b\n"
    ".short 80\n"
    ".short 1\n"
    ".quad 84\n"
    ".quad 80\n"
    ".skip 60\n"
    ".global __app_shell_end\n"
    "__app_shell_end:\n"

    ".section .app_editor_blob, \"a\"\n"
    ".balign 8\n"
    ".global __app_editor_start\n"
    "__app_editor_start:\n"
    ".long 0x31494c4b\n"
    ".short 80\n"
    ".short 1\n"
    ".quad 84\n"
    ".quad 80\n"
    ".byte 0x99, 0xaa, 0xbb, 0xcc\n"
    ".skip 56\n"
    ".global __app_editor_end\n"
    "__app_editor_end:\n"

    ".section .app_monitor_blob, \"a\"\n"
    ".balign 8\n"
    ".global __app_monitor_start\n"
    "__app_monitor_start:\n"
    ".long 0x31494c4b\n"
    ".short 80\n"
    ".short 1\n"
    ".quad 84\n"
    ".quad 80\n"
    ".byte 0xde, 0xad, 0xbe, 0xef\n"
    ".skip 56\n"
    ".global __app_monitor_end\n"
    "__app_monitor_end:\n"

    ".section .app_panel_blob, \"a\"\n"
    ".balign 8\n"
    ".global __app_panel_start\n"
    "__app_panel_start:\n"
    ".long 0x31494c4b\n"
    ".short 80\n"
    ".short 1\n"
    ".quad 84\n"
    ".quad 80\n"
    ".byte 0x70, 0x61, 0x6e, 0x65\n"
    ".skip 56\n"
    ".global __app_panel_end\n"
    "__app_panel_end:\n"

    ".section .app_clock_blob, \"a\"\n"
    ".balign 8\n"
    ".global __app_clock_start\n"
    "__app_clock_start:\n"
    ".long 0x31494c4b\n"
    ".short 80\n"
    ".short 1\n"
    ".quad 84\n"
    ".quad 80\n"
    ".byte 0x63, 0x6c, 0x6b, 0x21\n"
    ".skip 56\n"
    ".global __app_clock_end\n"
    "__app_clock_end:\n"
);

void test_boot_program_find_existing_program(void) {
    const boot_program_t *program = boot_program_find("shell");

    TEST_ASSERT_NOT_NULL(program);
    TEST_ASSERT_EQUAL_UINT64('s', program->name[0]);
    TEST_ASSERT_EQUAL_UINT64('h', program->name[1]);
    TEST_ASSERT_EQUAL_UINT64('e', program->name[2]);
    TEST_ASSERT_EQUAL_UINT64('l', program->name[3]);
    TEST_ASSERT_EQUAL_UINT64('l', program->name[4]);
    TEST_ASSERT_EQUAL_UINT64('\0', program->name[5]);
}

void test_boot_program_find_rejects_missing_and_invalid_names(void) {
    TEST_ASSERT_NULL(boot_program_find("missing"));
    TEST_ASSERT_NULL(boot_program_find("hello"));
    TEST_ASSERT_NULL(boot_program_find("user_demo"));
    TEST_ASSERT_NULL(boot_program_find(""));
    TEST_ASSERT_NULL(boot_program_find(0));
}

void test_boot_program_metadata_round_trips_image_range(void) {
    const boot_program_t *program = boot_program_find("shell");

    TEST_ASSERT_NOT_NULL(program);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uintptr_t)__app_shell_start,
                             (uint64_t)(uintptr_t)program->image);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)((uintptr_t)__app_shell_end -
                                        (uintptr_t)__app_shell_start),
                             program->size);
}

void test_boot_program_find_resolves_editor_and_panel(void) {
    const boot_program_t *editor;
    const boot_program_t *panel;

    /*
     * The panel taskbar launches each registered app via
     *   sys_spawn("/kolibri/<name>", 0)
     * which routes through boot_program_find("<name>"). The editor
     * button specifically must resolve, otherwise the panel click path
     * silently fails when the user clicks the editor launcher.
     */
    editor = boot_program_find("editor");
    TEST_ASSERT_NOT_NULL(editor);
    TEST_ASSERT_EQUAL_UINT64('e', editor->name[0]);
    TEST_ASSERT_EQUAL_UINT64('d', editor->name[1]);
    TEST_ASSERT_EQUAL_UINT64('i', editor->name[2]);
    TEST_ASSERT_EQUAL_UINT64('t', editor->name[3]);
    TEST_ASSERT_EQUAL_UINT64('o', editor->name[4]);
    TEST_ASSERT_EQUAL_UINT64('r', editor->name[5]);
    TEST_ASSERT_EQUAL_UINT64('\0', editor->name[6]);
    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)((uintptr_t)__app_editor_end - (uintptr_t)__app_editor_start),
        editor->size);

    panel = boot_program_find("panel");
    TEST_ASSERT_NOT_NULL(panel);
    TEST_ASSERT_EQUAL_UINT64('p', panel->name[0]);
    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)((uintptr_t)__app_panel_end - (uintptr_t)__app_panel_start),
        panel->size);
}

void test_boot_program_find_resolves_remaining_registered_apps(void) {
    TEST_ASSERT_NOT_NULL(boot_program_find("monitor"));
    TEST_ASSERT_NOT_NULL(boot_program_find("clock"));
}

void test_boot_program_image_points_to_flat_header_magic(void) {
    /*
     * Each registered image starts with the flat-header magic 0x31494c4b
     * ('KLI1' in little-endian), so user_image_load_flat can parse it.
     * If boot_program_find returned a blob without that prefix, the
     * spawn path would fail with -ENOENT after the loader rejects the
     * header.
     */
    const boot_program_t *editor = boot_program_find("editor");
    const uint32_t magic =
        *(const uint32_t *)(const void *)editor->image;

    TEST_ASSERT_EQUAL_UINT64(0x31494c4bU, magic);
}

void test_boot_program_editor_image_size_matches_assembly_blob(void) {
    /*
     * Guard against silent regressions where boot_program.c drops the
     * editor entry or the linker script stops emitting its blob. The
     * panel's editor button click is meaningless if the loader cannot
     * resolve "editor" to a non-empty image.
     */
    const boot_program_t *editor = boot_program_find("editor");

    TEST_ASSERT_NOT_NULL(editor);
    TEST_ASSERT_TRUE(editor->size > 80);
    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)((uintptr_t)__app_editor_end - (uintptr_t)__app_editor_start),
        editor->size);
}

void test_boot_program_find_results_survive_later_lookups(void) {
    const boot_program_t *editor = boot_program_find("editor");
    const uint8_t *editor_image;
    uint64_t editor_size;

    TEST_ASSERT_NOT_NULL(editor);
    editor_image = editor->image;
    editor_size = editor->size;

    TEST_ASSERT_NOT_NULL(boot_program_find("panel"));
    TEST_ASSERT_NOT_NULL(boot_program_find("clock"));

    TEST_ASSERT_EQUAL_UINT64('e', editor->name[0]);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uintptr_t)__app_editor_start,
                             (uint64_t)(uintptr_t)editor->image);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uintptr_t)editor_image,
                             (uint64_t)(uintptr_t)editor->image);
    TEST_ASSERT_EQUAL_UINT64(editor_size, editor->size);
}
