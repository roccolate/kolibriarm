#include <stdint.h>

#include "unity/unity.h"
#include "../kernel/bootfs.h"
#include "../kernel/vfs.h"

extern char __user_demo_start[];
extern char __user_demo_end[];

void test_bootfs_find_existing_file_metadata(void) {
    const bootfs_file_t *file = bootfs_find("user_demo");

    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uintptr_t)__user_demo_start,
                             (uint64_t)(uintptr_t)file->data);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)((uintptr_t)__user_demo_end -
                                        (uintptr_t)__user_demo_start),
                             file->size);
}

void test_bootfs_find_rejects_missing_and_invalid_names(void) {
    TEST_ASSERT_NULL(bootfs_find("missing"));
    TEST_ASSERT_NULL(bootfs_find(""));
    TEST_ASSERT_NULL(bootfs_find(0));
}

void test_bootfs_read_copies_requested_range(void) {
    uint8_t buffer[4] = { 0 };
    uint64_t bytes_read = 0;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)bootfs_read("user_demo", 1, buffer,
                                                   sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(sizeof(buffer), bytes_read);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__user_demo_start[1], buffer[0]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__user_demo_start[2], buffer[1]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__user_demo_start[3], buffer[2]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__user_demo_start[4], buffer[3]);
}

void test_bootfs_read_clamps_at_end_of_file(void) {
    uint8_t buffer[8] = { 0 };
    uint64_t bytes_read = 99;
    uint64_t size = (uint64_t)((uintptr_t)__user_demo_end -
                               (uintptr_t)__user_demo_start);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)bootfs_read("user_demo", size - 2,
                                                   buffer, sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(2, bytes_read);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__user_demo_start[size - 2], buffer[0]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__user_demo_start[size - 1], buffer[1]);
    TEST_ASSERT_EQUAL_UINT64(0, buffer[2]);
}

void test_bootfs_read_rejects_invalid_inputs(void) {
    uint8_t buffer[4] = { 0 };
    uint64_t bytes_read = 99;
    uint64_t size = (uint64_t)((uintptr_t)__user_demo_end -
                               (uintptr_t)__user_demo_start);

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)bootfs_read("missing", 0, buffer,
                                                   sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(0, bytes_read);

    bytes_read = 99;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)bootfs_read("user_demo", size + 1,
                                                   buffer, sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(0, bytes_read);

    bytes_read = 99;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)bootfs_read("user_demo", 0, 0,
                                                   sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(0, bytes_read);

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)bootfs_read("user_demo", 0, buffer,
                                                   sizeof(buffer), 0));
}

void test_bootfs_mount_vfs_exposes_user_demo_path(void) {
    const vfs_node_t *node;

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)bootfs_mount_vfs());

    node = vfs_find("/boot/user_demo");
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)((uintptr_t)__user_demo_end -
                                        (uintptr_t)__user_demo_start),
                             node->size);
}

void test_bootfs_mount_vfs_reads_user_demo_through_vfs(void) {
    uint8_t buffer[4] = { 0 };
    uint64_t bytes_read = 0;

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)bootfs_mount_vfs());
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_read("/boot/user_demo", 0, buffer,
                                                sizeof(buffer), &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(sizeof(buffer), bytes_read);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__user_demo_start[0], buffer[0]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__user_demo_start[1], buffer[1]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__user_demo_start[2], buffer[2]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__user_demo_start[3], buffer[3]);
}
