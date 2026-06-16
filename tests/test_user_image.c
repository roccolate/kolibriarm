#include <stdint.h>

#include "unity/unity.h"
#include "../kernel/user_image.h"

extern char __user_demo_start[];

void test_user_image_entry_uses_base_plus_offset(void) {
    user_image_t image = {
        .name = "hello",
        .base = 0x400000ULL,
        .size = 0x1000ULL,
        .entry_offset = 0x80ULL,
    };

    TEST_ASSERT_EQUAL_UINT64(0x400080ULL, user_image_entry(&image));

    image.entry_offset = image.size;
    TEST_ASSERT_EQUAL_UINT64(0, user_image_entry(&image));
    TEST_ASSERT_EQUAL_UINT64(0, user_image_entry(0));
}

void test_user_image_prepare_process_registers_image_and_stack(void) {
    process_t process;
    user_image_t image = {
        .name = "hello",
        .base = 0x500000ULL,
        .size = 0x2000ULL,
        .entry_offset = 0x100ULL,
    };

    process_init(&process, 12, image.name);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)user_image_prepare_process(
                                 &process, &image, 0x700000ULL, 0x1000ULL,
                                 0x340ULL));
    TEST_ASSERT_EQUAL_UINT64(0x500100ULL, process.pc);
    TEST_ASSERT_EQUAL_UINT64(0x701000ULL, process.sp);
    TEST_ASSERT_EQUAL_UINT64(0x340ULL, process.pstate);
    TEST_ASSERT_TRUE(process_user_range_contains(&process, 0x500000ULL,
                                                 0x2000ULL));
    TEST_ASSERT_TRUE(process_user_range_contains(&process, 0x700000ULL,
                                                 0x1000ULL));
    TEST_ASSERT_EQUAL_UINT64(2, process.user_region_count);
}

void test_user_image_load_copy_copies_source_and_sets_descriptor(void) {
    uint8_t source[8] = { 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48 };
    uint8_t loaded[8] = { 0 };
    user_image_t image;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)user_image_load_copy(
                                 &image, "copy",
                                 (uint64_t)(uintptr_t)loaded, sizeof(loaded),
                                 (uint64_t)(uintptr_t)source, sizeof(source),
                                 (uint64_t)(uintptr_t)&source[3]));

    TEST_ASSERT_TRUE(image.name == (const char *)"copy");
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uintptr_t)loaded, image.base);
    TEST_ASSERT_EQUAL_UINT64(sizeof(source), image.size);
    TEST_ASSERT_EQUAL_UINT64(3, image.entry_offset);

    for (uint32_t i = 0; i < sizeof(source); i++) {
        TEST_ASSERT_EQUAL_UINT64(source[i], loaded[i]);
    }
}

void test_user_image_load_flat_uses_header_entry_table(void) {
    struct {
        user_flat_image_header_t header;
        uint8_t code[16];
    } source;
    uint8_t loaded[sizeof(source)] = { 0 };
    user_image_t image;

    source.header.magic = USER_IMAGE_MAGIC;
    source.header.header_size = sizeof(source.header);
    source.header.entry_count = 2;
    source.header.image_size = sizeof(source);
    source.header.entry_offsets[0] = sizeof(source.header) + 4;
    source.header.entry_offsets[1] = sizeof(source.header) + 8;
    source.header.entry_offsets[2] = 0;
    source.header.entry_offsets[3] = 0;
    for (uint32_t i = 0; i < sizeof(source.code); i++) {
        source.code[i] = (uint8_t)(0x80U + i);
    }

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)user_image_load_flat(
                                 &image, "flat",
                                 (uint64_t)(uintptr_t)loaded, sizeof(loaded),
                                 (uint64_t)(uintptr_t)&source, sizeof(source),
                                 1));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uintptr_t)loaded, image.base);
    TEST_ASSERT_EQUAL_UINT64(sizeof(source), image.size);
    TEST_ASSERT_EQUAL_UINT64(sizeof(source.header) + 8, image.entry_offset);

    for (uint32_t i = 0; i < sizeof(source); i++) {
        const uint8_t *bytes = (const uint8_t *)(const void *)&source;

        TEST_ASSERT_EQUAL_UINT64(bytes[i], loaded[i]);
    }
}

