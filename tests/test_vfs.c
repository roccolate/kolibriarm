#include <stdint.h>

#include "unity/unity.h"
#include "../kernel/vfs.h"

typedef struct {
    const uint8_t *data;
    uint64_t size;
} test_file_t;

static int test_file_read(void *context, uint64_t offset, uint8_t *buffer,
                          uint64_t capacity, uint64_t *bytes_read) {
    test_file_t *file = (test_file_t *)context;
    uint64_t count;

    if (bytes_read != 0) {
        *bytes_read = 0;
    }

    if (file == 0 || buffer == 0 || bytes_read == 0 || offset > file->size) {
        return -1;
    }

    count = capacity;
    if (count > file->size - offset) {
        count = file->size - offset;
    }

    for (uint64_t i = 0; i < count; i++) {
        buffer[i] = file->data[offset + i];
    }

    *bytes_read = count;
    return 0;
}

void test_vfs_mount_and_find_static_node(void) {
    uint8_t data[] = { 1, 2, 3 };
    test_file_t file = {
        .data = data,
        .size = sizeof(data),
    };
    vfs_node_t node = {
        .path = "/boot/user_demo",
        .size = sizeof(data),
        .read = test_file_read,
        .context = &file,
    };
    const vfs_node_t *found;

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_mount_static(&node, 1));

    found = vfs_find("/boot/user_demo");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_UINT64(sizeof(data), found->size);
    TEST_ASSERT_NULL(vfs_find("/boot/missing"));
}

void test_vfs_read_dispatches_to_node_reader(void) {
    uint8_t data[] = { 0x10, 0x20, 0x30, 0x40 };
    uint8_t buffer[4] = { 0 };
    uint64_t bytes_read = 0;
    test_file_t file = {
        .data = data,
        .size = sizeof(data),
    };
    vfs_node_t node = {
        .path = "/boot/user_demo",
        .size = sizeof(data),
        .read = test_file_read,
        .context = &file,
    };

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_mount_static(&node, 1));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_read("/boot/user_demo", 1, buffer,
                                                sizeof(buffer), &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(3, bytes_read);
    TEST_ASSERT_EQUAL_UINT64(0x20, buffer[0]);
    TEST_ASSERT_EQUAL_UINT64(0x30, buffer[1]);
    TEST_ASSERT_EQUAL_UINT64(0x40, buffer[2]);
    TEST_ASSERT_EQUAL_UINT64(0, buffer[3]);
}

void test_vfs_rejects_invalid_mounts_and_reads(void) {
    uint8_t data[] = { 1 };
    uint8_t buffer[4] = { 0 };
    uint64_t bytes_read = 99;
    test_file_t file = {
        .data = data,
        .size = sizeof(data),
    };
    vfs_node_t valid = {
        .path = "/one",
        .size = sizeof(data),
        .read = test_file_read,
        .context = &file,
    };
    vfs_node_t relative = {
        .path = "one",
        .size = sizeof(data),
        .read = test_file_read,
        .context = &file,
    };
    vfs_node_t no_reader = {
        .path = "/no-reader",
        .size = sizeof(data),
        .read = 0,
        .context = &file,
    };
    vfs_node_t duplicates[2] = {
        {
            .path = "/dup",
            .size = sizeof(data),
            .read = test_file_read,
            .context = &file,
        },
        {
            .path = "/dup",
            .size = sizeof(data),
            .read = test_file_read,
            .context = &file,
        },
    };

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_mount_static(0, 1));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_mount_static(&valid, 0));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_mount_static(&relative, 1));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_mount_static(&no_reader, 1));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_mount_static(duplicates, 2));

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_mount_static(&valid, 1));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_mount_static(&valid, 1));

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_read("/missing", 0, buffer,
                                                sizeof(buffer), &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(0, bytes_read);

    bytes_read = 99;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_read("/one", 0, 0, sizeof(buffer),
                                                &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(0, bytes_read);

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_read("/one", 0, buffer,
                                                sizeof(buffer), 0));
}

void test_vfs_mount_static_respects_node_limit(void) {
    uint8_t data[] = { 1 };
    test_file_t file = {
        .data = data,
        .size = sizeof(data),
    };
    vfs_node_t nodes[VFS_MAX_NODES];
    vfs_node_t extra = {
        .path = "/extra",
        .size = sizeof(data),
        .read = test_file_read,
        .context = &file,
    };
    const char *paths[VFS_MAX_NODES] = {
        "/0", "/1", "/2", "/3", "/4", "/5", "/6", "/7",
    };

    for (uint32_t i = 0; i < VFS_MAX_NODES; i++) {
        nodes[i].path = paths[i];
        nodes[i].size = sizeof(data);
        nodes[i].read = test_file_read;
        nodes[i].context = &file;
    }

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_mount_static(nodes,
                                                        VFS_MAX_NODES));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_mount_static(&extra, 1));
}

