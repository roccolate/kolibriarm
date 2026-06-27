#include <stdint.h>

#include "unity/unity.h"
#include "../kernel/panel_boot_argv.h"

#define TEST_STACK_BASE 0x0000000000800000ULL
#define TEST_STACK_SIZE 4096U

static uint64_t load_u64(const uint8_t *src) {
    uint64_t value = 0;

    for (uint32_t i = 0; i < sizeof(uint64_t); i++) {
        value |= (uint64_t)src[i] << (i * 8U);
    }

    return value;
}

static void clear_stack(uint8_t *stack, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        stack[i] = 0xccU;
    }
}

static int stack_string_equals(const uint8_t *stack, uint64_t vaddr,
                               const char *expected) {
    uint64_t offset = vaddr - TEST_STACK_BASE;
    uint32_t i = 0;

    if (vaddr < TEST_STACK_BASE ||
        offset >= TEST_STACK_SIZE ||
        expected == 0) {
        return 0;
    }

    for (;;) {
        char actual = (char)stack[offset + i];

        if (actual != expected[i]) {
            return 0;
        }
        if (actual == '\0') {
            return 1;
        }
        i++;
        if (offset + i >= TEST_STACK_SIZE) {
            return 0;
        }
    }
}

void test_panel_boot_argv_rejects_too_many_strings(void) {
    uint8_t stack[TEST_STACK_SIZE] __attribute__((aligned(16)));
    uint64_t argv[PANEL_BOOT_ARGV_MAX_STRINGS + 1U];
    uint64_t argv_vaddr = 0xfeedfaceULL;
    const char arg[] = "x";

    clear_stack(stack, sizeof(stack));
    for (uint32_t i = 0; i < PANEL_BOOT_ARGV_MAX_STRINGS + 1U; i++) {
        argv[i] = (uint64_t)(uintptr_t)arg;
    }

    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)-1,
        (uint64_t)panel_boot_place_argv_on_stack(
            stack, TEST_STACK_BASE, TEST_STACK_SIZE, argv,
            PANEL_BOOT_ARGV_MAX_STRINGS + 1U, &argv_vaddr));
    TEST_ASSERT_EQUAL_UINT64(0xfeedfaceULL, argv_vaddr);
}

void test_panel_boot_argv_rejects_total_string_budget_overflow(void) {
    uint8_t stack[TEST_STACK_SIZE] __attribute__((aligned(16)));
    char large[PANEL_BOOT_ARGV_MAX_BYTES + 1U];
    uint64_t argv[1];
    uint64_t argv_vaddr = 0xfeedfaceULL;

    clear_stack(stack, sizeof(stack));
    for (uint32_t i = 0; i < PANEL_BOOT_ARGV_MAX_BYTES; i++) {
        large[i] = 'a';
    }
    large[PANEL_BOOT_ARGV_MAX_BYTES] = '\0';
    argv[0] = (uint64_t)(uintptr_t)large;

    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)-1,
        (uint64_t)panel_boot_place_argv_on_stack(
            stack, TEST_STACK_BASE, TEST_STACK_SIZE, argv, 1, &argv_vaddr));
    TEST_ASSERT_EQUAL_UINT64(0xfeedfaceULL, argv_vaddr);
}

void test_panel_boot_argv_packs_well_formed_args_with_alignment_and_sentinel(void) {
    uint8_t stack[TEST_STACK_SIZE] __attribute__((aligned(16)));
    const char arg0[] = "editor";
    const char arg1[] = "--safe";
    const char arg2[] = "file.txt";
    uint64_t argv[3];
    uint64_t argv_vaddr = 0;
    uint64_t offset;
    uint64_t arg0_vaddr;
    uint64_t arg1_vaddr;
    uint64_t arg2_vaddr;

    clear_stack(stack, sizeof(stack));
    argv[0] = (uint64_t)(uintptr_t)arg0;
    argv[1] = (uint64_t)(uintptr_t)arg1;
    argv[2] = (uint64_t)(uintptr_t)arg2;

    TEST_ASSERT_EQUAL_UINT64(
        0,
        (uint64_t)panel_boot_place_argv_on_stack(
            stack, TEST_STACK_BASE, TEST_STACK_SIZE, argv, 3, &argv_vaddr));
    TEST_ASSERT_TRUE((argv_vaddr & 0xfULL) == 0);
    TEST_ASSERT_TRUE(argv_vaddr >= TEST_STACK_BASE);
    TEST_ASSERT_TRUE(argv_vaddr < TEST_STACK_BASE + TEST_STACK_SIZE);

    offset = argv_vaddr - TEST_STACK_BASE;
    arg0_vaddr = load_u64(&stack[offset + 0U * sizeof(uint64_t)]);
    arg1_vaddr = load_u64(&stack[offset + 1U * sizeof(uint64_t)]);
    arg2_vaddr = load_u64(&stack[offset + 2U * sizeof(uint64_t)]);

    TEST_ASSERT_TRUE(stack_string_equals(stack, arg0_vaddr, arg0));
    TEST_ASSERT_TRUE(stack_string_equals(stack, arg1_vaddr, arg1));
    TEST_ASSERT_TRUE(stack_string_equals(stack, arg2_vaddr, arg2));
    TEST_ASSERT_EQUAL_UINT64(
        0, load_u64(&stack[offset + 3U * sizeof(uint64_t)]));
}

void test_panel_boot_argv_zero_argc_returns_null_argv(void) {
    uint8_t stack[TEST_STACK_SIZE] __attribute__((aligned(16)));
    uint64_t argv_vaddr = 0xfeedfaceULL;

    clear_stack(stack, sizeof(stack));

    TEST_ASSERT_EQUAL_UINT64(
        0,
        (uint64_t)panel_boot_place_argv_on_stack(
            stack, TEST_STACK_BASE, TEST_STACK_SIZE, 0, 0, &argv_vaddr));
    TEST_ASSERT_EQUAL_UINT64(0, argv_vaddr);
}

void test_panel_boot_argv_rejects_invalid_stack_inputs(void) {
    uint8_t stack[TEST_STACK_SIZE] __attribute__((aligned(16)));
    const char arg[] = "shell";
    uint64_t argv[1];
    uint64_t argv_vaddr = 0xfeedfaceULL;

    argv[0] = (uint64_t)(uintptr_t)arg;

    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)-1,
        (uint64_t)panel_boot_place_argv_on_stack(
            0, TEST_STACK_BASE, TEST_STACK_SIZE, argv, 1, &argv_vaddr));
    TEST_ASSERT_EQUAL_UINT64(0xfeedfaceULL, argv_vaddr);

    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)-1,
        (uint64_t)panel_boot_place_argv_on_stack(
            stack, TEST_STACK_BASE, TEST_STACK_SIZE, argv, 1, 0));

    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)-1,
        (uint64_t)panel_boot_place_argv_on_stack(
            stack, TEST_STACK_BASE, 8, argv, 1, &argv_vaddr));
    TEST_ASSERT_EQUAL_UINT64(0xfeedfaceULL, argv_vaddr);
}
