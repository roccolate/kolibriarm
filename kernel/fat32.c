#include "kernel/fat32.h"

#include <stdint.h>

#include "kernel/vfs.h"

/*
 * Minimal FAT32 root-directory support.
 *
 * This module intentionally handles only 8.3 names in the root directory. It
 * backs the /fat VFS mount used by the kernel shell path and keeps all sector
 * I/O behind caller-supplied callbacks so host tests can use an in-memory disk.
 */

#define FAT32_ATTR_VOLUME_ID 0x08U
#define FAT32_ATTR_DIRECTORY 0x10U
#define FAT32_ATTR_LONG_NAME 0x0fU
#define FAT32_CLUSTER_MASK   0x0fffffffU
#define FAT32_CLUSTER_EOC    0x0ffffff8U
#define FAT32_CLUSTER_BAD    0x0ffffff7U
#define FAT32_MAX_VFS_FILES  8U

typedef struct {
    fat32_fs_t *fs;
    fat32_file_t file;
} fat32_vfs_file_t;

static fat32_vfs_file_t g_fat32_vfs_files[FAT32_MAX_VFS_FILES];
static vfs_node_t g_fat32_vfs_nodes[FAT32_MAX_VFS_FILES];
static uint32_t g_fat32_vfs_count;

_Static_assert(FAT32_MAX_VFS_FILES <= VFS_MAX_NODES,
               "FAT32 VFS files must fit the VFS static node table");

/*
 * Default filesystem handle for callers that don't go through a
 * mounted VFS node. Set by fat32_mount; used by vfs_unlink /
 * vfs_rename when they have to act on a path under "/fat/".
 */
static fat32_fs_t *g_fat32_default_fs;

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void put_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void put_le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static int cluster_is_eoc(uint32_t cluster) {
    return cluster >= FAT32_CLUSTER_EOC;
}

static int ascii_upper(int c) {
    if (c >= 'a' && c <= 'z') {
        return c - ('a' - 'A');
    }

    return c;
}

static void clear_short_name(uint8_t out[11]) {
    for (uint32_t i = 0; i < 11U; i++) {
        out[i] = ' ';
    }
}

static int make_short_name(const char *name, uint8_t out[11]) {
    uint32_t i = 0;
    uint32_t base = 0;
    uint32_t ext = 0;
    uint8_t in_ext = 0;

    if (name == 0 || name[0] == '\0') {
        return -1;
    }

    if (name[0] == '/') {
        name++;
        if (name[0] == '\0') {
            return -1;
        }
    }

    clear_short_name(out);
    while (name[i] != '\0') {
        int c = ascii_upper(name[i]);

        if (c == '/') {
            return -1;
        }

        if (c == '.') {
            if (in_ext != 0 || base == 0) {
                return -1;
            }
            in_ext = 1;
            i++;
            continue;
        }

        if (c <= ' ' || c == '"' || c == '*' || c == '+' || c == ',' ||
            c == ':' || c == ';' || c == '<' || c == '=' || c == '>' ||
            c == '?' || c == '[' || c == '\\' || c == ']' || c == '|') {
            return -1;
        }

        if (in_ext == 0) {
            if (base >= 8U) {
                return -1;
            }
            out[base] = (uint8_t)c;
            base++;
        } else {
            if (ext >= 3U) {
                return -1;
            }
            out[8U + ext] = (uint8_t)c;
            ext++;
        }

        i++;
    }

    if (base == 0 || (in_ext != 0 && ext == 0)) {
        return -1;
    }

    return 0;
}

static int short_name_equals(const uint8_t entry[11],
                             const uint8_t wanted[11]) {
    for (uint32_t i = 0; i < 11U; i++) {
        if (entry[i] != wanted[i]) {
            return 0;
        }
    }

    return 1;
}

static int append_list_byte(uint8_t *buffer, uint64_t capacity,
                            uint64_t *out, uint8_t value) {
    if (*out >= capacity) {
        return -1;
    }

    buffer[*out] = value;
    (*out)++;
    return 0;
}

