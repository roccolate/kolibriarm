#include <stdint.h>

#include "unity/unity.h"
#include "../kernel/boot_program.h"

char __user_demo_start[64] = {
    0x4b, 0x4c, 0x49, 0x31,
    0x30, 0x00,
    0x01, 0x00,
    0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xaa, 0xbb, 0xcc, 0xdd,
};
char __user_demo_end[1];

void test_boot_program_find_existing_program(void) {
    const boot_program_t *program = boot_program_find("user_demo");

    TEST_ASSERT_NOT_NULL(program);
    TEST_ASSERT_EQUAL_UINT64('u', program->name[0]);
    TEST_ASSERT_EQUAL_UINT64('s', program->name[1]);
    TEST_ASSERT_EQUAL_UINT64('e', program->name[2]);
    TEST_ASSERT_EQUAL_UINT64('r', program->name[3]);
    TEST_ASSERT_EQUAL_UINT64('_', program->name[4]);
    TEST_ASSERT_EQUAL_UINT64('d', program->name[5]);
    TEST_ASSERT_EQUAL_UINT64('e', program->name[6]);
    TEST_ASSERT_EQUAL_UINT64('m', program->name[7]);
    TEST_ASSERT_EQUAL_UINT64('o', program->name[8]);
    TEST_ASSERT_EQUAL_UINT64('\0', program->name[9]);
}

void test_boot_program_find_rejects_missing_and_invalid_names(void) {
    TEST_ASSERT_NULL(boot_program_find("missing"));
    TEST_ASSERT_NULL(boot_program_find("user"));
    TEST_ASSERT_NULL(boot_program_find(""));
    TEST_ASSERT_NULL(boot_program_find(0));
}

void test_boot_program_metadata_round_trips_image_range(void) {
    const boot_program_t *program = boot_program_find("user_demo");

    TEST_ASSERT_NOT_NULL(program);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uintptr_t)__user_demo_start,
                             (uint64_t)(uintptr_t)program->image);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)((uintptr_t)__user_demo_end -
                                        (uintptr_t)__user_demo_start),
                             program->size);
}
