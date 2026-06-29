#include "kernel/vfs.h"

#include <stdint.h>

#include "kernel/fat32.h"

/*
 * Small in-kernel virtual filesystem.
 *
 * VFS owns the path table, open-file offsets, and mounted list callbacks. It
 * does not allocate memory or own file contents; mounted filesystems keep
 * their backing storage and expose read/write/stat/list operations here.
 */

static vfs_node_t g_vfs_nodes[VFS_MAX_NODES];
static char g_vfs_paths[VFS_MAX_NODES][VFS_MAX_PATH];
static uint32_t g_vfs_node_count;

#define VFS_MAX_LIST_MOUNTS 4U

typedef struct {
    char path[VFS_MAX_PATH];
    vfs_list_fn_t list;
    void *context;
    uint8_t used;
} vfs_list_mount_t;

static vfs_list_mount_t g_list_mounts[VFS_MAX_LIST_MOUNTS];

typedef struct {
    const vfs_node_t *node;
    uint64_t offset;
    uint32_t flags;
    uint8_t used;
} vfs_open_file_t;

static vfs_open_file_t g_open_files[VFS_MAX_OPEN_FILES];

static vfs_open_file_t *vfs_fd_at(int fd) {
    if (fd < 0 || fd >= (int)VFS_MAX_OPEN_FILES ||
        g_open_files[fd].used == 0 || g_open_files[fd].node == 0) {
        return 0;
    }

    return &g_open_files[fd];
}

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

static int vfs_path_is_mountable(const char *path) {
    uint32_t i = 0;

    if (path == 0 || path[0] != '/') {
        return 0;
    }

    while (path[i] != '\0') {
        if (i + 1U >= VFS_MAX_PATH) {
            return 0;
        }
        i++;
    }

    return 1;
}

static int vfs_copy_path(char dest[VFS_MAX_PATH], const char *path) {
    uint32_t i = 0;

    if (dest == 0 || path == 0 || path[0] != '/') {
        return -1;
    }

    while (path[i] != '\0') {
        if (i + 1U >= VFS_MAX_PATH) {
            return -1;
        }

        dest[i] = path[i];
        i++;
    }

    dest[i] = '\0';
    return 0;
}

static int vfs_node_size(const vfs_node_t *node, uint64_t *size) {
    vfs_stat_t stat;

    if (node == 0 || size == 0) {
        return -1;
    }

    if (node->stat != 0) {
        if (node->stat(node->context, &stat) != 0) {
            return -1;
        }
        *size = stat.size;
    } else {
        *size = node->size;
    }

    return 0;
}

static int vfs_read_result_valid(uint64_t offset, uint64_t file_size,
                                 uint64_t capacity, uint64_t count) {
    if (offset > file_size || count > capacity) {
        return 0;
    }

    return count <= file_size - offset;
}

static int vfs_write_result_valid(uint64_t offset, uint64_t requested,
                                  uint64_t count) {
    if (count > requested || offset > UINT64_MAX - count) {
        return 0;
    }

    return 1;
}

static uint32_t vfs_open_access_mode(uint32_t flags) {
    return flags & VFS_O_ACCMODE;
}

static int vfs_open_flags_valid(uint32_t flags) {
    uint32_t mode = vfs_open_access_mode(flags);

    if ((flags & ~(VFS_O_ACCMODE | VFS_O_CREAT)) != 0) {
        return 0;
    }

    return mode == VFS_O_RDONLY || mode == VFS_O_WRONLY ||
           mode == VFS_O_RDWR;
}

static int vfs_fat32_root_name(const char *path, char *out,
                               uint64_t out_size) {
    const char *suffix = vfs_strip_prefix(path, "/fat/");
    uint64_t i;

    if (suffix == 0 || out == 0 || out_size == 0) {
        return -1;
    }

    for (i = 0; i + 1U < out_size; i++) {
        char c = suffix[i];

        if (c == '\0') {
            break;
        }
        if (c == '/') {
            return -1;
        }
        out[i] = c;
    }

    if (suffix[i] != '\0') {
        return -1;
    }
    out[i] = '\0';
    return i == 0 ? -1 : 0;
}