static int append_short_name(const uint8_t entry[11], uint8_t attr,
                             uint8_t *buffer, uint64_t capacity,
                             uint64_t *out) {
    uint32_t base_end = 8U;
    uint32_t ext_end = 3U;

    while (base_end > 0U && entry[base_end - 1U] == ' ') {
        base_end--;
    }
    while (ext_end > 0U && entry[8U + ext_end - 1U] == ' ') {
        ext_end--;
    }

    for (uint32_t i = 0; i < base_end; i++) {
        if (append_list_byte(buffer, capacity, out, entry[i]) != 0) {
            return -1;
        }
    }

    if (ext_end > 0U) {
        if (append_list_byte(buffer, capacity, out, '.') != 0) {
            return -1;
        }
        for (uint32_t i = 0; i < ext_end; i++) {
            if (append_list_byte(buffer, capacity, out,
                                 entry[8U + i]) != 0) {
                return -1;
            }
        }
    }

    if ((attr & FAT32_ATTR_DIRECTORY) != 0) {
        if (append_list_byte(buffer, capacity, out, '/') != 0) {
            return -1;
        }
    }

    return append_list_byte(buffer, capacity, out, '\n');
}

static uint32_t cluster_to_lba(const fat32_fs_t *fs, uint32_t cluster) {
    return fs->data_start_lba +
           (cluster - 2U) * fs->sectors_per_cluster;
}

static int read_sector(fat32_fs_t *fs, uint32_t lba) {
    if (fs == 0 || fs->read_sector == 0 ||
        (fs->mounted != 0 && lba >= fs->total_sectors)) {
        return -1;
    }

    return fs->read_sector(fs->context, lba, fs->sector);
}

static int write_sector(fat32_fs_t *fs, uint32_t lba,
                        const uint8_t *buffer) {
    if (fs == 0 || fs->write_sector == 0 || buffer == 0 ||
        fs->mounted == 0 || lba >= fs->total_sectors) {
        return -1;
    }

    return fs->write_sector(fs->context, lba, buffer);
}

static int read_fat_entry(fat32_fs_t *fs, uint32_t cluster, uint32_t *next) {
    uint32_t fat_offset;
    uint32_t fat_sector;
    uint32_t entry_offset;
    uint32_t value;

    if (fs == 0 || next == 0 || cluster < 2U) {
        return -1;
    }

    fat_offset = cluster * 4U;
    fat_sector = fs->fat_start_lba + fat_offset / FAT32_SECTOR_SIZE;
    entry_offset = fat_offset % FAT32_SECTOR_SIZE;

    if (read_sector(fs, fat_sector) != 0) {
        return -1;
    }

    value = le32(&fs->sector[entry_offset]) & FAT32_CLUSTER_MASK;
    if (value == FAT32_CLUSTER_BAD) {
        return -1;
    }

    *next = value;
    return 0;
}

/*
 * Write a single FAT entry and mirror the change to every copy of the
 * FAT. fat32_mount requires fat_count>=1; we walk all copies so the
 * invariant holds regardless of the geometry.
 */
static int write_fat_entry(fat32_fs_t *fs, uint32_t cluster, uint32_t value) {
    uint32_t fat_offset;
    uint32_t entry_offset;
    uint32_t sector_index;

    if (fs == 0 || fs->write_sector == 0 || cluster < 2U) {
        return -1;
    }

    fat_offset = cluster * 4U;
    entry_offset = fat_offset % FAT32_SECTOR_SIZE;

    for (uint32_t copy = 0; copy < fs->fat_count; copy++) {
        sector_index = fs->fat_start_lba + copy * fs->sectors_per_fat +
                       fat_offset / FAT32_SECTOR_SIZE;
        if (read_sector(fs, sector_index) != 0) {
            return -1;
        }
        put_le32(&fs->sector[entry_offset],
                 value & FAT32_CLUSTER_MASK);
        if (write_sector(fs, sector_index, fs->sector) != 0) {
            return -1;
        }
    }

    return 0;
}

/*
 * Find the first free cluster (FAT entry == 0) starting from cluster 2.
 * Returns the cluster number on success, 0 on failure. The caller is
 * responsible for marking the entry with write_fat_entry after linking
 * it into the chain.
 */
static uint32_t find_free_cluster(fat32_fs_t *fs) {
    uint32_t fat_bytes = fs->sectors_per_fat * FAT32_SECTOR_SIZE;
    uint32_t total_clusters = fat_bytes / 4U;

    for (uint32_t cluster = 2; cluster < total_clusters + 2U; cluster++) {
        uint32_t value = 0;
        if (read_fat_entry(fs, cluster, &value) != 0) {
            return 0;
        }
        if (value == 0) {
            return cluster;
        }
    }

    return 0;
}

