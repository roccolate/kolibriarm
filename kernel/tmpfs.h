#ifndef KOLIBRIARM_KERNEL_TMPFS_H
#define KOLIBRIARM_KERNEL_TMPFS_H

#include <stdint.h>

#define TMPFS_MAX_FILES 8U
#define TMPFS_MAX_FILE_SIZE 256U
#define TMPFS_MAX_NAME 32U

/*
 * Fixed-size in-memory filesystem used during kernel boot.
 *
 * File names are copied into tmpfs-owned storage and file contents live in a
 * per-slot static byte array. There is no dynamic allocation and writes clamp
 * at TMPFS_MAX_FILE_SIZE.
 */

typedef struct {
    uint64_t size;
} tmpfs_stat_t;

void tmpfs_reset(void);
int tmpfs_create(const char *name);
int tmpfs_delete(const char *name);
int tmpfs_write(const char *name, uint64_t offset, const uint8_t *buffer,
                uint64_t size, uint64_t *bytes_written);
int tmpfs_read(const char *name, uint64_t offset, uint8_t *buffer,
               uint64_t capacity, uint64_t *bytes_read);
int tmpfs_stat(const char *name, tmpfs_stat_t *stat);
int tmpfs_mount_vfs(const char *path, const char *name);

#endif
