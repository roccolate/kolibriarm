#include <stdint.h>

#include "unity/unity.h"
#include "../kernel/vfs.h"

typedef struct {
    const uint8_t *data;
    uint64_t size;
} test_file_t;

typedef struct {
    uint8_t data[8];
    uint64_t size;
    uint64_t capacity;
} test_rw_file_t;

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

static int test_rw_file_read(void *context, uint64_t offset, uint8_t *buffer,
                             uint64_t capacity, uint64_t *bytes_read) {
    test_rw_file_t *file = (test_rw_file_t *)context;
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

static int test_rw_file_write(void *context, uint64_t offset,
                              const uint8_t *buffer, uint64_t size,
                              uint64_t *bytes_written) {
    test_rw_file_t *file = (test_rw_file_t *)context;
    uint64_t count;

    if (bytes_written != 0) {
        *bytes_written = 0;
    }

    if (file == 0 || buffer == 0 || bytes_written == 0 ||
        offset > file->size || offset > file->capacity) {
        return -1;
    }

    count = size;
    if (count > file->capacity - offset) {
        count = file->capacity - offset;
    }

    for (uint64_t i = 0; i < count; i++) {
        file->data[offset + i] = buffer[i];
    }

    if (offset + count > file->size) {
        file->size = offset + count;
    }

    *bytes_written = count;
    return 0;
}

static int test_rw_file_stat(void *context, vfs_stat_t *stat) {
    test_rw_file_t *file = (test_rw_file_t *)context;

    if (file == 0 || stat == 0) {
        return -1;
    }

    stat->size = file->size;
    return 0;
}

static int test_list_static(void *context, uint8_t *buffer, uint64_t capacity,
                            uint64_t *bytes_written) {
    const char *text = (const char *)context;
    uint64_t count = 0;

    if (bytes_written != 0) {
        *bytes_written = 0;
    }
    if (text == 0 || buffer == 0 || bytes_written == 0) {
        return -1;
    }

    while (text[count] != '\0' && count < capacity) {
        buffer[count] = (uint8_t)text[count];
        count++;
    }
    *bytes_written = count;
    return 0;
}

static int test_bad_read_overreports(void *context, uint64_t offset,
                                     uint8_t *buffer, uint64_t capacity,
                                     uint64_t *bytes_read) {
    (void)context;
    (void)offset;
    (void)buffer;

    if (bytes_read != 0) {
        *bytes_read = capacity + 1U;
    }
    return 0;
}

static int test_bad_write_overreports(void *context, uint64_t offset,
                                      const uint8_t *buffer, uint64_t size,
                                      uint64_t *bytes_written) {
    (void)context;
    (void)offset;
    (void)buffer;

    if (bytes_written != 0) {
        *bytes_written = size + 1U;
    }
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
    char long_path[VFS_MAX_PATH + 1U];
    vfs_node_t too_long = {
        .path = long_path,
        .size = sizeof(data),
        .read = test_file_read,
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

    long_path[0] = '/';
    for (uint32_t i = 1; i < VFS_MAX_PATH; i++) {
        long_path[i] = 'x';
    }
    long_path[VFS_MAX_PATH] = '\0';

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
                             (uint64_t)vfs_mount_static(&too_long, 1));
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

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_write("/one", 0, data,
                                                 sizeof(data), 0));
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
        "/8", "/9", "/a", "/b", "/c", "/d", "/e", "/f",
        "/g", "/h", "/i", "/j", "/k", "/l", "/m", "/n",
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

void test_vfs_strip_prefix_returns_suffix_for_exact_prefix(void) {
    const char *armonios = vfs_strip_prefix("/armonios/shell", "/armonios/");
    const char *fat = vfs_strip_prefix("/fat/dir/file.txt", "/fat/");

    TEST_ASSERT_NOT_NULL(armonios);
    TEST_ASSERT_EQUAL_UINT64('s', (uint64_t)armonios[0]);
    TEST_ASSERT_EQUAL_UINT64('h', (uint64_t)armonios[1]);
    TEST_ASSERT_EQUAL_UINT64('\0', (uint64_t)armonios[5]);

    TEST_ASSERT_NOT_NULL(fat);
    TEST_ASSERT_EQUAL_UINT64('d', (uint64_t)fat[0]);
    TEST_ASSERT_EQUAL_UINT64('/', (uint64_t)fat[3]);
    TEST_ASSERT_EQUAL_UINT64('\0', (uint64_t)fat[12]);
}

void test_vfs_strip_prefix_rejects_invalid_or_empty_suffix(void) {
    TEST_ASSERT_NULL(vfs_strip_prefix(0, "/fat/"));
    TEST_ASSERT_NULL(vfs_strip_prefix("/fat/a", 0));
    TEST_ASSERT_NULL(vfs_strip_prefix("/fat/a", ""));
    TEST_ASSERT_NULL(vfs_strip_prefix("/fat", "/fat/"));
    TEST_ASSERT_NULL(vfs_strip_prefix("/fat/", "/fat/"));
    TEST_ASSERT_NULL(vfs_strip_prefix("/fatty/a", "/fat/"));
    TEST_ASSERT_NULL(vfs_strip_prefix("/armonios", "/armonios/"));
    TEST_ASSERT_NULL(vfs_strip_prefix("/armonios/", "/armonios/"));
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

void test_vfs_open_flags_enforce_read_write_modes(void) {
    uint8_t input[] = { 9 };
    uint8_t output[2] = { 0 };
    uint64_t count = 99;
    int fd;
    test_rw_file_t file = {
        .data = { 1, 2 },
        .size = 2,
        .capacity = 8,
    };
    vfs_node_t node = {
        .path = "/rw-mode",
        .size = 2,
        .read = test_rw_file_read,
        .write = test_rw_file_write,
        .stat = test_rw_file_stat,
        .context = &file,
    };

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_mount_static(&node, 1));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_open_flags("/rw-mode", 3));

    fd = vfs_open_flags("/rw-mode", VFS_O_WRONLY);
    TEST_ASSERT_TRUE(fd >= 0);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_read_fd(fd, output,
                                                   sizeof(output), &count));
    TEST_ASSERT_EQUAL_UINT64(0, count);
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_write_fd(fd, input,
                                                    sizeof(input), &count));
    TEST_ASSERT_EQUAL_UINT64(sizeof(input), count);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_close(fd));

    fd = vfs_open_flags("/rw-mode", VFS_O_RDONLY);
    TEST_ASSERT_TRUE(fd >= 0);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_write_fd(fd, input,
                                                    sizeof(input), &count));
    TEST_ASSERT_EQUAL_UINT64(0, count);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_close(fd));

    fd = vfs_open_flags("/rw-mode", VFS_O_RDWR);
    TEST_ASSERT_TRUE(fd >= 0);
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_read_fd(fd, output,
                                                   sizeof(output), &count));
    TEST_ASSERT_EQUAL_UINT64(2, count);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_close(fd));
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