/*
 * Allocate a free cluster and link it after prev_cluster. Pass
 * prev_cluster == 0 to allocate a new chain head (the new cluster
 * gets FAT32_CLUSTER_EOC). Returns the new cluster number, or 0 on
 * failure (no free clusters or write failed).
 */
static uint32_t allocate_cluster(fat32_fs_t *fs, uint32_t prev_cluster) {
    uint32_t new_cluster = find_free_cluster(fs);
    uint32_t prev_value;

    if (new_cluster == 0) {
        return 0;
    }

    if (prev_cluster != 0) {
        if (read_fat_entry(fs, prev_cluster, &prev_value) != 0) {
            return 0;
        }
        if (!cluster_is_eoc(prev_value)) {
            return 0;
        }
        if (write_fat_entry(fs, prev_cluster, new_cluster) != 0) {
            return 0;
        }
        if (write_fat_entry(fs, new_cluster, FAT32_CLUSTER_EOC) != 0) {
            return 0;
        }
    } else {
        if (write_fat_entry(fs, new_cluster, FAT32_CLUSTER_EOC) != 0) {
            return 0;
        }
    }

    return new_cluster;
}

static int advance_or_allocate_cluster(fat32_fs_t *fs, fat32_file_t *file,
                                       uint64_t cluster_size,
                                       uint32_t *cluster) {
    uint32_t next;

    if (fs == 0 || file == 0 || cluster == 0 || *cluster < 2U ||
        read_fat_entry(fs, *cluster, &next) != 0) {
        return -1;
    }

    if (cluster_is_eoc(next)) {
        uint32_t new_cluster;

        if (cluster_size > 0xffffffffULL ||
            file->capacity > 0xffffffffU - (uint32_t)cluster_size) {
            return -1;
        }

        new_cluster = allocate_cluster(fs, *cluster);
        if (new_cluster == 0) {
            return -1;
        }
        file->capacity += (uint32_t)cluster_size;
        *cluster = new_cluster;
        return 0;
    }

    if (next < 2U) {
        return -1;
    }

    *cluster = next;
    return 0;
}

/*
 * Free the cluster chain starting at first_cluster by walking the
 * FAT links and writing 0 to every entry. The first_cluster entry
 * itself is also zeroed.
 */
static int free_cluster_chain(fat32_fs_t *fs, uint32_t first_cluster) {
    uint32_t cluster = first_cluster;

    if (first_cluster < 2U) {
        return -1;
    }

    while (cluster >= 2U && !cluster_is_eoc(cluster)) {
        uint32_t next;
        if (read_fat_entry(fs, cluster, &next) != 0) {
            return -1;
        }
        if (write_fat_entry(fs, cluster, 0) != 0) {
            return -1;
        }
        cluster = next;
    }

    return 0;
}

static int file_chain_capacity(fat32_fs_t *fs, uint32_t first_cluster,
                               uint32_t *capacity) {
    uint64_t total = 0;
    uint32_t cluster = first_cluster;
    uint32_t cluster_size;

    if (fs == 0 || capacity == 0 || first_cluster < 2U) {
        return -1;
    }

    cluster_size = fs->sectors_per_cluster * FAT32_SECTOR_SIZE;
    while (cluster >= 2U && !cluster_is_eoc(cluster)) {
        total += cluster_size;
        if (total > 0xffffffffULL) {
            return -1;
        }
        if (read_fat_entry(fs, cluster, &cluster) != 0) {
            return -1;
        }
    }

    *capacity = (uint32_t)total;
    return 0;
}

static void clear_vfs_slot(uint32_t index) {
    if (index >= FAT32_MAX_VFS_FILES) {
        return;
    }

    g_fat32_vfs_files[index].fs = 0;
    g_fat32_vfs_files[index].file.first_cluster = 0;
    g_fat32_vfs_files[index].file.dir_lba = 0;
    g_fat32_vfs_files[index].file.dir_offset = 0;
    g_fat32_vfs_files[index].file.capacity = 0;
    g_fat32_vfs_files[index].file.size = 0;
    g_fat32_vfs_nodes[index].path = 0;
    g_fat32_vfs_nodes[index].size = 0;
    g_fat32_vfs_nodes[index].read = 0;
    g_fat32_vfs_nodes[index].write = 0;
    g_fat32_vfs_nodes[index].stat = 0;
    g_fat32_vfs_nodes[index].context = 0;
}

