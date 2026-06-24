#ifndef KOLIBRIARM_KERNEL_FAT32_H
#define KOLIBRIARM_KERNEL_FAT32_H

#include <stdint.h>

#define FAT32_SECTOR_SIZE 512U

typedef int (*fat32_read_sector_fn_t)(void *context, uint32_t lba,
                                      uint8_t *buffer);
typedef int (*fat32_write_sector_fn_t)(void *context, uint32_t lba,
                                       const uint8_t *buffer);

typedef struct {
    fat32_read_sector_fn_t read_sector;
    fat32_write_sector_fn_t write_sector;
    void *context;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t fat_count;
    uint32_t sectors_per_fat;
    uint32_t total_sectors;
    uint32_t fat_start_lba;
    uint32_t data_start_lba;
    uint32_t root_cluster;
    uint8_t sector[FAT32_SECTOR_SIZE];
    uint8_t write_sector_buffer[FAT32_SECTOR_SIZE];
    uint8_t mounted;
} fat32_fs_t;

typedef struct {
    uint32_t first_cluster;
    uint32_t dir_lba;
    uint32_t dir_offset;
    uint32_t capacity;
    uint32_t size;
} fat32_file_t;

int fat32_mount(fat32_fs_t *fs, fat32_read_sector_fn_t read_sector,
                void *context);
void fat32_set_write_sector(fat32_fs_t *fs,
                            fat32_write_sector_fn_t write_sector);
fat32_fs_t *fat32_default_fs(void);
int fat32_open_root(fat32_fs_t *fs, const char *name, fat32_file_t *file);
int fat32_list_root(fat32_fs_t *fs, uint8_t *buffer, uint64_t capacity,
                    uint64_t *bytes_written);
int fat32_read(fat32_fs_t *fs, const fat32_file_t *file, uint64_t offset,
               uint8_t *buffer, uint64_t capacity, uint64_t *bytes_read);
int fat32_write(fat32_fs_t *fs, fat32_file_t *file, uint64_t offset,
                const uint8_t *buffer, uint64_t size,
                uint64_t *bytes_written);
int fat32_create(fat32_fs_t *fs, const char *name, fat32_file_t *file);
int fat32_delete(fat32_fs_t *fs, const char *name);
int fat32_rename(fat32_fs_t *fs, const char *old_name,
                 const char *new_name);
void fat32_vfs_reset(void);
int fat32_mount_vfs_root(fat32_fs_t *fs, const char *path);
int fat32_mount_vfs_file(fat32_fs_t *fs, const char *path,
                         const char *name);

#endif
