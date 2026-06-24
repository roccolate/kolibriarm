#include <stdint.h>

#include "unity/unity.h"
#include "../kernel/bootfs.h"
#include "../kernel/vfs.h"

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

void test_bootfs_find_existing_file_metadata(void) {
    const bootfs_file_t *file = bootfs_find("hello");

    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uintptr_t)__app_hello_start,
                             (uint64_t)(uintptr_t)file->data);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)((uintptr_t)__app_hello_end -
                                        (uintptr_t)__app_hello_start),
                             file->size);
}

void test_bootfs_find_returns_each_registered_app(void) {
    TEST_ASSERT_NOT_NULL(bootfs_find("hello"));
    TEST_ASSERT_NOT_NULL(bootfs_find("loop"));
    TEST_ASSERT_NOT_NULL(bootfs_find("fault"));
    TEST_ASSERT_NOT_NULL(bootfs_find("shell"));
}

void test_bootfs_finds_editor_and_panel_for_taskbar_spawn(void) {
    /*
     * The panel's editor button click resolves the editor through this
     * path: bootfs_find("editor") -> bootfs_read -> user_image_load.
     * If either lookup fails, sys_spawn returns -ENOENT and the panel
     * silently does nothing on click. The bootfs_find result feeds the
     * user_image loader directly, so we also assert the editor's
     * bootfs data pointer matches the linked-in assembly blob.
     *
     * bootfs_find returns a pointer into a file-scope static buffer
     * (g_found_file), so every call overwrites the previous result.
     * Snapshot each lookup's fields into locals before doing the next
     * call, otherwise the editor assertions see the panel data.
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

void test_bootfs_finds_monitor_win_and_clock(void) {
    TEST_ASSERT_NOT_NULL(bootfs_find("monitor"));
    TEST_ASSERT_NOT_NULL(bootfs_find("win"));
    TEST_ASSERT_NOT_NULL(bootfs_find("clock"));
}

void test_bootfs_find_rejects_missing_and_invalid_names(void) {
    TEST_ASSERT_NULL(bootfs_find("missing"));
    TEST_ASSERT_NULL(bootfs_find("user_demo"));
    TEST_ASSERT_NULL(bootfs_find(""));
    TEST_ASSERT_NULL(bootfs_find(0));
}

void test_bootfs_read_copies_requested_range(void) {
    uint8_t buffer[4] = { 0 };
    uint64_t bytes_read = 0;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)bootfs_read("hello", 1, buffer,
                                                   sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(sizeof(buffer), bytes_read);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_hello_start[1], buffer[0]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_hello_start[2], buffer[1]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_hello_start[3], buffer[2]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_hello_start[4], buffer[3]);
}

void test_bootfs_read_clamps_at_end_of_file(void) {
    uint8_t buffer[8] = { 0 };
    uint64_t bytes_read = 99;
    uint64_t size = (uint64_t)((uintptr_t)__app_hello_end -
                               (uintptr_t)__app_hello_start);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)bootfs_read("hello", size - 2,
                                                   buffer, sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(2, bytes_read);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_hello_start[size - 2], buffer[0]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_hello_start[size - 1], buffer[1]);
    TEST_ASSERT_EQUAL_UINT64(0, buffer[2]);
}

void test_bootfs_read_rejects_invalid_inputs(void) {
    uint8_t buffer[4] = { 0 };
    uint64_t bytes_read = 99;
    uint64_t size = (uint64_t)((uintptr_t)__app_hello_end -
                               (uintptr_t)__app_hello_start);

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)bootfs_read("missing", 0, buffer,
                                                   sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(0, bytes_read);

    bytes_read = 99;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)bootfs_read("hello", size + 1,
                                                   buffer, sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(0, bytes_read);

    bytes_read = 99;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)bootfs_read("hello", 0, 0,
                                                   sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(0, bytes_read);

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)bootfs_read("hello", 0, buffer,
                                                   sizeof(buffer), 0));
}

void test_bootfs_mount_vfs_exposes_kolibri_paths(void) {
    const vfs_node_t *hello_node;
    const vfs_node_t *loop_node;
    const vfs_node_t *shell_node;

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)bootfs_mount_vfs());

    hello_node = vfs_find("/kolibri/hello");
    TEST_ASSERT_NOT_NULL(hello_node);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)((uintptr_t)__app_hello_end -
                                        (uintptr_t)__app_hello_start),
                             hello_node->size);

    loop_node = vfs_find("/kolibri/loop");
    TEST_ASSERT_NOT_NULL(loop_node);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)((uintptr_t)__app_loop_end -
                                        (uintptr_t)__app_loop_start),
                             loop_node->size);

    shell_node = vfs_find("/kolibri/shell");
    TEST_ASSERT_NOT_NULL(shell_node);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)((uintptr_t)__app_shell_end -
                                        (uintptr_t)__app_shell_start),
                             shell_node->size);
}

void test_bootfs_mount_vfs_exposes_editor_and_panel_paths(void) {
    const vfs_node_t *editor_node;
    const vfs_node_t *panel_node;

    /*
     * The panel click handler builds "/kolibri/editor" and passes it to
     * sys_spawn. The lookup vfs_find("/kolibri/editor") is the same
     * path the spawn code uses, so if this assertion ever fails the
     * editor button is broken even though boot_program_find("editor")
     * may still succeed in isolation.
     */
    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)bootfs_mount_vfs());

    editor_node = vfs_find("/kolibri/editor");
    TEST_ASSERT_NOT_NULL(editor_node);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)((uintptr_t)__app_editor_end -
                                        (uintptr_t)__app_editor_start),
                             editor_node->size);

    panel_node = vfs_find("/kolibri/panel");
    TEST_ASSERT_NOT_NULL(panel_node);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)((uintptr_t)__app_panel_end -
                                        (uintptr_t)__app_panel_start),
                             panel_node->size);
}

void test_bootfs_mount_vfs_exposes_remaining_app_paths(void) {
    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)bootfs_mount_vfs());

    TEST_ASSERT_NOT_NULL(vfs_find("/kolibri/monitor"));
    TEST_ASSERT_NOT_NULL(vfs_find("/kolibri/win"));
    TEST_ASSERT_NOT_NULL(vfs_find("/kolibri/clock"));
}

void test_bootfs_mount_vfs_reads_editor_through_vfs(void) {
    uint8_t buffer[4] = { 0 };
    uint64_t bytes_read = 0;

    /*
     * Mirrors what user_image_load_bootfs_flat does internally: open
     * /kolibri/editor, read the first few bytes, and confirm the flat
     * header magic 0x31494c4b ('KLI1') is the first uint32_t. Without
     * this, a typo in bootfs.c or a path mismatch would silently
     * corrupt the editor launcher.
     */
    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)bootfs_mount_vfs());
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_read("/kolibri/editor", 0, buffer,
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
                             (uint64_t)vfs_read("/kolibri/hello", 0, buffer,
                                                sizeof(buffer), &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(sizeof(buffer), bytes_read);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_hello_start[0], buffer[0]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_hello_start[1], buffer[1]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_hello_start[2], buffer[2]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__app_hello_start[3], buffer[3]);
}

void test_bootfs_old_user_demo_path_is_gone(void) {
    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)bootfs_mount_vfs());
    TEST_ASSERT_NULL(vfs_find("/boot/user_demo"));
}
