#ifndef KOLIBRIARM_KERNEL_VFS_H
#define KOLIBRIARM_KERNEL_VFS_H

#include <stdint.h>

#define VFS_MAX_NODES 8U
#define VFS_MAX_OPEN_FILES 8U

typedef int (*vfs_read_fn_t)(void *context, uint64_t offset, uint8_t *buffer,
                             uint64_t capacity, uint64_t *bytes_read);

typedef struct {
    const char *path;
    uint64_t size;
    vfs_read_fn_t read;
    void *context;
} vfs_node_t;

typedef struct {
    uint64_t size;
} vfs_stat_t;

void vfs_reset(void);
int vfs_mount_static(const vfs_node_t *nodes, uint32_t count);
const vfs_node_t *vfs_find(const char *path);
int vfs_read(const char *path, uint64_t offset, uint8_t *buffer,
             uint64_t capacity, uint64_t *bytes_read);
int vfs_stat(const char *path, vfs_stat_t *stat);
int vfs_open(const char *path);
int vfs_read_fd(int fd, uint8_t *buffer, uint64_t capacity,
                uint64_t *bytes_read);
int vfs_seek(int fd, uint64_t offset);
int vfs_close(int fd);

#endif
