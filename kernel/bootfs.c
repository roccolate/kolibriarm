#include "kernel/bootfs.h"

#include <stdint.h>

#include "kernel/boot_program.h"
#include "kernel/vfs.h"

#define BOOTFS_MAX_NODES 5U

static bootfs_file_t g_found_file;
static vfs_node_t g_bootfs_vfs_nodes[BOOTFS_MAX_NODES];

static int bootfs_vfs_read(void *context, uint64_t offset, uint8_t *buffer,
                           uint64_t capacity, uint64_t *bytes_read) {
    const char *name = (const char *)context;

    if (name == 0) {
        return -1;
    }

    return bootfs_read(name, offset, buffer, capacity, bytes_read);
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
    static const char *app_names[] = {
        "shell", "editor", "monitor", "panel", "clock",
    };
    static const char *app_paths[] = {
        "/kolibri/shell", "/kolibri/editor", "/kolibri/monitor",
        "/kolibri/panel", "/kolibri/clock",
    };
    uint32_t mounted = 0;
    uint32_t app_count = sizeof(app_names) / sizeof(app_names[0]);

    for (uint32_t i = 0; i < BOOTFS_MAX_NODES; i++) {
        g_bootfs_vfs_nodes[i].path = 0;
        g_bootfs_vfs_nodes[i].size = 0;
        g_bootfs_vfs_nodes[i].read = 0;
        g_bootfs_vfs_nodes[i].write = 0;
        g_bootfs_vfs_nodes[i].stat = 0;
        g_bootfs_vfs_nodes[i].context = 0;
    }

    for (uint32_t i = 0; i < app_count; i++) {
        const bootfs_file_t *file = bootfs_find(app_names[i]);
        vfs_node_t *node;

        if (file == 0 || file->size == 0) {
            continue;
        }

        node = &g_bootfs_vfs_nodes[mounted];
        node->path = app_paths[i];
        node->size = file->size;
        node->read = bootfs_vfs_read;
        node->context = (void *)file->name;
        mounted++;
    }

    if (mounted == 0) {
        return -1;
    }

    return vfs_mount_static(g_bootfs_vfs_nodes, mounted);
}