void test_vfs_list_root_returns_mounted_paths(void) {
    uint8_t data[] = { 1 };
    uint8_t buffer[64] = { 0 };
    uint64_t bytes_written = 0;
    test_file_t file = {
        .data = data,
        .size = sizeof(data),
    };
    vfs_node_t nodes[2] = {
        {
            .path = "/boot/user_demo",
            .size = sizeof(data),
            .read = test_file_read,
            .context = &file,
        },
        {
            .path = "/tmp/note",
            .size = sizeof(data),
            .read = test_file_read,
            .context = &file,
        },
    };

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_mount_static(nodes, 2));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_list("/", buffer,
                                                  sizeof(buffer),
                                                  &bytes_written));
    TEST_ASSERT_EQUAL_UINT64(26, bytes_written);
    for (uint64_t i = 0; i < bytes_written; i++) {
        TEST_ASSERT_EQUAL_UINT64(
            (uint64_t)((const uint8_t *)"/boot/user_demo\n/tmp/note\n")[i],
            buffer[i]);
    }

    bytes_written = 99;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_list("/tmp", buffer,
                                                sizeof(buffer),
                                                &bytes_written));
    TEST_ASSERT_EQUAL_UINT64(0, bytes_written);
}

void test_vfs_list_truncates_to_capacity(void) {
    uint8_t data[] = { 1 };
    uint8_t buffer[8] = { 0 };
    uint64_t bytes_written = 0;
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
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_list("/", buffer,
                                                  sizeof(buffer),
                                                  &bytes_written));
    TEST_ASSERT_EQUAL_UINT64(sizeof(buffer), bytes_written);
    for (uint64_t i = 0; i < bytes_written; i++) {
        TEST_ASSERT_EQUAL_UINT64((uint64_t)((const uint8_t *)"/boot/us")[i],
                                 buffer[i]);
    }
}

void test_vfs_mount_list_validates_paths_and_duplicates(void) {
    uint8_t buffer[8] = { 0 };
    uint64_t bytes_written = 0;
    char long_path[VFS_MAX_PATH + 1U];

    long_path[0] = '/';
    for (uint32_t i = 1; i < VFS_MAX_PATH; i++) {
        long_path[i] = 'l';
    }
    long_path[VFS_MAX_PATH] = '\0';

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_mount_list(
                                 0, test_list_static, "bad\n"));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_mount_list(
                                 "relative", test_list_static, "bad\n"));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_mount_list(
                                 long_path, test_list_static, "bad\n"));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_mount_list("/fat", 0, "bad\n"));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_mount_list(
                                 "/fat", test_list_static, "ok\n"));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_mount_list(
                                 "/fat", test_list_static, "dup\n"));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_list("/fat", buffer,
                                                  sizeof(buffer),
                                                  &bytes_written));
    TEST_ASSERT_EQUAL_UINT64(3, bytes_written);
    TEST_ASSERT_EQUAL_UINT64('o', buffer[0]);
    TEST_ASSERT_EQUAL_UINT64('k', buffer[1]);
    TEST_ASSERT_EQUAL_UINT64('\n', buffer[2]);
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