static int fat32_vfs_read(void *context, uint64_t offset, uint8_t *buffer,
                          uint64_t capacity, uint64_t *bytes_read) {
    fat32_vfs_file_t *mounted = (fat32_vfs_file_t *)context;

    if (mounted == 0 || mounted->fs == 0) {
        return -1;
    }

    return fat32_read(mounted->fs, &mounted->file, offset, buffer, capacity,
                      bytes_read);
}

static int fat32_vfs_write(void *context, uint64_t offset,
                           const uint8_t *buffer, uint64_t size,
                           uint64_t *bytes_written) {
    fat32_vfs_file_t *mounted = (fat32_vfs_file_t *)context;

    if (mounted == 0 || mounted->fs == 0) {
        return -1;
    }

    return fat32_write(mounted->fs, &mounted->file, offset, buffer, size,
                       bytes_written);
}

static int fat32_vfs_stat(void *context, vfs_stat_t *stat) {
    fat32_vfs_file_t *mounted = (fat32_vfs_file_t *)context;

    if (mounted == 0 || mounted->fs == 0 || stat == 0) {
        return -1;
    }

    stat->size = mounted->file.size;
    return 0;
}

static int fat32_vfs_list_root(void *context, uint8_t *buffer,
                               uint64_t capacity, uint64_t *bytes_written) {
    return fat32_list_root((fat32_fs_t *)context, buffer, capacity,
                           bytes_written);
}

int fat32_mount(fat32_fs_t *fs, fat32_read_sector_fn_t read_sector_cb,
                void *context) {
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved;
    uint32_t fat_count;
    uint32_t root_entries;
    uint32_t total_sectors;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;
    uint32_t data_start;

    if (fs == 0 || read_sector_cb == 0) {
        return -1;
    }

    g_fat32_default_fs = 0;
    fs->read_sector = read_sector_cb;
    fs->write_sector = 0;
    fs->context = context;
    fs->mounted = 0;

    if (read_sector(fs, 0) != 0) {
        return -1;
    }

    bytes_per_sector = le16(&fs->sector[11]);
    sectors_per_cluster = fs->sector[13];
    reserved = le16(&fs->sector[14]);
    fat_count = fs->sector[16];
    root_entries = le16(&fs->sector[17]);
    total_sectors = le16(&fs->sector[19]);
    if (total_sectors == 0) {
        total_sectors = le32(&fs->sector[32]);
    }
    sectors_per_fat = le32(&fs->sector[36]);
    root_cluster = le32(&fs->sector[44]);

    if (bytes_per_sector != FAT32_SECTOR_SIZE ||
        sectors_per_cluster == 0 ||
        reserved == 0 ||
        fat_count == 0 ||
        root_entries != 0 ||
        total_sectors == 0 ||
        sectors_per_fat == 0 ||
        root_cluster < 2U) {
        return -1;
    }

    data_start = reserved + fat_count * sectors_per_fat;
    if (data_start >= total_sectors) {
        return -1;
    }

    fs->bytes_per_sector = bytes_per_sector;
    fs->sectors_per_cluster = sectors_per_cluster;
    fs->reserved_sectors = reserved;
    fs->fat_count = fat_count;
    fs->sectors_per_fat = sectors_per_fat;
    fs->total_sectors = total_sectors;
    fs->fat_start_lba = reserved;
    fs->data_start_lba = data_start;
    fs->root_cluster = root_cluster;
    fs->mounted = 1;
    g_fat32_default_fs = fs;
    return 0;
}

void fat32_set_write_sector(fat32_fs_t *fs,
                            fat32_write_sector_fn_t write_sector_cb) {
    if (fs != 0) {
        fs->write_sector = write_sector_cb;
    }
}

fat32_fs_t *fat32_default_fs(void) {
    return g_fat32_default_fs;
}