void test_user_image_load_bootfs_flat_uses_named_boot_file(void) {
    uint8_t loaded[128] = { 0 };
    user_image_t image;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)user_image_load_bootfs_flat(
                                 &image, "bootfs-flat", "user_demo",
                                 (uint64_t)(uintptr_t)loaded, sizeof(loaded),
                                 0));
    TEST_ASSERT_TRUE(image.name == (const char *)"bootfs-flat");
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uintptr_t)loaded, image.base);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__user_demo_start[0], loaded[0]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__user_demo_start[1], loaded[1]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__user_demo_start[2], loaded[2]);
    TEST_ASSERT_EQUAL_UINT64((uint8_t)__user_demo_start[3], loaded[3]);
}

void test_user_image_prepare_process_rejects_invalid_inputs(void) {
    process_t process;
    process_t overlap;
    user_image_t image = {
        .name = "bad",
        .base = 0x800000ULL,
        .size = 0x1000ULL,
        .entry_offset = 0x1000ULL,
    };

    process_init(&process, 13, image.name);

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)user_image_prepare_process(
                                 &process, &image, 0x900000ULL, 0x1000ULL,
                                 0x340ULL));
    TEST_ASSERT_EQUAL_UINT64(0, process.user_region_count);

    image.entry_offset = 0;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)user_image_prepare_process(
                                 0, &image, 0x900000ULL, 0x1000ULL, 0x340ULL));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)user_image_prepare_process(
                                 &process, &image, 0, 0x1000ULL, 0x340ULL));

    process_init(&overlap, 14, "overlap");
    image.entry_offset = 0;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)user_image_prepare_process(
                                 &overlap, &image, 0x800800ULL, 0x1000ULL,
                                 0x340ULL));
    TEST_ASSERT_EQUAL_UINT64(0, overlap.pc);
    TEST_ASSERT_EQUAL_UINT64(0, overlap.sp);
    TEST_ASSERT_EQUAL_UINT64(0, overlap.user_region_count);
}

void test_user_image_load_copy_rejects_invalid_ranges(void) {
    uint8_t source[4] = { 1, 2, 3, 4 };
    uint8_t loaded[4] = { 0 };
    user_image_t image;

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)user_image_load_copy(
                                 &image, "small",
                                 (uint64_t)(uintptr_t)loaded, 3,
                                 (uint64_t)(uintptr_t)source, sizeof(source),
                                 (uint64_t)(uintptr_t)source));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)user_image_load_copy(
                                 &image, "entry-before",
                                 (uint64_t)(uintptr_t)loaded, sizeof(loaded),
                                 (uint64_t)(uintptr_t)source, sizeof(source),
                                 (uint64_t)(uintptr_t)source - 1ULL));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)user_image_load_copy(
                                 &image, "entry-end",
                                 (uint64_t)(uintptr_t)loaded, sizeof(loaded),
                                 (uint64_t)(uintptr_t)source, sizeof(source),
                                 (uint64_t)(uintptr_t)&source[4]));
}

void test_user_image_load_flat_rejects_invalid_headers(void) {
    user_flat_image_header_t source;
    uint8_t loaded[128] = { 0 };
    user_image_t image;

    source.magic = USER_IMAGE_MAGIC;
    source.header_size = sizeof(source);
    source.entry_count = 1;
    source.image_size = sizeof(source);
    source.entry_offsets[0] = sizeof(source);
    source.entry_offsets[1] = 0;
    source.entry_offsets[2] = 0;
    source.entry_offsets[3] = 0;

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)user_image_load_flat(
                                 &image, "bad-index",
                                 (uint64_t)(uintptr_t)loaded, sizeof(loaded),
                                 (uint64_t)(uintptr_t)&source, sizeof(source),
                                 1));

    source.magic = 0;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)user_image_load_flat(
                                 &image, "bad-magic",
                                 (uint64_t)(uintptr_t)loaded, sizeof(loaded),
                                 (uint64_t)(uintptr_t)&source, sizeof(source),
                                 0));

    source.magic = USER_IMAGE_MAGIC;
    source.image_size = sizeof(source) + 1ULL;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)user_image_load_flat(
                                 &image, "bad-size",
                                 (uint64_t)(uintptr_t)loaded, sizeof(loaded),
                                 (uint64_t)(uintptr_t)&source, sizeof(source),
                                 0));
}

void test_user_image_load_bootfs_flat_rejects_missing_file(void) {
    uint8_t loaded[128] = { 0 };
    user_image_t image;

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)user_image_load_bootfs_flat(
                                 &image, "missing", "missing",
                                 (uint64_t)(uintptr_t)loaded, sizeof(loaded),
                                 0));
}