void test_vfs_write_dispatches_to_node_writer(void) {
    uint8_t input[] = { 0x55, 0x66, 0x77 };
    uint8_t output[4] = { 0 };
    uint64_t count = 0;
    vfs_stat_t stat;
    test_rw_file_t file = {
        .data = { 0 },
        .size = 0,
        .capacity = 8,
    };
    vfs_node_t node = {
        .path = "/rw",
        .size = 0,
        .read = test_rw_file_read,
        .write = test_rw_file_write,
        .stat = test_rw_file_stat,
        .context = &file,
    };

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_mount_static(&node, 1));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_write("/rw", 0, input,
                                                 sizeof(input), &count));
    TEST_ASSERT_EQUAL_UINT64(sizeof(input), count);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_stat("/rw", &stat));
    TEST_ASSERT_EQUAL_UINT64(sizeof(input), stat.size);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_read("/rw", 0, output,
                                                sizeof(output), &count));
    TEST_ASSERT_EQUAL_UINT64(sizeof(input), count);
    TEST_ASSERT_EQUAL_UINT64(0x55, output[0]);
    TEST_ASSERT_EQUAL_UINT64(0x66, output[1]);
    TEST_ASSERT_EQUAL_UINT64(0x77, output[2]);
    TEST_ASSERT_EQUAL_UINT64(0, output[3]);
}

void test_vfs_open_write_fd_updates_offset_and_size(void) {
    uint8_t first[] = { 1, 2 };
    uint8_t second[] = { 3, 4 };
    uint8_t output[4] = { 0 };
    uint64_t count = 0;
    int fd;
    test_rw_file_t file = {
        .data = { 0 },
        .size = 0,
        .capacity = 8,
    };
    vfs_node_t node = {
        .path = "/fd-rw",
        .size = 0,
        .read = test_rw_file_read,
        .write = test_rw_file_write,
        .stat = test_rw_file_stat,
        .context = &file,
    };

    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_mount_static(&node, 1));
    fd = vfs_open_flags("/fd-rw", VFS_O_RDWR);
    TEST_ASSERT_TRUE(fd >= 0);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_write_fd(fd, first, sizeof(first),
                                                    &count));
    TEST_ASSERT_EQUAL_UINT64(sizeof(first), count);
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_write_fd(fd, second,
                                                    sizeof(second), &count));
    TEST_ASSERT_EQUAL_UINT64(sizeof(second), count);

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_seek(fd, 0));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)vfs_read_fd(fd, output,
                                                   sizeof(output), &count));
    TEST_ASSERT_EQUAL_UINT64(sizeof(output), count);
    TEST_ASSERT_EQUAL_UINT64(1, output[0]);
    TEST_ASSERT_EQUAL_UINT64(2, output[1]);
    TEST_ASSERT_EQUAL_UINT64(3, output[2]);
    TEST_ASSERT_EQUAL_UINT64(4, output[3]);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_close(fd));
}

void test_vfs_rejects_overreported_io_counts(void) {
    uint8_t buffer[4] = { 0 };
    uint64_t count = 99;
    vfs_node_t read_node = {
        .path = "/bad-read",
        .size = sizeof(buffer),
        .read = test_bad_read_overreports,
    };
    vfs_node_t write_node = {
        .path = "/bad-write",
        .size = 0,
        .write = test_bad_write_overreports,
    };

    /*
     * Filesystem callbacks must not report more bytes than VFS requested.
     * Rejecting impossible counts keeps descriptor offsets from advancing
     * past the caller-visible operation size if a backend misbehaves.
     */
    vfs_reset();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_mount_static(&read_node, 1));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_read("/bad-read", 0, buffer,
                                                sizeof(buffer), &count));
    TEST_ASSERT_EQUAL_UINT64(0, count);

    vfs_reset();
    count = 99;
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vfs_mount_static(&write_node, 1));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_write("/bad-write", 0, buffer,
                                                 sizeof(buffer), &count));
    TEST_ASSERT_EQUAL_UINT64(0, count);
}

/*
 * vfs_unlink / vfs_rename reject anything that is not under
 * "/fat/". The full filesystem round-trip is covered by the FAT32
 * tests; these cases just lock down the path-validation contract so
 * tmpfs / bootfs callers don't accidentally try to delete an
 * immutable path.
 */
void test_vfs_unlink_rejects_non_fat_paths(void) {
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)vfs_unlink("/"));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)vfs_unlink("/tmp/note"));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_unlink("/fat/"));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_unlink("/armonios/shell"));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)vfs_unlink(0));
}

void test_vfs_rename_rejects_non_fat_paths(void) {
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_rename("/tmp/a", "/tmp/b"));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_rename("/fat/a", "/tmp/b"));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vfs_rename("/tmp/a", "/fat/b"));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)vfs_rename(0, "/fat/b"));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)vfs_rename("/fat/a", 0));
}