int fat32_open_root(fat32_fs_t *fs, const char *name, fat32_file_t *file) {
    uint8_t wanted[11];
    uint32_t cluster;

    if (fs == 0 || fs->mounted == 0 || file == 0 ||
        make_short_name(name, wanted) != 0) {
        return -1;
    }

    cluster = fs->root_cluster;
    while (cluster >= 2U && !cluster_is_eoc(cluster)) {
        for (uint32_t sector = 0; sector < fs->sectors_per_cluster; sector++) {
            uint32_t lba = cluster_to_lba(fs, cluster) + sector;

            if (read_sector(fs, lba) != 0) {
                return -1;
            }

            for (uint32_t offset = 0; offset < FAT32_SECTOR_SIZE; offset += 32U) {
                const uint8_t *entry = &fs->sector[offset];
                uint8_t attr = entry[11];
                uint32_t first_cluster;

                if (entry[0] == 0x00U) {
                    return -1;
                }

                if (entry[0] == 0xe5U || attr == FAT32_ATTR_LONG_NAME ||
                    (attr & FAT32_ATTR_DIRECTORY) != 0) {
                    continue;
                }

                if (!short_name_equals(entry, wanted)) {
                    continue;
                }

                first_cluster = ((uint32_t)le16(&entry[20]) << 16) |
                                le16(&entry[26]);
                file->first_cluster = first_cluster;
                file->dir_lba = lba;
                file->dir_offset = offset;
                file->capacity = 0;
                file->size = le32(&entry[28]);
                if (file->size != 0 && first_cluster < 2U) {
                    return -1;
                }
                if (first_cluster >= 2U &&
                    file_chain_capacity(fs, first_cluster,
                                        &file->capacity) != 0) {
                    return -1;
                }
                return 0;
            }
        }

        if (read_fat_entry(fs, cluster, &cluster) != 0) {
            return -1;
        }
    }

    return -1;
}

int fat32_list_root(fat32_fs_t *fs, uint8_t *buffer, uint64_t capacity,
                    uint64_t *bytes_written) {
    uint64_t out = 0;
    uint32_t cluster;

    if (bytes_written != 0) {
        *bytes_written = 0;
    }

    if (fs == 0 || fs->mounted == 0 || buffer == 0 || bytes_written == 0) {
        return -1;
    }

    cluster = fs->root_cluster;
    while (cluster >= 2U && !cluster_is_eoc(cluster)) {
        for (uint32_t sector = 0; sector < fs->sectors_per_cluster; sector++) {
            uint32_t lba = cluster_to_lba(fs, cluster) + sector;

            if (read_sector(fs, lba) != 0) {
                return -1;
            }

            for (uint32_t offset = 0; offset < FAT32_SECTOR_SIZE;
                 offset += 32U) {
                const uint8_t *entry = &fs->sector[offset];
                uint8_t attr = entry[11];

                if (entry[0] == 0x00U) {
                    *bytes_written = out;
                    return 0;
                }

                if (entry[0] == 0xe5U || attr == FAT32_ATTR_LONG_NAME ||
                    (attr & FAT32_ATTR_VOLUME_ID) != 0) {
                    continue;
                }

                if (append_short_name(entry, attr, buffer, capacity,
                                      &out) != 0) {
                    *bytes_written = out;
                    return 0;
                }
            }
        }

        if (read_fat_entry(fs, cluster, &cluster) != 0) {
            return -1;
        }
    }

    *bytes_written = out;
    return 0;
}

int fat32_read(fat32_fs_t *fs, const fat32_file_t *file, uint64_t offset,
               uint8_t *buffer, uint64_t capacity, uint64_t *bytes_read) {
    uint64_t cluster_size;
    uint64_t remaining;
    uint64_t copied = 0;
    uint64_t skip;
    uint32_t cluster;

    if (bytes_read != 0) {
        *bytes_read = 0;
    }

    if (fs == 0 || fs->mounted == 0 || file == 0 || buffer == 0 ||
        bytes_read == 0 || offset > file->size) {
        return -1;
    }

    remaining = file->size - offset;
    if (remaining > capacity) {
        remaining = capacity;
    }

    if (remaining == 0) {
        *bytes_read = 0;
        return 0;
    }

    if (file->first_cluster < 2U) {
        return -1;
    }

    cluster = file->first_cluster;
    cluster_size = (uint64_t)fs->sectors_per_cluster * FAT32_SECTOR_SIZE;
    skip = offset;

    while (skip >= cluster_size) {
        skip -= cluster_size;
        if (read_fat_entry(fs, cluster, &cluster) != 0 ||
            cluster_is_eoc(cluster) || cluster < 2U) {
            return -1;
        }
    }

    while (remaining > 0) {
        uint64_t cluster_offset = skip;

        for (uint32_t sector = 0;
             sector < fs->sectors_per_cluster && remaining > 0;
             sector++) {
            uint64_t sector_offset = 0;
            uint64_t count;
            uint32_t lba;

            if (cluster_offset >= FAT32_SECTOR_SIZE) {
                cluster_offset -= FAT32_SECTOR_SIZE;
                continue;
            }

            lba = cluster_to_lba(fs, cluster) + sector;
            if (read_sector(fs, lba) != 0) {
                return -1;
            }

            sector_offset = cluster_offset;
            count = FAT32_SECTOR_SIZE - sector_offset;
            if (count > remaining) {
                count = remaining;
            }

            for (uint64_t i = 0; i < count; i++) {
                buffer[copied + i] = fs->sector[sector_offset + i];
            }

            copied += count;
            remaining -= count;
            cluster_offset = 0;
        }

        skip = 0;
        if (remaining > 0) {
            if (read_fat_entry(fs, cluster, &cluster) != 0 ||
                cluster_is_eoc(cluster) || cluster < 2U) {
                return -1;
            }
        }
    }

    *bytes_read = copied;
    return 0;
}

