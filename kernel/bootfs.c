#include "kernel/bootfs.h"

#include <stdint.h>

#include "kernel/boot_program.h"
#include "kernel/vfs.h"

static bootfs_file_t g_found_file;
static vfs_node_t g_bootfs_vfs_nodes[1];

static int bootfs_vfs_read(void *context, uint64_t offset, uint8_t *buffer,
                           uint64_t capacity, uint64_t *bytes_read) {
    return bootfs_read((const char *)context, offset, buffer, capacity,
                       bytes_read);
}

const bootfs_file_t *bootfs_find(const char *name) {
    const boot_program_t *program = boot_program_find(name);

    if (program == 0) {
        return 0;
    }

    g_found_file.name = program->name;
    g_found_file.data = program->image;
    g_found_file.size = program->size;
    return &g_found_file;
}

int bootfs_read(const char *name, uint64_t offset, uint8_t *buffer,
                uint64_t capacity, uint64_t *bytes_read) {
    const bootfs_file_t *file = bootfs_find(name);
    uint64_t available;
    uint64_t count;

    if (bytes_read != 0) {
        *bytes_read = 0;
    }

    if (file == 0 || buffer == 0 || bytes_read == 0 || offset > file->size) {
        return -1;
    }

    available = file->size - offset;
    count = capacity;
    if (count > available) {
        count = available;
    }

    for (uint64_t i = 0; i < count; i++) {
        buffer[i] = file->data[offset + i];
    }

    *bytes_read = count;
    return 0;
}

int bootfs_mount_vfs(void) {
    const bootfs_file_t *file = bootfs_find("user_demo");

    if (file == 0) {
        return -1;
    }

    g_bootfs_vfs_nodes[0].path = "/boot/user_demo";
    g_bootfs_vfs_nodes[0].size = file->size;
    g_bootfs_vfs_nodes[0].read = bootfs_vfs_read;
    g_bootfs_vfs_nodes[0].context = (void *)file->name;

    return vfs_mount_static(g_bootfs_vfs_nodes, 1);
}
