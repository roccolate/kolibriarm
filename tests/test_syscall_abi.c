/*
 * test_syscall_abi.c
 *
 * Lock down the ArmoniOS syscall ABI. The header
 * kernel/syscall_numbers.h carries one _Static_assert per implemented
 * syscall number; that catches ABI drift at compile time. This test
 * file re-asserts the contract at runtime so a regression on a
 * non-asserted branch (for example, accidentally deleting the assert)
 * is caught before it reaches QEMU.
 *
 * The test only exercises lightweight public headers on purpose: the full
 * syscall_dispatch path pulls in mmu, sched, timer and a working UART, none of
 * which the host suite provides. The end-to-end "does the kernel honour the
 * ABI" question is answered by the tests that drive the same code paths via
 * the public kernel/process, kernel/vfs and kernel/gui APIs.
 */

#include "unity/unity.h"

#include <stdint.h>

#include "kernel/process.h"
#include "kernel/syscall_helpers.h"
#include "kernel/syscall_numbers.h"
#include "kernel/user_exit.h"
#include "kernel/user_vm.h"
#include "kernel/vfs.h"

void test_syscall_abi_implemented_numbers_match_dispatch(void) {
    /* Implemented numbers must match the rows in docs/SYSCALLS.md under
     * "Implemented Now". A drift here means a number was renumbered
     * without updating the docs and breaks every userland image. */
    TEST_ASSERT_EQUAL_UINT64(1ULL, SYS_EXIT);
    TEST_ASSERT_EQUAL_UINT64(2ULL, SYS_YIELD);
    TEST_ASSERT_EQUAL_UINT64(3ULL, SYS_GETPID);
    TEST_ASSERT_EQUAL_UINT64(4ULL, SYS_SPAWN);
    TEST_ASSERT_EQUAL_UINT64(6ULL, SYS_WAIT);
    TEST_ASSERT_EQUAL_UINT64(7ULL, SYS_KILL);
    TEST_ASSERT_EQUAL_UINT64(8ULL, SYS_SPAWN_ARGV);
    TEST_ASSERT_EQUAL_UINT64(20ULL, SYS_MMAP);
    TEST_ASSERT_EQUAL_UINT64(21ULL, SYS_MUNMAP);
    TEST_ASSERT_EQUAL_UINT64(40ULL, SYS_OPEN);
    TEST_ASSERT_EQUAL_UINT64(41ULL, SYS_CLOSE);
    TEST_ASSERT_EQUAL_UINT64(42ULL, SYS_READ);
    TEST_ASSERT_EQUAL_UINT64(43ULL, SYS_WRITE);
    TEST_ASSERT_EQUAL_UINT64(44ULL, SYS_SEEK);
    TEST_ASSERT_EQUAL_UINT64(45ULL, SYS_STAT);
    TEST_ASSERT_EQUAL_UINT64(46ULL, SYS_READDIR);
    TEST_ASSERT_EQUAL_UINT64(47ULL, SYS_UNLINK);
    TEST_ASSERT_EQUAL_UINT64(48ULL, SYS_RENAME);
    TEST_ASSERT_EQUAL_UINT64(60ULL, SYS_IPC_SEND);
    TEST_ASSERT_EQUAL_UINT64(61ULL, SYS_IPC_RECV);
    TEST_ASSERT_EQUAL_UINT64(70ULL, SYS_WINDOW_CREATE);
    TEST_ASSERT_EQUAL_UINT64(71ULL, SYS_WINDOW_DESTROY);
    TEST_ASSERT_EQUAL_UINT64(72ULL, SYS_WINDOW_DRAW_TEXT);
    TEST_ASSERT_EQUAL_UINT64(73ULL, SYS_WINDOW_DRAW_RECT);
    TEST_ASSERT_EQUAL_UINT64(74ULL, SYS_WINDOW_EVENT);
    TEST_ASSERT_EQUAL_UINT64(75ULL, SYS_WINDOW_SET_TITLE);
    TEST_ASSERT_EQUAL_UINT64(76ULL, SYS_WINDOW_REDRAW);
    TEST_ASSERT_EQUAL_UINT64(77ULL, SYS_WINDOW_FOCUS);
    TEST_ASSERT_EQUAL_UINT64(78ULL, SYS_WINDOW_FOR_PID);
    TEST_ASSERT_EQUAL_UINT64(79ULL, SYS_CURSOR_SET_SHAPE);
    TEST_ASSERT_EQUAL_UINT64(80ULL, SYS_WINDOW_FLUSH);
    TEST_ASSERT_EQUAL_UINT64(81ULL, SYS_WINDOW_GET_BOUNDS);
    TEST_ASSERT_EQUAL_UINT64(82ULL, SYS_WINDOW_SET_BOUNDS);
    TEST_ASSERT_EQUAL_UINT64(83ULL, SYS_WINDOW_MINIMIZE);
    TEST_ASSERT_EQUAL_UINT64(84ULL, SYS_WINDOW_RESTORE);
    TEST_ASSERT_EQUAL_UINT64(85ULL, SYS_WINDOW_STATE);
    TEST_ASSERT_EQUAL_UINT64(86ULL, SYS_CURSOR_REGISTER_REGION);
    TEST_ASSERT_EQUAL_UINT64(100ULL, SYS_TIMEINFO);
    TEST_ASSERT_EQUAL_UINT64(101ULL, SYS_MEMINFO);
    TEST_ASSERT_EQUAL_UINT64(102ULL, SYS_PROCLIST);
}

