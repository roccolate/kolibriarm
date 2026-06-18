#include <stdint.h>

#include "unity/unity.h"
#include "../kernel/boot_program.h"

extern char __app_hello_start[];
extern char __app_hello_end[];
extern char __app_loop_start[];
extern char __app_loop_end[];
extern char __app_fault_start[];
extern char __app_fault_end[];
extern char __app_shell_start[];
extern char __app_shell_end[];
extern char __app_editor_start[];
extern char __app_editor_end[];
extern char __app_monitor_start[];
extern char __app_monitor_end[];
extern char __app_win_start[];
extern char __app_win_end[];
extern char __app_panel_start[];
extern char __app_panel_end[];

__asm__(
    ".section .app_hello_blob, \"a\"\n"
    ".balign 8\n"
    ".global __app_hello_start\n"
    "__app_hello_start:\n"
    ".long 0x31494c4b\n"
    ".short 80\n"
    ".short 1\n"
    ".quad 84\n"
    ".quad 80\n"
    ".byte 0xaa, 0xbb, 0xcc, 0xdd\n"
    ".skip 56\n"
    ".global __app_hello_end\n"
    "__app_hello_end:\n"

    ".section .app_loop_blob, \"a\"\n"
    ".balign 8\n"
    ".global __app_loop_start\n"
    "__app_loop_start:\n"
    ".long 0x31494c4b\n"
    ".short 80\n"
    ".short 1\n"
    ".quad 84\n"
    ".quad 80\n"
    ".byte 0x10, 0x20, 0x30, 0x40\n"
    ".skip 56\n"
    ".global __app_loop_end\n"
    "__app_loop_end:\n"

    ".section .app_fault_blob, \"a\"\n"
    ".balign 8\n"
    ".global __app_fault_start\n"
    "__app_fault_start:\n"
    ".long 0x31494c4b\n"
    ".short 80\n"
    ".short 1\n"
    ".quad 84\n"
    ".quad 80\n"
    ".byte 0x55, 0x66, 0x77, 0x88\n"
    ".skip 56\n"
    ".global __app_fault_end\n"
    "__app_fault_end:\n"

    ".section .app_shell_blob, \"a\"\n"
    ".balign 8\n"
    ".global __app_shell_start\n"
    "__app_shell_start:\n"
    ".long 0x31494c4b\n"
    ".short 80\n"
    ".short 1\n"
    ".quad 84\n"
    ".quad 80\n"
    ".skip 60\n"
    ".global __app_shell_end\n"
    "__app_shell_end:\n"

    ".section .app_editor_blob, \"a\"\n"
    ".balign 8\n"
    ".global __app_editor_start\n"
    "__app_editor_start:\n"
    ".long 0x31494c4b\n"
    ".short 80\n"
    ".short 1\n"
    ".quad 84\n"
    ".quad 80\n"
    ".byte 0x99, 0xaa, 0xbb, 0xcc\n"
    ".skip 56\n"
    ".global __app_editor_end\n"
    "__app_editor_end:\n"

    ".section .app_monitor_blob, \"a\"\n"
    ".balign 8\n"
    ".global __app_monitor_start\n"
    "__app_monitor_start:\n"
    ".long 0x31494c4b\n"
    ".short 80\n"
    ".short 1\n"
    ".quad 84\n"
    ".quad 80\n"
    ".byte 0xde, 0xad, 0xbe, 0xef\n"
    ".skip 56\n"
    ".global __app_monitor_end\n"
    "__app_monitor_end:\n"

    ".section .app_win_blob, \"a\"\n"
    ".balign 8\n"
    ".global __app_win_start\n"
    "__app_win_start:\n"
    ".long 0x31494c4b\n"
    ".short 80\n"
    ".short 1\n"
    ".quad 84\n"
    ".quad 80\n"
    ".byte 0x77, 0x69, 0x6e, 0x21\n"
    ".skip 56\n"
    ".global __app_win_end\n"
    "__app_win_end:\n"

    ".section .app_panel_blob, \"a\"\n"
    ".balign 8\n"
    ".global __app_panel_start\n"
    "__app_panel_start:\n"
    ".long 0x31494c4b\n"
    ".short 80\n"
    ".short 1\n"
    ".quad 84\n"
    ".quad 80\n"
    ".byte 0x70, 0x61, 0x6e, 0x65\n"
    ".skip 56\n"
    ".global __app_panel_end\n"
    "__app_panel_end:\n"
);

void test_boot_program_find_existing_program(void) {
    const boot_program_t *program = boot_program_find("hello");

    TEST_ASSERT_NOT_NULL(program);
    TEST_ASSERT_EQUAL_UINT64('h', program->name[0]);
    TEST_ASSERT_EQUAL_UINT64('e', program->name[1]);
    TEST_ASSERT_EQUAL_UINT64('l', program->name[2]);
    TEST_ASSERT_EQUAL_UINT64('l', program->name[3]);
    TEST_ASSERT_EQUAL_UINT64('o', program->name[4]);
    TEST_ASSERT_EQUAL_UINT64('\0', program->name[5]);
}

void test_boot_program_find_returns_each_registered_app(void) {
    TEST_ASSERT_NOT_NULL(boot_program_find("hello"));
    TEST_ASSERT_NOT_NULL(boot_program_find("loop"));
    TEST_ASSERT_NOT_NULL(boot_program_find("fault"));
    TEST_ASSERT_NOT_NULL(boot_program_find("shell"));
}

void test_boot_program_find_rejects_missing_and_invalid_names(void) {
    TEST_ASSERT_NULL(boot_program_find("missing"));
    TEST_ASSERT_NULL(boot_program_find("user"));
    TEST_ASSERT_NULL(boot_program_find("user_demo"));
    TEST_ASSERT_NULL(boot_program_find(""));
    TEST_ASSERT_NULL(boot_program_find(0));
}

void test_boot_program_metadata_round_trips_image_range(void) {
    const boot_program_t *program = boot_program_find("hello");

    TEST_ASSERT_NOT_NULL(program);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uintptr_t)__app_hello_start,
                             (uint64_t)(uintptr_t)program->image);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)((uintptr_t)__app_hello_end -
                                        (uintptr_t)__app_hello_start),
                             program->size);
}
