#include "kernel/vfs.h"

#include <stdint.h>

static vfs_node_t g_vfs_nodes[VFS_MAX_NODES];
static uint32_t g_vfs_node_count;

typedef struct {
    const vfs_node_t *node;
    uint64_t offset;
    uint8_t used;
} vfs_open_file_t;

static vfs_open_file_t g_open_files[VFS_MAX_OPEN_FILES];

static int vfs_path_equals(const char *left, const char *right) {
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

void vfs_reset(void) {
    for (uint32_t i = 0; i < VFS_MAX_NODES; i++) {
        g_vfs_nodes[i].path = 0;
        g_vfs_nodes[i].size = 0;
        g_vfs_nodes[i].read = 0;
        g_vfs_nodes[i].context = 0;
    }
    for (uint32_t i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        g_open_files[i].node = 0;
        g_open_files[i].offset = 0;
        g_open_files[i].used = 0;
    }
    g_vfs_node_count = 0;
}

int vfs_mount_static(const vfs_node_t *nodes, uint32_t count) {
    if (nodes == 0 || count == 0 ||
        g_vfs_node_count + count > VFS_MAX_NODES) {
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        const vfs_node_t *node = &nodes[i];

        if (node->path == 0 || node->path[0] != '/' || node->read == 0 ||
            vfs_find(node->path) != 0) {
            return -1;
        }

        for (uint32_t j = 0; j < i; j++) {
            if (vfs_path_equals(nodes[j].path, node->path)) {
                return -1;
            }
        }
    }

    for (uint32_t i = 0; i < count; i++) {
        g_vfs_nodes[g_vfs_node_count] = nodes[i];
        g_vfs_node_count++;
    }

    return 0;
}

const vfs_node_t *vfs_find(const char *path) {
    if (path == 0 || path[0] == '\0') {
        return 0;
    }

    for (uint32_t i = 0; i < g_vfs_node_count; i++) {
        if (vfs_path_equals(g_vfs_nodes[i].path, path)) {
            return &g_vfs_nodes[i];
        }
    }

    return 0;
}

int vfs_read(const char *path, uint64_t offset, uint8_t *buffer,
             uint64_t capacity, uint64_t *bytes_read) {
    const vfs_node_t *node = vfs_find(path);

    if (bytes_read != 0) {
        *bytes_read = 0;
    }

    if (node == 0 || node->read == 0 || buffer == 0 || bytes_read == 0 ||
        offset > node->size) {
        return -1;
    }

    return node->read(node->context, offset, buffer, capacity, bytes_read);
}

int vfs_stat(const char *path, vfs_stat_t *stat) {
    const vfs_node_t *node = vfs_find(path);

    if (node == 0 || stat == 0) {
        return -1;
    }

    stat->size = node->size;
    return 0;
}

int vfs_open(const char *path) {
    const vfs_node_t *node = vfs_find(path);

    if (node == 0) {
        return -1;
    }

    for (uint32_t i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (g_open_files[i].used == 0) {
            g_open_files[i].node = node;
            g_open_files[i].offset = 0;
            g_open_files[i].used = 1;
            return (int)i;
        }
    }

    return -1;
}

int vfs_read_fd(int fd, uint8_t *buffer, uint64_t capacity,
                uint64_t *bytes_read) {
    vfs_open_file_t *file;
    int status;

    if (bytes_read != 0) {
        *bytes_read = 0;
    }

    if (fd < 0 || fd >= (int)VFS_MAX_OPEN_FILES || buffer == 0 ||
        bytes_read == 0 || g_open_files[fd].used == 0 ||
        g_open_files[fd].node == 0) {
        return -1;
    }

    file = &g_open_files[fd];
    if (file->offset > file->node->size) {
        return -1;
    }

    status = file->node->read(file->node->context, file->offset, buffer,
                              capacity, bytes_read);
    if (status != 0) {
        *bytes_read = 0;
        return status;
    }

    file->offset += *bytes_read;
    return 0;
}

int vfs_seek(int fd, uint64_t offset) {
    if (fd < 0 || fd >= (int)VFS_MAX_OPEN_FILES ||
        g_open_files[fd].used == 0 || g_open_files[fd].node == 0 ||
        offset > g_open_files[fd].node->size) {
        return -1;
    }

    g_open_files[fd].offset = offset;
    return 0;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= (int)VFS_MAX_OPEN_FILES ||
        g_open_files[fd].used == 0) {
        return -1;
    }

    g_open_files[fd].node = 0;
    g_open_files[fd].offset = 0;
    g_open_files[fd].used = 0;
    return 0;
}