static const vfs_node_t *vfs_open_dynamic_fat32_node(const char *path,
                                                     uint32_t flags) {
    char name[VFS_MAX_PATH];
    fat32_file_t file;
    fat32_fs_t *fs;

    if (vfs_fat32_root_name(path, name, sizeof(name)) != 0) {
        return 0;
    }

    fs = fat32_default_fs();
    if (fs == 0 || fs->mounted == 0) {
        return 0;
    }

    if (fat32_open_root(fs, name, &file) != 0) {
        if ((flags & VFS_O_CREAT) == 0 ||
            fat32_create(fs, name, &file) != 0) {
            return 0;
        }
    }

    if (fat32_mount_vfs_file(fs, path, name) != 0) {
        return 0;
    }

    return vfs_find(path);
}

void vfs_reset(void) {
    for (uint32_t i = 0; i < VFS_MAX_NODES; i++) {
        g_vfs_nodes[i].path = 0;
        g_vfs_nodes[i].size = 0;
        g_vfs_nodes[i].read = 0;
        g_vfs_nodes[i].write = 0;
        g_vfs_nodes[i].stat = 0;
        g_vfs_nodes[i].context = 0;
        for (uint32_t j = 0; j < VFS_MAX_PATH; j++) {
            g_vfs_paths[i][j] = '\0';
        }
    }
    for (uint32_t i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        g_open_files[i].node = 0;
        g_open_files[i].offset = 0;
        g_open_files[i].flags = VFS_O_RDONLY;
        g_open_files[i].used = 0;
    }
    for (uint32_t i = 0; i < VFS_MAX_LIST_MOUNTS; i++) {
        g_list_mounts[i].list = 0;
        g_list_mounts[i].context = 0;
        g_list_mounts[i].used = 0;
        for (uint32_t j = 0; j < VFS_MAX_PATH; j++) {
            g_list_mounts[i].path[j] = '\0';
        }
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

        if (!vfs_path_is_mountable(node->path) ||
            (node->read == 0 && node->write == 0) ||
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
        (void)vfs_copy_path(g_vfs_paths[g_vfs_node_count], nodes[i].path);
        g_vfs_nodes[g_vfs_node_count].path = g_vfs_paths[g_vfs_node_count];
        g_vfs_nodes[g_vfs_node_count].size = nodes[i].size;
        g_vfs_nodes[g_vfs_node_count].read = nodes[i].read;
        g_vfs_nodes[g_vfs_node_count].write = nodes[i].write;
        g_vfs_nodes[g_vfs_node_count].stat = nodes[i].stat;
        g_vfs_nodes[g_vfs_node_count].context = nodes[i].context;
        g_vfs_node_count++;
    }

    return 0;
}

int vfs_mount_list(const char *path, vfs_list_fn_t list, void *context) {
    if (!vfs_path_is_mountable(path) || list == 0) {
        return -1;
    }

    for (uint32_t i = 0; i < VFS_MAX_LIST_MOUNTS; i++) {
        if (g_list_mounts[i].used != 0 &&
            vfs_path_equals(g_list_mounts[i].path, path)) {
            return -1;
        }
    }

    for (uint32_t i = 0; i < VFS_MAX_LIST_MOUNTS; i++) {
        if (g_list_mounts[i].used == 0) {
            if (vfs_copy_path(g_list_mounts[i].path, path) != 0) {
                return -1;
            }
            g_list_mounts[i].list = list;
            g_list_mounts[i].context = context;
            g_list_mounts[i].used = 1;
            return 0;
        }
    }

    return -1;
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

const char *vfs_strip_prefix(const char *path, const char *prefix) {
    if (path == 0 || prefix == 0 || prefix[0] == '\0') {
        return 0;
    }

    while (*prefix != '\0') {
        if (*path != *prefix) {
            return 0;
        }
        path++;
        prefix++;
    }

    return *path == '\0' ? 0 : path;
}

int vfs_read(const char *path, uint64_t offset, uint8_t *buffer,
             uint64_t capacity, uint64_t *bytes_read) {
    const vfs_node_t *node = vfs_find(path);
    uint64_t size;
    int status;

    if (bytes_read != 0) {
        *bytes_read = 0;
    }

    if (node == 0 || node->read == 0 || buffer == 0 || bytes_read == 0 ||
        vfs_node_size(node, &size) != 0 || offset > size) {
        return -1;
    }

    status = node->read(node->context, offset, buffer, capacity, bytes_read);
    if (status != 0 ||
        !vfs_read_result_valid(offset, size, capacity, *bytes_read)) {
        *bytes_read = 0;
        return status != 0 ? status : -1;
    }

    return 0;
}

int vfs_write(const char *path, uint64_t offset, const uint8_t *buffer,
              uint64_t size, uint64_t *bytes_written) {
    const vfs_node_t *node = vfs_find(path);
    uint64_t current_size;
    int status;

    if (bytes_written != 0) {
        *bytes_written = 0;
    }

    if (node == 0 || node->write == 0 || buffer == 0 ||
        bytes_written == 0 || vfs_node_size(node, &current_size) != 0 ||
        offset > current_size) {
        return -1;
    }

    status = node->write(node->context, offset, buffer, size, bytes_written);
    if (status != 0 ||
        !vfs_write_result_valid(offset, size, *bytes_written)) {
        *bytes_written = 0;
        return status != 0 ? status : -1;
    }

    return 0;
}

int vfs_stat(const char *path, vfs_stat_t *stat) {
    const vfs_node_t *node = vfs_find(path);

    if (node == 0 || stat == 0) {
        return -1;
    }

    return vfs_node_size(node, &stat->size);
}

int vfs_list(const char *path, uint8_t *buffer, uint64_t capacity,
             uint64_t *bytes_written) {
    uint64_t out = 0;

    if (bytes_written != 0) {
        *bytes_written = 0;
    }

    if (path == 0 || buffer == 0 || bytes_written == 0 ||
        path[0] == '\0') {
        return -1;
    }

    for (uint32_t i = 0; i < VFS_MAX_LIST_MOUNTS; i++) {
        if (g_list_mounts[i].used != 0 &&
            vfs_path_equals(g_list_mounts[i].path, path)) {
            return g_list_mounts[i].list(g_list_mounts[i].context, buffer,
                                         capacity, bytes_written);
        }
    }

    if (!vfs_path_equals(path, "/")) {
        return -1;
    }

    for (uint32_t i = 0; i < g_vfs_node_count; i++) {
        const char *node_path = g_vfs_nodes[i].path;

        if (node_path == 0) {
            continue;
        }

        while (*node_path != '\0') {
            if (out >= capacity) {
                *bytes_written = out;
                return 0;
            }
            buffer[out++] = (uint8_t)*node_path++;
        }

        if (out >= capacity) {
            *bytes_written = out;
            return 0;
        }
        buffer[out++] = (uint8_t)'\n';
    }

    *bytes_written = out;
    return 0;
}

int vfs_open_flags(const char *path, uint32_t flags) {
    const vfs_node_t *node = vfs_find(path);
    uint32_t mode = vfs_open_access_mode(flags);

    if (!vfs_open_flags_valid(flags)) {
        return -1;
    }

    if (node == 0) {
        node = vfs_open_dynamic_fat32_node(path, flags);
    }

    if (node == 0 ||
        ((mode == VFS_O_RDONLY || mode == VFS_O_RDWR) &&
         node->read == 0) ||
        ((mode == VFS_O_WRONLY || mode == VFS_O_RDWR) &&
         node->write == 0)) {
        return -1;
    }

    for (uint32_t i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (g_open_files[i].used == 0) {
            g_open_files[i].node = node;
            g_open_files[i].offset = 0;
            g_open_files[i].flags = mode;
            g_open_files[i].used = 1;
            return (int)i;
        }
    }

    return -1;
}

int vfs_open(const char *path) {
    return vfs_open_flags(path, VFS_O_RDONLY);
}

int vfs_read_fd(int fd, uint8_t *buffer, uint64_t capacity,
                uint64_t *bytes_read) {
    vfs_open_file_t *file;
    int status;
    uint64_t size;

    if (bytes_read != 0) {
        *bytes_read = 0;
    }

    if (buffer == 0 || bytes_read == 0) {
        return -1;
    }

    file = vfs_fd_at(fd);
    if (file == 0 || file->node->read == 0 ||
        file->flags == VFS_O_WRONLY ||
        vfs_node_size(file->node, &size) != 0 || file->offset > size) {
        return -1;
    }

    status = file->node->read(file->node->context, file->offset, buffer,
                              capacity, bytes_read);
    if (status != 0 ||
        !vfs_read_result_valid(file->offset, size, capacity, *bytes_read)) {
        *bytes_read = 0;
        return status != 0 ? status : -1;
    }

    file->offset += *bytes_read;
    return 0;
}

int vfs_write_fd(int fd, const uint8_t *buffer, uint64_t size,
                 uint64_t *bytes_written) {
    vfs_open_file_t *file;
    int status;
    uint64_t current_size;

    if (bytes_written != 0) {
        *bytes_written = 0;
    }

    if (buffer == 0 || bytes_written == 0) {
        return -1;
    }

    file = vfs_fd_at(fd);
    if (file == 0 || file->node->write == 0 ||
        file->flags == VFS_O_RDONLY ||
        vfs_node_size(file->node, &current_size) != 0 ||
        file->offset > current_size) {
        return -1;
    }

    status = file->node->write(file->node->context, file->offset, buffer,
                               size, bytes_written);
    if (status != 0 ||
        !vfs_write_result_valid(file->offset, size, *bytes_written)) {
        *bytes_written = 0;
        return status != 0 ? status : -1;
    }

    file->offset += *bytes_written;
    return 0;
}

int vfs_seek(int fd, uint64_t offset) {
    uint64_t size;

    vfs_open_file_t *file = vfs_fd_at(fd);

    if (file == 0 || vfs_node_size(file->node, &size) != 0 ||
        offset > size) {
        return -1;
    }

    file->offset = offset;
    return 0;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= (int)VFS_MAX_OPEN_FILES ||
        g_open_files[fd].used == 0) {
        return -1;
    }

    g_open_files[fd].node = 0;
    g_open_files[fd].offset = 0;
    g_open_files[fd].flags = VFS_O_RDONLY;
    g_open_files[fd].used = 0;
    return 0;
}

int vfs_unlink(const char *path) {
    char name[VFS_MAX_PATH];
    fat32_fs_t *fs;

    if (vfs_fat32_root_name(path, name, sizeof(name)) != 0) {
        return -1;
    }
    fs = fat32_default_fs();
    if (fs == 0 || fs->mounted == 0) {
        return -1;
    }
    return fat32_delete(fs, name);
}

int vfs_rename(const char *old_path, const char *new_path) {
    char old_name[VFS_MAX_PATH];
    char new_name[VFS_MAX_PATH];
    fat32_fs_t *fs;

    if (vfs_fat32_root_name(old_path, old_name, sizeof(old_name)) != 0 ||
        vfs_fat32_root_name(new_path, new_name, sizeof(new_name)) != 0) {
        return -1;
    }
    fs = fat32_default_fs();
    if (fs == 0 || fs->mounted == 0) {
        return -1;
    }
    return fat32_rename(fs, old_name, new_name);
}