void test_vfs_open_read_fd_and_close(void) {
    uint8_t data[] = { 0xa0, 0xb1, 0xc2, 0xd3 };
    uint8_t first[2] = { 0 };
    uint8_t second[4] = { 0 };
    uint64_t bytes_read = 0;
    int fd;
    test_file_t file = {
        .data = data,
        .size = sizeof(data),
    };
    vfs_node_t node = {
        .path = "/seq",
        .size = sizeof(data),
        .read = test_file_read,
        .context = &file,
    };

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_mount_static(&node, 1));

    fd = vfs_open("/seq");
    TEST_ASSERT_TRUE(fd >= 0);
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_read_fd(fd, first, sizeof(first),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(2, bytes_read);
    TEST_ASSERT_EQUAL_UINT64(0xa0, first[0]);
    TEST_ASSERT_EQUAL_UINT64(0xb1, first[1]);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_read_fd(fd, second,
                                                   sizeof(second),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(2, bytes_read);
    TEST_ASSERT_EQUAL_UINT64(0xc2, second[0]);
    TEST_ASSERT_EQUAL_UINT64(0xd3, second[1]);
    TEST_ASSERT_EQUAL_UINT64(0, second[2]);

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_close(fd));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_read_fd(fd, second,
                                                   sizeof(second),
                                                   &bytes_read));
}

void test_vfs_open_rejects_missing_and_invalid_descriptors(void) {
    uint8_t data[] = { 1 };
    uint8_t buffer[1] = { 0 };
    uint64_t bytes_read = 99;
    int fd;
    test_file_t file = {
        .data = data,
        .size = sizeof(data),
    };
    vfs_node_t node = {
        .path = "/one",
        .size = sizeof(data),
        .read = test_file_read,
        .context = &file,
    };

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)vfs_open("/missing"));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_mount_static(&node, 1));

    fd = vfs_open("/one");
    TEST_ASSERT_TRUE(fd >= 0);

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_read_fd(-1, buffer,
                                                   sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(0, bytes_read);

    bytes_read = 99;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_read_fd(VFS_MAX_OPEN_FILES,
                                                   buffer, sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(0, bytes_read);

    bytes_read = 99;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_read_fd(fd, 0, sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(0, bytes_read);

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_read_fd(fd, buffer,
                                                   sizeof(buffer), 0));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_close(fd));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)vfs_close(fd));
}

void test_vfs_open_respects_descriptor_limit_and_reuses_closed_slot(void) {
    uint8_t data[] = { 1 };
    int fds[VFS_MAX_OPEN_FILES];
    test_file_t file = {
        .data = data,
        .size = sizeof(data),
    };
    vfs_node_t node = {
        .path = "/one",
        .size = sizeof(data),
        .read = test_file_read,
        .context = &file,
    };

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_mount_static(&node, 1));

    for (uint32_t i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        fds[i] = vfs_open("/one");
        TEST_ASSERT_TRUE(fds[i] >= 0);
    }

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)vfs_open("/one"));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_close(fds[3]));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)fds[3], (uint64_t)vfs_open("/one"));
}

void test_vfs_stat_reports_node_size(void) {
    uint8_t data[] = { 1, 2, 3, 4, 5 };
    vfs_stat_t stat;
    test_file_t file = {
        .data = data,
        .size = sizeof(data),
    };
    vfs_node_t node = {
        .path = "/stat",
        .size = sizeof(data),
        .read = test_file_read,
        .context = &file,
    };

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_mount_static(&node, 1));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_stat("/stat", &stat));
    TEST_ASSERT_EQUAL_UINT64(sizeof(data), stat.size);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_stat("/missing", &stat));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_stat("/stat", 0));
}

void test_vfs_seek_sets_descriptor_offset(void) {
    uint8_t data[] = { 0x11, 0x22, 0x33, 0x44 };
    uint8_t buffer[2] = { 0 };
    uint64_t bytes_read = 0;
    int fd;
    test_file_t file = {
        .data = data,
        .size = sizeof(data),
    };
    vfs_node_t node = {
        .path = "/seek",
        .size = sizeof(data),
        .read = test_file_read,
        .context = &file,
    };

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_mount_static(&node, 1));
    fd = vfs_open("/seek");
    TEST_ASSERT_TRUE(fd >= 0);

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_seek(fd, 2));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_read_fd(fd, buffer,
                                                   sizeof(buffer),
                                                   &bytes_read));
    TEST_ASSERT_EQUAL_UINT64(2, bytes_read);
    TEST_ASSERT_EQUAL_UINT64(0x33, buffer[0]);
    TEST_ASSERT_EQUAL_UINT64(0x44, buffer[1]);

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)vfs_seek(fd, 5));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_close(fd));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)vfs_seek(fd, 0));
}
