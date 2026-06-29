#include <stdint.h>

#include "unity/unity.h"
#include "../kernel/bootfs.h"
#include "../kernel/vfs.h"

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

void test_bootfs_find_existing_file_metadata(void) {
    const bootfs_file_t *file = bootfs_find("shell");

    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uintptr_t)__app_shell_start,
                             (uint64_t)(uintptr_t)file->data);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)((uintptr_t)__app_shell_end -
                                        (uintptr_t)__app_shell_start),
                             file->size);
}

void test_bootfs_find_returns_each_registered_app(void) {
    TEST_ASSERT_NOT_NULL(bootfs_find("shell"));
    TEST_ASSERT_NOT_NULL(bootfs_find("editor"));
    TEST_ASSERT_NOT_NULL(bootfs_find("files"));
    TEST_ASSERT_NOT_NULL(bootfs_find("monitor"));
    TEST_ASSERT_NOT_NULL(bootfs_find("clock"));
}

void test_bootfs_finds_editor_and_panel_for_taskbar_spawn(void) {
    /*
     * The panel's editor button click resolves the editor through this
     * path: bootfs_find("editor") -> bootfs_read -> user_image_load.
     * If either lookup fails, sys_spawn returns -ENOENT and the panel
     * silently does nothing on click. The bootfs_find result feeds the
     * user_image loader directly, so we also assert the editor's
     * bootfs data pointer matches the linked-in assembly blob.
     */
    const bootfs_file_t *editor_file;
    const uint8_t *editor_data;
    uint64_t editor_size;
    const bootfs_file_t *panel_file;

    editor_file = bootfs_find("editor");
    TEST_ASSERT_NOT_NULL(editor_file);
    editor_data = editor_file->data;
    editor_size = editor_file->size;

    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)((uintptr_t)__app_editor_end - (uintptr_t)__app_editor_start),
        editor_size);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uintptr_t)__app_editor_start,
                             (uint64_t)(uintptr_t)editor_data);

    panel_file = bootfs_find("panel");
    TEST_ASSERT_NOT_NULL(panel_file);
    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)((uintptr_t)__app_panel_end - (uintptr_t)__app_panel_start),
        panel_file->size);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uintptr_t)__app_panel_start,
                             (uint64_t)(uintptr_t)panel_file->data);
}

void test_bootfs_finds_monitor_and_clock(void) {
    TEST_ASSERT_NOT_NULL(bootfs_find("monitor"));
    TEST_ASSERT_NOT_NULL(bootfs_find("clock"));
}

void test_bootfs_find_rejects_missing_and_invalid_names(void) {
    TEST_ASSERT_NULL(bootfs_find("missing"));
    TEST_ASSERT_NULL(bootfs_find("hello"));
    TEST_ASSERT_NULL(bootfs_find(""));
    TEST_ASSERT_NULL(bootfs_find(0));
}

void test_bootfs_read_copies_requested_range(void) {
    uint8_t buffer[4] = { 0 };
    uint64_t bytes_read = 0;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)bootfs_read("shell", 1, buffer,
                                                   sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(sizeof(buffer), bytes_read);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_shell_start[1], buffer[0]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_shell_start[2], buffer[1]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_shell_start[3], buffer[2]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_shell_start[4], buffer[3]);
}

void test_bootfs_read_clamps_at_end_of_file(void) {
    uint8_t buffer[8] = { 0 };
    uint64_t bytes_read = 99;
    uint64_t size = (uint64_t)((uintptr_t)__app_shell_end -
                               (uintptr_t)__app_shell_start);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)bootfs_read("shell", size - 2,
                                                   buffer, sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(2, bytes_read);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_shell_start[size - 2], buffer[0]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_shell_start[size - 1], buffer[1]);
    TEST_ASSERT_EQUAL_UINT64(0, buffer[2]);
}

void test_bootfs_read_rejects_invalid_inputs(void) {
    uint8_t buffer[4] = { 0 };
    uint64_t bytes_read = 99;
    uint64_t size = (uint64_t)((uintptr_t)__app_shell_end -
                               (uintptr_t)__app_shell_start);

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)bootfs_read("missing", 0, buffer,
                                                   sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(0, bytes_read);

    bytes_read = 99;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)bootfs_read("shell", size + 1,
                                                   buffer, sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(0, bytes_read);

    bytes_read = 99;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)bootfs_read("shell", 0, 0,
                                                   sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(0, bytes_read);

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)bootfs_read("shell", 0, buffer,
                                                   sizeof(buffer), 0));
}