int fat32_write(fat32_fs_t *fs, fat32_file_t *file, uint64_t offset,
                const uint8_t *buffer, uint64_t size,
                uint64_t *bytes_written) {
    uint64_t cluster_size;
    uint64_t remaining;
    uint64_t copied = 0;
    uint64_t skip;
    uint32_t cluster;
    uint32_t new_size;

    if (bytes_written != 0) {
        *bytes_written = 0;
    }

    if (fs == 0 || fs->mounted == 0 || fs->write_sector == 0 ||
        file == 0 || buffer == 0 || bytes_written == 0 ||
        offset > file->size || offset > file->capacity ||
        file->dir_lba >= fs->total_sectors ||
        file->dir_offset + 32U > FAT32_SECTOR_SIZE) {
        return -1;
    }

    /*
     * Truncate path: writing 0 bytes just shrinks the size field.
     * Directory metadata still gets the new size, no FAT changes.
     */
    if (size == 0) {
        new_size = (uint32_t)offset;
        if (read_sector(fs, file->dir_lba) != 0) {
            return -1;
        }
        put_le32(&fs->sector[file->dir_offset + 28U], new_size);
        if (write_sector(fs, file->dir_lba, fs->sector) != 0) {
            return -1;
        }
        file->size = new_size;
        return 0;
    }

    remaining = size;

    /*
     * First cluster bootstrap. An empty file (first_cluster == 0)
     * gets a brand-new single-cluster chain before any data is
     * written. The directory entry's first-cluster field is patched
     * once the cluster is allocated.
     */
    if (file->first_cluster < 2U) {
        uint32_t head = allocate_cluster(fs, 0);
        if (head == 0) {
            return -1;
        }
        file->first_cluster = head;
        file->capacity = (uint32_t)fs->sectors_per_cluster * FAT32_SECTOR_SIZE;

        if (read_sector(fs, file->dir_lba) != 0) {
            return -1;
        }
        put_le16(&fs->sector[file->dir_offset + 20U],
                 (uint16_t)(head >> 16));
        put_le16(&fs->sector[file->dir_offset + 26U],
                 (uint16_t)(head & 0xffffU));
        if (write_sector(fs, file->dir_lba, fs->sector) != 0) {
            return -1;
        }
    }

    cluster = file->first_cluster;
    cluster_size = (uint64_t)fs->sectors_per_cluster * FAT32_SECTOR_SIZE;
    skip = offset;

    while (skip >= cluster_size) {
        skip -= cluster_size;
        if (advance_or_allocate_cluster(fs, file, cluster_size,
                                        &cluster) != 0) {
            return -1;
        }
    }

    while (remaining > 0) {
        uint64_t cluster_offset = skip;

        if (cluster < 2U || cluster_is_eoc(cluster)) {
            return -1;
        }

        for (uint32_t sector = 0;
             sector < fs->sectors_per_cluster && remaining > 0;
             sector++) {
            uint64_t sector_offset = 0;
            uint64_t count;
            uint32_t lba;

            if (cluster_offset >= FAT32_SECTOR_SIZE) {
                cluster_offset -= FAT32_SECTOR_SIZE;
                continue;
            }

            lba = cluster_to_lba(fs, cluster) + sector;
            if (read_sector(fs, lba) != 0) {
                return -1;
            }

            for (uint32_t i = 0; i < FAT32_SECTOR_SIZE; i++) {
                fs->write_sector_buffer[i] = fs->sector[i];
            }

            sector_offset = cluster_offset;
            count = FAT32_SECTOR_SIZE - sector_offset;
            if (count > remaining) {
                count = remaining;
            }

            for (uint64_t i = 0; i < count; i++) {
                fs->write_sector_buffer[sector_offset + i] =
                    buffer[copied + i];
            }

            if (write_sector(fs, lba, fs->write_sector_buffer) != 0) {
                return -1;
            }

            copied += count;
            remaining -= count;
            cluster_offset = 0;
        }

        skip = 0;
        if (remaining > 0) {
            if (advance_or_allocate_cluster(fs, file, cluster_size,
                                            &cluster) != 0) {
                return -1;
            }
        }
    }

    new_size = (uint32_t)(offset + copied);
    if (read_sector(fs, file->dir_lba) != 0) {
        return -1;
    }
    put_le32(&fs->sector[file->dir_offset + 28U], new_size);
    if (write_sector(fs, file->dir_lba, fs->sector) != 0) {
        return -1;
    }

    file->size = new_size;
    *bytes_written = copied;
    return 0;
}

