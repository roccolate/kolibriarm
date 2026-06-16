#include "kernel/tmpfs.h"

#include <stdint.h>

typedef struct {
    char name[TMPFS_MAX_NAME];
    uint8_t data[TMPFS_MAX_FILE_SIZE];
    uint64_t size;
    uint8_t used;
} tmpfs_file_t;

static tmpfs_file_t g_tmpfs_files[TMPFS_MAX_FILES];

static int tmpfs_name_equals(const char *left, const char *right) {
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

static tmpfs_file_t *tmpfs_find_file(const char *name) {
    if (name == 0 || name[0] == '\0') {
        return 0;
    }

    for (uint32_t i = 0; i < TMPFS_MAX_FILES; i++) {
        if (g_tmpfs_files[i].used != 0 &&
            tmpfs_name_equals(g_tmpfs_files[i].name, name)) {
            return &g_tmpfs_files[i];
        }
    }

    return 0;
}

static int tmpfs_copy_name(char dest[TMPFS_MAX_NAME], const char *name) {
    uint32_t i = 0;

    if (dest == 0 || name == 0 || name[0] == '\0') {
        return -1;
    }

    while (name[i] != '\0') {
        if (i + 1U >= TMPFS_MAX_NAME) {
            return -1;
        }

        dest[i] = name[i];
        i++;
    }

    dest[i] = '\0';
    return 0;
}

void tmpfs_reset(void) {
    for (uint32_t i = 0; i < TMPFS_MAX_FILES; i++) {
        g_tmpfs_files[i].size = 0;
        g_tmpfs_files[i].used = 0;
        for (uint32_t j = 0; j < TMPFS_MAX_NAME; j++) {
            g_tmpfs_files[i].name[j] = '\0';
        }
        for (uint32_t j = 0; j < TMPFS_MAX_FILE_SIZE; j++) {
            g_tmpfs_files[i].data[j] = 0;
        }
    }
}

int tmpfs_create(const char *name) {
    if (name == 0 || name[0] == '\0' || tmpfs_find_file(name) != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < TMPFS_MAX_FILES; i++) {
        if (g_tmpfs_files[i].used == 0) {
            if (tmpfs_copy_name(g_tmpfs_files[i].name, name) != 0) {
                return -1;
            }
            g_tmpfs_files[i].size = 0;
            g_tmpfs_files[i].used = 1;
            return 0;
        }
    }

    return -1;
}

int tmpfs_delete(const char *name) {
    tmpfs_file_t *file = tmpfs_find_file(name);

    if (file == 0) {
        return -1;
    }

    for (uint32_t i = 0; i < TMPFS_MAX_NAME; i++) {
        file->name[i] = '\0';
    }
    file->size = 0;
    file->used = 0;
    return 0;
}

int tmpfs_write(const char *name, uint64_t offset, const uint8_t *buffer,
                uint64_t size, uint64_t *bytes_written) {
    tmpfs_file_t *file = tmpfs_find_file(name);
    uint64_t count;

    if (bytes_written != 0) {
        *bytes_written = 0;
    }

    if (file == 0 || buffer == 0 || bytes_written == 0 ||
        offset > TMPFS_MAX_FILE_SIZE) {
        return -1;
    }

    count = size;
    if (count > TMPFS_MAX_FILE_SIZE - offset) {
        count = TMPFS_MAX_FILE_SIZE - offset;
    }

    for (uint64_t i = 0; i < count; i++) {
        file->data[offset + i] = buffer[i];
    }

    if (offset + count > file->size) {
        file->size = offset + count;
    }

    *bytes_written = count;
    return 0;
}

int tmpfs_read(const char *name, uint64_t offset, uint8_t *buffer,
               uint64_t capacity, uint64_t *bytes_read) {
    tmpfs_file_t *file = tmpfs_find_file(name);
    uint64_t count;

    if (bytes_read != 0) {
        *bytes_read = 0;
    }

    if (file == 0 || buffer == 0 || bytes_read == 0 || offset > file->size) {
        return -1;
    }

    count = capacity;
    if (count > file->size - offset) {
        count = file->size - offset;
    }

    for (uint64_t i = 0; i < count; i++) {
        buffer[i] = file->data[offset + i];
    }

    *bytes_read = count;
    return 0;
}

int tmpfs_stat(const char *name, tmpfs_stat_t *stat) {
    tmpfs_file_t *file = tmpfs_find_file(name);

    if (file == 0 || stat == 0) {
        return -1;
    }

    stat->size = file->size;
    return 0;
}
