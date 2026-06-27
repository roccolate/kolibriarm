#include <stdint.h>

#include "unity/unity.h"
#include "../kernel/tmpfs.h"
#include "../kernel/vfs.h"

void test_tmpfs_create_write_read_stat_and_delete(void) {
    uint8_t input[] = { 0x41, 0x42, 0x43, 0x44 };
    uint8_t output[8] = { 0 };
    uint64_t count = 0;
    tmpfs_stat_t stat;

    tmpfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)tmpfs_create("note"));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)tmpfs_write("note", 0, input,
                                                   sizeof(input), &count));
    TEST_ASSERT_EQUAL_UINT64(sizeof(input), count);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)tmpfs_stat("note", &stat));
    TEST_ASSERT_EQUAL_UINT64(sizeof(input), stat.size);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)tmpfs_read("note", 1, output,
                                                  sizeof(output), &count));
    TEST_ASSERT_EQUAL_UINT64(3, count);
    TEST_ASSERT_EQUAL_UINT64(0x42, output[0]);
    TEST_ASSERT_EQUAL_UINT64(0x43, output[1]);
    TEST_ASSERT_EQUAL_UINT64(0x44, output[2]);
    TEST_ASSERT_EQUAL_UINT64(0, output[3]);

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)tmpfs_delete("note"));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)tmpfs_stat("note", &stat));
}

void test_tmpfs_write_clamps_to_file_capacity(void) {
    uint8_t input[4] = { 1, 2, 3, 4 };
    uint8_t output[4] = { 0 };
    uint64_t count = 99;
    tmpfs_stat_t stat;

    tmpfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)tmpfs_create("full"));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)tmpfs_write(
                                 "full", TMPFS_MAX_FILE_SIZE - 2, input,
                                 sizeof(input), &count));
    TEST_ASSERT_EQUAL_UINT64(2, count);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)tmpfs_stat("full", &stat));
    TEST_ASSERT_EQUAL_UINT64(TMPFS_MAX_FILE_SIZE, stat.size);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)tmpfs_read(
                                 "full", TMPFS_MAX_FILE_SIZE - 2, output,
                                 sizeof(output), &count));
    TEST_ASSERT_EQUAL_UINT64(2, count);
    TEST_ASSERT_EQUAL_UINT64(1, output[0]);
    TEST_ASSERT_EQUAL_UINT64(2, output[1]);
    TEST_ASSERT_EQUAL_UINT64(0, output[2]);
}

void test_tmpfs_reused_slot_clears_old_file_bytes(void) {
    uint8_t old_data[] = { 0xaa, 0xbb, 0xcc };
    uint8_t new_tail[] = { 0x11 };
    uint8_t output[3] = { 0xff, 0xff, 0xff };
    uint64_t count = 99;

    tmpfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)tmpfs_create("old"));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)tmpfs_write("old", 0, old_data,
                                                   sizeof(old_data),
                                                   &count));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)tmpfs_delete("old"));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)tmpfs_create("new"));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)tmpfs_write("new", 2, new_tail,
                                                   sizeof(new_tail), &count));
    TEST_ASSERT_EQUAL_UINT64(sizeof(new_tail), count);
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)tmpfs_read("new", 0, output,
                                                  sizeof(output), &count));
    TEST_ASSERT_EQUAL_UINT64(sizeof(output), count);
    TEST_ASSERT_EQUAL_UINT64(0, output[0]);
    TEST_ASSERT_EQUAL_UINT64(0, output[1]);
    TEST_ASSERT_EQUAL_UINT64(0x11, output[2]);
}

void test_tmpfs_rejects_invalid_inputs_and_duplicates(void) {
    uint8_t data[] = { 1 };
    uint8_t output[1] = { 0 };
    uint64_t count = 99;
    tmpfs_stat_t stat;
    char mutable_name[] = { 'm', 'u', 't', '\0' };
    char too_long[TMPFS_MAX_NAME + 1U];

    tmpfs_reset();
    for (uint32_t i = 0; i < sizeof(too_long) - 1U; i++) {
        too_long[i] = 'x';
    }
    too_long[sizeof(too_long) - 1U] = '\0';

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)tmpfs_create(0));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)tmpfs_create(""));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)tmpfs_create(too_long));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)tmpfs_create("one"));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)tmpfs_create("one"));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)tmpfs_create(mutable_name));
    mutable_name[0] = 'x';
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)tmpfs_stat("mut", &stat));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)tmpfs_write("missing", 0, data,
                                                   sizeof(data), &count));
    TEST_ASSERT_EQUAL_UINT64(0, count);

    count = 99;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)tmpfs_write("one", 0, 0,
                                                   sizeof(data), &count));
    TEST_ASSERT_EQUAL_UINT64(0, count);

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)tmpfs_write("one", 0, data,
                                                   sizeof(data), 0));

    count = 99;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)tmpfs_read("one", 0, 0,
                                                  sizeof(output), &count));
    TEST_ASSERT_EQUAL_UINT64(0, count);

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)tmpfs_read("one", 0, output,
                                                  sizeof(output), 0));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)tmpfs_stat("missing",
                                                               &stat));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)tmpfs_stat("one", 0));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)tmpfs_delete("missing"));
}

void test_tmpfs_respects_file_limit_and_reuses_deleted_slot(void) {
    const char *names[TMPFS_MAX_FILES] = {
        "0", "1", "2", "3", "4", "5", "6", "7",
    };

    tmpfs_reset();
    for (uint32_t i = 0; i < TMPFS_MAX_FILES; i++) {
        TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)tmpfs_create(names[i]));
    }

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)tmpfs_create("extra"));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)tmpfs_delete("3"));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)tmpfs_create("extra"));
}

void test_tmpfs_mount_vfs_read_write_and_stat(void) {
    uint8_t input[] = { 0x31, 0x32, 0x33 };
    uint8_t output[4] = { 0 };
    uint64_t count = 0;
    vfs_stat_t stat;
    int fd;

    vfs_reset();
    tmpfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)tmpfs_create("note"));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)tmpfs_mount_vfs("/tmp/note",
                                                          "note"));

    fd = vfs_open_flags("/tmp/note", VFS_O_RDWR);
    TEST_ASSERT_TRUE(fd >= 0);
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_write_fd(fd, input,
                                                    sizeof(input), &count));
    TEST_ASSERT_EQUAL_UINT64(sizeof(input), count);

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_stat("/tmp/note", &stat));
    TEST_ASSERT_EQUAL_UINT64(sizeof(input), stat.size);

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_seek(fd, 0));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_read_fd(fd, output,
                                                   sizeof(output), &count));
    TEST_ASSERT_EQUAL_UINT64(sizeof(input), count);
    TEST_ASSERT_EQUAL_UINT64(0x31, output[0]);
    TEST_ASSERT_EQUAL_UINT64(0x32, output[1]);
    TEST_ASSERT_EQUAL_UINT64(0x33, output[2]);
    TEST_ASSERT_EQUAL_UINT64(0, output[3]);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_close(fd));
}

void test_tmpfs_mount_vfs_rejects_missing_and_invalid_inputs(void) {
    vfs_reset();
    tmpfs_reset();
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)tmpfs_mount_vfs("/tmp/missing",
                                                       "missing"));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)tmpfs_create("note"));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)tmpfs_mount_vfs(0, "note"));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)tmpfs_mount_vfs("tmp/note", "note"));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)tmpfs_mount_vfs("/tmp/note", 0));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)tmpfs_mount_vfs("/tmp/note",
                                                          "note"));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)tmpfs_mount_vfs("/tmp/note",
                                                       "note"));
}