/*
 * Find an empty directory slot inside the root directory chain and
 * return its (lba, offset). Returns 0 on success and -1 if every
 * slot in every cluster is occupied or a sector read fails.
 *
 * The 0x00 sentinel entry marks "no more entries"; once we hit it,
 * we can use that slot directly without scanning further.
 */
static int find_free_dir_entry(fat32_fs_t *fs, uint32_t *out_lba,
                              uint32_t *out_offset) {
    uint32_t cluster = fs->root_cluster;

    while (cluster >= 2U && !cluster_is_eoc(cluster)) {
        for (uint32_t sector = 0; sector < fs->sectors_per_cluster;
             sector++) {
            uint32_t lba = cluster_to_lba(fs, cluster) + sector;
            if (read_sector(fs, lba) != 0) {
                return -1;
            }
            for (uint32_t offset = 0; offset < FAT32_SECTOR_SIZE;
                 offset += 32U) {
                uint8_t first = fs->sector[offset];
                if (first == 0x00U || first == 0xe5U) {
                    *out_lba = lba;
                    *out_offset = offset;
                    return 0;
                }
            }
        }
        if (read_fat_entry(fs, cluster, &cluster) != 0) {
            return -1;
        }
    }

    return -1;
}

static void write_dir_entry(uint8_t *sector, uint32_t offset,
                            const uint8_t short_name[11], uint8_t attr,
                            uint32_t first_cluster, uint32_t size) {
    uint8_t *entry = &sector[offset];
    for (uint32_t i = 0; i < 11U; i++) {
        entry[i] = short_name[i];
    }
    entry[11] = attr;
    entry[12] = 0;
    entry[13] = 0;
    put_le16(&entry[14], 0);
    put_le16(&entry[16], 0);
    put_le16(&entry[18], 0);
    put_le16(&entry[20], (uint16_t)(first_cluster >> 16));
    put_le16(&entry[22], 0);
    put_le16(&entry[24], 0);
    put_le16(&entry[26], (uint16_t)(first_cluster & 0xffffU));
    put_le32(&entry[28], size);
}

int fat32_create(fat32_fs_t *fs, const char *name, fat32_file_t *file) {
    uint8_t short_name[11];
    uint32_t dir_lba = 0;
    uint32_t dir_offset = 0;
    uint32_t new_cluster;

    if (fs == 0 || fs->mounted == 0 || fs->write_sector == 0 || file == 0 ||
        make_short_name(name, short_name) != 0) {
        return -1;
    }

    if (fat32_open_root(fs, name, file) == 0) {
        return -1; /* already exists */
    }

    if (find_free_dir_entry(fs, &dir_lba, &dir_offset) != 0) {
        return -1;
    }

    new_cluster = allocate_cluster(fs, 0);
    if (new_cluster == 0) {
        return -1;
    }

    if (read_sector(fs, dir_lba) != 0) {
        return -1;
    }
    write_dir_entry(fs->sector, dir_offset, short_name, 0x20U, new_cluster, 0);
    if (write_sector(fs, dir_lba, fs->sector) != 0) {
        return -1;
    }

    /*
     * Zero the cluster so reads return EOF (size=0) cleanly even
     * before any write.
     */
    uint32_t lba = cluster_to_lba(fs, new_cluster);
    for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
        for (uint32_t i = 0; i < FAT32_SECTOR_SIZE; i++) {
            fs->write_sector_buffer[i] = 0;
        }
        if (write_sector(fs, lba + s, fs->write_sector_buffer) != 0) {
            return -1;
        }
    }

    file->first_cluster = new_cluster;
    file->dir_lba = dir_lba;
    file->dir_offset = dir_offset;
    file->capacity = (uint32_t)fs->sectors_per_cluster * FAT32_SECTOR_SIZE;
    file->size = 0;
    return 0;
}

