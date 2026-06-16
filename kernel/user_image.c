#include "kernel/user_image.h"

#include <stdint.h>

#include "kernel/bootfs.h"

uint64_t user_image_entry(const user_image_t *image) {
    if (image == 0 || image->base == 0 || image->size == 0 ||
        image->entry_offset >= image->size) {
        return 0;
    }

    return image->base + image->entry_offset;
}

int user_image_load_copy(user_image_t *image, const char *name,
                         uint64_t load_base, uint64_t load_capacity,
                         uint64_t source_base, uint64_t source_size,
                         uint64_t source_entry) {
    const uint8_t *source = (const uint8_t *)(uintptr_t)source_base;
    uint8_t *load = (uint8_t *)(uintptr_t)load_base;

    if (image == 0 || load_base == 0 || load_capacity == 0 ||
        source_base == 0 || source_size == 0 || source_size > load_capacity ||
        source_entry < source_base ||
        source_entry - source_base >= source_size) {
        return -1;
    }

    for (uint64_t i = 0; i < source_size; i++) {
        load[i] = source[i];
    }

    image->name = name;
    image->base = load_base;
    image->size = source_size;
    image->entry_offset = source_entry - source_base;

    return 0;
}

int user_image_load_flat(user_image_t *image, const char *name,
                         uint64_t load_base, uint64_t load_capacity,
                         uint64_t source_base, uint64_t source_capacity,
                         uint32_t entry_index) {
    const user_flat_image_header_t *header =
        (const user_flat_image_header_t *)(uintptr_t)source_base;
    uint64_t entry_offset;

    if (source_base == 0 || source_capacity < sizeof(*header)) {
        return -1;
    }

    if (header->magic != USER_IMAGE_MAGIC ||
        header->header_size < sizeof(*header) ||
        header->header_size > header->image_size ||
        header->entry_count == 0 ||
        header->entry_count > USER_IMAGE_MAX_ENTRIES ||
        entry_index >= header->entry_count ||
        header->image_size > source_capacity) {
        return -1;
    }

    entry_offset = header->entry_offsets[entry_index];
    if (entry_offset < header->header_size || entry_offset >= header->image_size) {
        return -1;
    }

    return user_image_load_copy(image, name, load_base, load_capacity,
                                source_base, header->image_size,
                                source_base + entry_offset);
}

int user_image_load_bootfs_flat(user_image_t *image, const char *image_name,
                                const char *bootfs_name, uint64_t load_base,
                                uint64_t load_capacity,
                                uint32_t entry_index) {
    const bootfs_file_t *file = bootfs_find(bootfs_name);

    if (file == 0) {
        return -1;
    }

    return user_image_load_flat(image, image_name, load_base, load_capacity,
                                (uint64_t)(uintptr_t)file->data, file->size,
                                entry_index);
}

int user_image_prepare_process(process_t *process, const user_image_t *image,
                               uint64_t stack_start, uint64_t stack_size,
                               uint64_t pstate) {
    uint64_t entry = user_image_entry(image);
    uint64_t stack_top = stack_start + stack_size;
    uint64_t old_pc;
    uint64_t old_sp;
    uint64_t old_pstate;

    if (process == 0 || entry == 0 || stack_start == 0 || stack_size == 0 ||
        stack_top < stack_start) {
        return -1;
    }

    old_pc = process->pc;
    old_sp = process->sp;
    old_pstate = process->pstate;
    process_set_entry(process, entry, stack_top, pstate);

    if (process_add_user_region(process, image->base, image->size) != 0) {
        process_set_entry(process, old_pc, old_sp, old_pstate);
        return -1;
    }

    if (process_add_user_region(process, stack_start, stack_size) != 0) {
        (void)process_remove_user_region(process, image->base, image->size);
        process_set_entry(process, old_pc, old_sp, old_pstate);
        return -1;
    }

    return 0;
}