void test_bootfs_mount_vfs_exposes_armonios_paths(void) {
    const vfs_node_t *shell_node;

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)bootfs_mount_vfs());

    shell_node = vfs_find("/armonios/shell");
    TEST_ASSERT_NOT_NULL(shell_node);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)((uintptr_t)__app_shell_end -
                                        (uintptr_t)__app_shell_start),
                             shell_node->size);
}

void test_bootfs_mount_vfs_exposes_editor_and_panel_paths(void) {
    const vfs_node_t *editor_node;
    const vfs_node_t *panel_node;

    /*
     * The panel click handler builds "/armonios/editor" and passes it to
     * sys_spawn. The lookup vfs_find("/armonios/editor") is the same
     * path the spawn code uses, so if this assertion ever fails the
     * editor button is broken even though boot_program_find("editor")
     * may still succeed in isolation.
     */
    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)bootfs_mount_vfs());

    editor_node = vfs_find("/armonios/editor");
    TEST_ASSERT_NOT_NULL(editor_node);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)((uintptr_t)__app_editor_end -
                                        (uintptr_t)__app_editor_start),
                             editor_node->size);

    panel_node = vfs_find("/armonios/panel");
    TEST_ASSERT_NOT_NULL(panel_node);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)((uintptr_t)__app_panel_end -
                                        (uintptr_t)__app_panel_start),
                             panel_node->size);
}

void test_bootfs_mount_vfs_exposes_remaining_app_paths(void) {
    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)bootfs_mount_vfs());

    TEST_ASSERT_NOT_NULL(vfs_find("/armonios/monitor"));
    TEST_ASSERT_NOT_NULL(vfs_find("/armonios/clock"));
    TEST_ASSERT_NOT_NULL(vfs_find("/armonios/files"));
}

void test_bootfs_mount_vfs_reads_editor_through_vfs(void) {
    uint8_t buffer[4] = { 0 };
    uint64_t bytes_read = 0;

    /*
     * Mirrors what user_image_load_bootfs_flat does internally: open
     * /armonios/editor, read the first few bytes, and confirm the flat
     * header magic 0x31494c4b ('KLI1') is the first uint32_t. Without
     * this, a typo in bootfs.c or a path mismatch would silently
     * corrupt the editor launcher.
     */
    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)bootfs_mount_vfs());
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_read("/armonios/editor", 0, buffer,
                                                sizeof(buffer), &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(sizeof(buffer), bytes_read);

    uint32_t magic = (uint32_t)buffer[0] | ((uint32_t)buffer[1] << 8) |
                     ((uint32_t)buffer[2] << 16) |
                     ((uint32_t)buffer[3] << 24);
    TEST_ASSERT_EQUAL_UINT64(0x31494c4bU, magic);
}

void test_bootfs_mount_vfs_reads_app_through_vfs(void) {
    uint8_t buffer[4] = { 0 };
    uint64_t bytes_read = 0;

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)bootfs_mount_vfs());
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_read("/armonios/shell", 0, buffer,
                                                sizeof(buffer), &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(sizeof(buffer), bytes_read);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_shell_start[0], buffer[0]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_shell_start[1], buffer[1]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_shell_start[2], buffer[2]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_shell_start[3], buffer[3]);
}

void test_bootfs_old_user_demo_path_is_gone(void) {
    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)bootfs_mount_vfs());
    TEST_ASSERT_NULL(vfs_find("/boot/user_demo"));
}

void test_bootfs_find_results_survive_later_lookups(void) {
    const bootfs_file_t *editor = bootfs_find("editor");
    const uint8_t *editor_data;
    uint64_t editor_size;

    TEST_ASSERT_NOT_NULL(editor);
    editor_data = editor->data;
    editor_size = editor->size;

    TEST_ASSERT_NOT_NULL(bootfs_find("panel"));
    TEST_ASSERT_NOT_NULL(bootfs_find("files"));
    TEST_ASSERT_NOT_NULL(bootfs_find("clock"));

    TEST_ASSERT_EQUAL_UINT64('e', editor->name[0]);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uintptr_t)__app_editor_start,
                             (uint64_t)(uintptr_t)editor->data);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uintptr_t)editor_data,
                             (uint64_t)(uintptr_t)editor->data);
    TEST_ASSERT_EQUAL_UINT64(editor_size, editor->size);
}