void test_syscall_abi_ranges_do_not_overlap(void) {
    /* Process, memory, VFS, IPC, window and info ranges must not bleed
     * into each other. An overlap means a syscall lost its number to
     * a neighbour range, which is an ABI break. */
    TEST_ASSERT_TRUE(SYS_KILL < SYS_MMAP);
    TEST_ASSERT_TRUE(SYS_SPAWN_ARGV < SYS_MMAP);
    TEST_ASSERT_TRUE(SYS_MUNMAP < SYS_OPEN);
    TEST_ASSERT_TRUE(SYS_RENAME < SYS_IPC_SEND);
    TEST_ASSERT_TRUE(SYS_IPC_RECV < SYS_WINDOW_CREATE);
    TEST_ASSERT_TRUE(SYS_WINDOW_FLUSH < SYS_TIMEINFO);
}

void test_syscall_abi_error_codes_match_documented_constants(void) {
    /* docs/SYSCALLS.md lists these as the implemented negative error codes. If
     * they drift, every app that checks errno by hand breaks silently. */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(int64_t)-2,
                             (uint64_t)(int64_t)USER_VM_ERR_NOMEM);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(int64_t)-3,
                             (uint64_t)(int64_t)ERR_NOENT);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(int64_t)-5,
                             (uint64_t)(int64_t)ERR_BADF);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(int64_t)-7,
                             (uint64_t)(int64_t)ERR_INVAL);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(int64_t)-11,
                             (uint64_t)(int64_t)ERR_AGAIN);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(int64_t)-13,
                             (uint64_t)(int64_t)ERR_PERM);

    TEST_ASSERT_TRUE(USER_VM_ERR_NOMEM != ERR_NOENT);
    TEST_ASSERT_TRUE(ERR_NOENT != ERR_BADF);
    TEST_ASSERT_TRUE(ERR_BADF != ERR_INVAL);
    TEST_ASSERT_TRUE(ERR_INVAL != ERR_AGAIN);
    TEST_ASSERT_TRUE(ERR_AGAIN != ERR_PERM);
}

void test_syscall_abi_vfs_open_flags_match_documentation(void) {
    TEST_ASSERT_EQUAL_UINT64(0ULL, VFS_O_RDONLY);
    TEST_ASSERT_EQUAL_UINT64(1ULL, VFS_O_WRONLY);
    TEST_ASSERT_EQUAL_UINT64(2ULL, VFS_O_RDWR);
    TEST_ASSERT_EQUAL_UINT64(0x40ULL, VFS_O_CREAT);
    TEST_ASSERT_EQUAL_UINT64(0x43ULL, VFS_O_ALLOWED);
}

void test_syscall_abi_user_exit_codes_match_documented_constants(void) {
    /* sys_kill and lower-EL fault handling expose these exit codes to
     * waiters. They are part of the observable process ABI, not just
     * local implementation details. */
    TEST_ASSERT_EQUAL_UINT64(0x80ULL, KERNEL_USER_KILL_EXIT_CODE);
    TEST_ASSERT_EQUAL_UINT64(0xfffffffffffffff0ULL,
                             KERNEL_USER_FAULT_EXIT_CODE);
    TEST_ASSERT_TRUE(KERNEL_USER_KILL_EXIT_CODE != KERNEL_USER_FAULT_EXIT_CODE);
}

void test_syscall_abi_user_range_validation_rejects_out_of_region(void) {
    /* Every syscall that takes a user pointer must reject pointers
     * outside the caller's registered regions. The dispatcher delegates
     * to process_user_range_contains; if that function ever grows lax,
     * the syscall ABI loses its isolation promise.
     *
     * This is the kernel-side half of the contract; the same helper is
     * exercised through every public syscall in the host suite via the
     * syscall_dispatch entry, but pinning it here makes the intent
     * explicit and catches a regression that would otherwise need a
     * full EL0 image to surface. */
    process_t process;
    process_init(&process, 4242U, "abi_range");

    /* Register one small region. */
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)process_add_user_region(
                                 &process, 0x1000ULL, 0x100ULL));

    /* In range. */
    TEST_ASSERT_TRUE(process_user_range_contains(&process, 0x1000ULL, 0x100ULL));
    TEST_ASSERT_TRUE(process_user_range_contains(&process, 0x1050ULL, 0x10ULL));

    /* Out of range: just before, just after, and overlaps. */
    TEST_ASSERT_TRUE(!process_user_range_contains(&process, 0x0fffULL, 1ULL));
    TEST_ASSERT_TRUE(!process_user_range_contains(&process, 0x1100ULL, 1ULL));
    TEST_ASSERT_TRUE(!process_user_range_contains(&process, 0x10f0ULL, 0x20ULL));

    /* Zero-length query is vacuously true at any address — the kernel
     * treats it as "no bytes, no overlap to check". This is a deliberate
     * choice for syscall hot paths that want to validate the pointer
     * before computing a length. */
    TEST_ASSERT_TRUE(process_user_range_contains(&process, 0x1000ULL, 0ULL));
    TEST_ASSERT_TRUE(process_user_range_contains(&process, 0x1100ULL, 0ULL));
    TEST_ASSERT_TRUE(process_user_range_contains(&process, 0x0ULL, 0ULL));

    process_release(&process);
}
