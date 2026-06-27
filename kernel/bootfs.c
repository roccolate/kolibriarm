#include "kernel/bootfs.h"

#include <stdint.h>

#include "kernel/boot_program.h"
#include "kernel/vfs.h"

/*
 * Boot filesystem over embedded KLI1 app images.
 *
 * bootfs exposes the boot_program registry as read-only files under
 * /kolibri/<name>. It is the always-available app source used when storage is
 * absent or FAT32 probing fails.
 */

#define BOOTFS_MAX_NODES 5U

typedef struct {
    const char *name;
    const char *path;
} bootfs_entry_t;

static const bootfs_entry_t g_bootfs_entries[] = {
    { "shell", "/kolibri/shell" },
    { "editor", "/kolibri/editor" },
    { "monitor", "/kolibri/monitor" },
    { "panel", "/kolibri/panel" },
    { "clock", "/kolibri/clock" },
};

#define BOOTFS_ENTRY_COUNT \
    (sizeof(g_bootfs_entries) / sizeof(g_bootfs_entries[0]))

_Static_assert(BOOTFS_ENTRY_COUNT <= BOOTFS_MAX_NODES,
               "bootfs entries must fit mounted VFS nodes");

static bootfs_file_t g_found_files[BOOTFS_ENTRY_COUNT];
static vfs_node_t g_bootfs_vfs_nodes[BOOTFS_MAX_NODES];

static int bootfs_name_equals(const char *left, const char *right) {
    if (left == 0 || right == 0) {
        return 0;
    }

    while (*left != '\0' && *right != '\0') {
        if (*left != *right) {
            return 0;
        }
        left++;
        right++;
    }

    return *left == *right;
}

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

    for (uint32_t i = 0; i < BOOTFS_ENTRY_COUNT; i++) {
        if (bootfs_name_equals(program->name, g_bootfs_entries[i].name)) {
            bootfs_file_t *file = &g_found_files[i];

            file->name = program->name;
            file->data = program->image;
            file->size = program->size;
            return file;
        }
    }

    return 0;
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
    uint32_t mounted = 0;

    for (uint32_t i = 0; i < BOOTFS_MAX_NODES; i++) {
        g_bootfs_vfs_nodes[i].path = 0;
        g_bootfs_vfs_nodes[i].size = 0;
        g_bootfs_vfs_nodes[i].read = 0;
        g_bootfs_vfs_nodes[i].write = 0;
        g_bootfs_vfs_nodes[i].stat = 0;
        g_bootfs_vfs_nodes[i].context = 0;
    }

    for (uint32_t i = 0; i < BOOTFS_ENTRY_COUNT; i++) {
        const bootfs_file_t *file = bootfs_find(g_bootfs_entries[i].name);
        vfs_node_t *node;

        if (file == 0 || file->size == 0) {
            continue;
        }

        node = &g_bootfs_vfs_nodes[mounted];
        node->path = g_bootfs_entries[i].path;
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
