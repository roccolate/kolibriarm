#ifndef KOLIBRIARM_KERNEL_VFS_H
#define KOLIBRIARM_KERNEL_VFS_H

#include <stdint.h>

#define VFS_MAX_NODES 16U
#define VFS_MAX_OPEN_FILES 8U
#define VFS_MAX_PATH 64U

#define VFS_O_RDONLY 0U
#define VFS_O_WRONLY 1U
#define VFS_O_RDWR   2U

typedef struct {
    uint64_t size;
} vfs_stat_t;

typedef int (*vfs_read_fn_t)(void *context, uint64_t offset, uint8_t *buffer,
                             uint64_t capacity, uint64_t *bytes_read);
typedef int (*vfs_write_fn_t)(void *context, uint64_t offset,
                              const uint8_t *buffer, uint64_t size,
                              uint64_t *bytes_written);
typedef int (*vfs_stat_fn_t)(void *context, vfs_stat_t *stat);
typedef int (*vfs_list_fn_t)(void *context, uint8_t *buffer,
                             uint64_t capacity, uint64_t *bytes_written);

typedef struct {
    const char *path;
    uint64_t size;
    vfs_read_fn_t read;
    vfs_write_fn_t write;
    vfs_stat_fn_t stat;
    void *context;
} vfs_node_t;

void vfs_reset(void);
int vfs_mount_static(const vfs_node_t *nodes, uint32_t count);
int vfs_mount_list(const char *path, vfs_list_fn_t list, void *context);
const vfs_node_t *vfs_find(const char *path);
int vfs_read(const char *path, uint64_t offset, uint8_t *buffer,
             uint64_t capacity, uint64_t *bytes_read);
int vfs_write(const char *path, uint64_t offset, const uint8_t *buffer,
              uint64_t size, uint64_t *bytes_written);
int vfs_stat(const char *path, vfs_stat_t *stat);
int vfs_list(const char *path, uint8_t *buffer, uint64_t capacity,
             uint64_t *bytes_written);
int vfs_open(const char *path);
int vfs_open_flags(const char *path, uint32_t flags);
int vfs_read_fd(int fd, uint8_t *buffer, uint64_t capacity,
                uint64_t *bytes_read);
int vfs_write_fd(int fd, const uint8_t *buffer, uint64_t size,
                 uint64_t *bytes_written);
int vfs_seek(int fd, uint64_t offset);
int vfs_close(int fd);

#endif