int fat32_delete(fat32_fs_t *fs, const char *name) {
    fat32_file_t file;
    uint8_t short_name[11];

    if (fs == 0 || fs->mounted == 0 || fs->write_sector == 0 ||
        make_short_name(name, short_name) != 0) {
        return -1;
    }

    if (fat32_open_root(fs, name, &file) != 0) {
        return -1;
    }

    /*
     * FAT32 permits an empty regular file to have first_cluster == 0. In that
     * case there is no chain to release; deleting the directory entry is
     * enough. Non-empty files with first_cluster < 2 are rejected earlier by
     * fat32_open_root.
     */
    if (file.first_cluster >= 2U &&
        free_cluster_chain(fs, file.first_cluster) != 0) {
        return -1;
    }

    if (read_sector(fs, file.dir_lba) != 0) {
        return -1;
    }
    fs->sector[file.dir_offset] = 0xe5U;
    if (write_sector(fs, file.dir_lba, fs->sector) != 0) {
        return -1;
    }

    return 0;
}

int fat32_rename(fat32_fs_t *fs, const char *old_name, const char *new_name) {
    fat32_file_t file;
    fat32_file_t existing;
    uint8_t new_short[11];

    if (fs == 0 || fs->mounted == 0 || fs->write_sector == 0 ||
        make_short_name(new_name, new_short) != 0) {
        return -1;
    }

    if (fat32_open_root(fs, old_name, &file) != 0) {
        return -1;
    }

    /* Reject the rename if the destination already exists. */
    if (fat32_open_root(fs, new_name, &existing) == 0) {
        return -1;
    }

    if (read_sector(fs, file.dir_lba) != 0) {
        return -1;
    }
    uint8_t *entry = &fs->sector[file.dir_offset];
    for (uint32_t i = 0; i < 11U; i++) {
        entry[i] = new_short[i];
    }
    if (write_sector(fs, file.dir_lba, fs->sector) != 0) {
        return -1;
    }

    return 0;
}

void fat32_vfs_reset(void) {
    for (uint32_t i = 0; i < FAT32_MAX_VFS_FILES; i++) {
        clear_vfs_slot(i);
    }
    g_fat32_vfs_count = 0;
}

int fat32_mount_vfs_root(fat32_fs_t *fs, const char *path) {
    if (fs == 0 || fs->mounted == 0 || path == 0 || path[0] != '/') {
        return -1;
    }

    return vfs_mount_list(path, fat32_vfs_list_root, fs);
}

int fat32_mount_vfs_file(fat32_fs_t *fs, const char *path,
                         const char *name) {
    uint32_t index = g_fat32_vfs_count;

    if (fs == 0 || fs->mounted == 0 || path == 0 || path[0] != '/' ||
        name == 0 || index >= FAT32_MAX_VFS_FILES) {
        return -1;
    }

    clear_vfs_slot(index);
    if (fat32_open_root(fs, name, &g_fat32_vfs_files[index].file) != 0) {
        return -1;
    }

    g_fat32_vfs_files[index].fs = fs;
    g_fat32_vfs_nodes[index].path = path;
    g_fat32_vfs_nodes[index].size = g_fat32_vfs_files[index].file.size;
    g_fat32_vfs_nodes[index].read = fat32_vfs_read;
    g_fat32_vfs_nodes[index].write = fat32_vfs_write;
    g_fat32_vfs_nodes[index].stat = fat32_vfs_stat;
    g_fat32_vfs_nodes[index].context = &g_fat32_vfs_files[index];

    if (vfs_mount_static(&g_fat32_vfs_nodes[index], 1) != 0) {
        clear_vfs_slot(index);
        return -1;
    }

    g_fat32_vfs_count++;
    return 0;
}
