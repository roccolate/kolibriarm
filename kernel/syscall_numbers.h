#ifndef KOLIBRIARM_KERNEL_SYSCALL_NUMBERS_H
#define KOLIBRIARM_KERNEL_SYSCALL_NUMBERS_H

/*
 * Syscall number table — KolibriARM ABI.
 *
 * The numbers in this file are FROZEN. Each one has a matching entry in
 * SYSCALLS.md under "Implemented Now". Any change must update both this
 * file and SYSCALLS.md in the same commit, and must add a comment that
 * explains the ABI break (the kernel has no compatibility layer; renumbering
 * silently breaks every userland image on disk).
 *
 * The `_Static_assert` next to each value makes the build fail loudly if
 * someone reorders or renumbers a syscall without touching the doc. Tests
 * (tests/test_syscall_abi.c) exercise each dispatch entry at runtime to
 * keep behaviour honest; docs alone drift.
 */

#define SYS_EXIT               1ULL
_Static_assert(SYS_EXIT == 1ULL, "ABI drift: SYS_EXIT — see SYSCALLS.md");

#define SYS_YIELD              2ULL
_Static_assert(SYS_YIELD == 2ULL, "ABI drift: SYS_YIELD — see SYSCALLS.md");

#define SYS_GETPID             3ULL
_Static_assert(SYS_GETPID == 3ULL, "ABI drift: SYS_GETPID — see SYSCALLS.md");

#define SYS_SPAWN              4ULL
_Static_assert(SYS_SPAWN == 4ULL, "ABI drift: SYS_SPAWN — see SYSCALLS.md");

#define SYS_WAIT               6ULL
_Static_assert(SYS_WAIT == 6ULL, "ABI drift: SYS_WAIT — see SYSCALLS.md");

#define SYS_KILL               7ULL
_Static_assert(SYS_KILL == 7ULL, "ABI drift: SYS_KILL — see SYSCALLS.md");

#define SYS_SPAWN_ARGV         8ULL
_Static_assert(SYS_SPAWN_ARGV == 8ULL, "ABI drift: SYS_SPAWN_ARGV — see SYSCALLS.md");

#define SYS_MMAP               20ULL
_Static_assert(SYS_MMAP == 20ULL, "ABI drift: SYS_MMAP — see SYSCALLS.md");

#define SYS_MUNMAP             21ULL
_Static_assert(SYS_MUNMAP == 21ULL, "ABI drift: SYS_MUNMAP — see SYSCALLS.md");

#define SYS_OPEN               40ULL
_Static_assert(SYS_OPEN == 40ULL, "ABI drift: SYS_OPEN — see SYSCALLS.md");

#define SYS_CLOSE              41ULL
_Static_assert(SYS_CLOSE == 41ULL, "ABI drift: SYS_CLOSE — see SYSCALLS.md");

#define SYS_READ               42ULL
_Static_assert(SYS_READ == 42ULL, "ABI drift: SYS_READ — see SYSCALLS.md");

#define SYS_WRITE              43ULL
_Static_assert(SYS_WRITE == 43ULL, "ABI drift: SYS_WRITE — see SYSCALLS.md");

#define SYS_SEEK               44ULL
_Static_assert(SYS_SEEK == 44ULL, "ABI drift: SYS_SEEK — see SYSCALLS.md");

#define SYS_STAT               45ULL
_Static_assert(SYS_STAT == 45ULL, "ABI drift: SYS_STAT — see SYSCALLS.md");

#define SYS_READDIR            46ULL
_Static_assert(SYS_READDIR == 46ULL, "ABI drift: SYS_READDIR — see SYSCALLS.md");

#define SYS_UNLINK             47ULL
_Static_assert(SYS_UNLINK == 47ULL, "ABI drift: SYS_UNLINK — see SYSCALLS.md");

#define SYS_RENAME             48ULL
_Static_assert(SYS_RENAME == 48ULL, "ABI drift: SYS_RENAME — see SYSCALLS.md");

#define SYS_IPC_SEND           60ULL
_Static_assert(SYS_IPC_SEND == 60ULL, "ABI drift: SYS_IPC_SEND — see SYSCALLS.md");

#define SYS_IPC_RECV           61ULL
_Static_assert(SYS_IPC_RECV == 61ULL, "ABI drift: SYS_IPC_RECV — see SYSCALLS.md");

#define SYS_WINDOW_CREATE      70ULL
_Static_assert(SYS_WINDOW_CREATE == 70ULL,
               "ABI drift: SYS_WINDOW_CREATE — see SYSCALLS.md");

#define SYS_WINDOW_DESTROY     71ULL
_Static_assert(SYS_WINDOW_DESTROY == 71ULL,
               "ABI drift: SYS_WINDOW_DESTROY — see SYSCALLS.md");

#define SYS_WINDOW_DRAW_TEXT   72ULL
_Static_assert(SYS_WINDOW_DRAW_TEXT == 72ULL,
               "ABI drift: SYS_WINDOW_DRAW_TEXT — see SYSCALLS.md");

#define SYS_WINDOW_DRAW_RECT   73ULL
_Static_assert(SYS_WINDOW_DRAW_RECT == 73ULL,
               "ABI drift: SYS_WINDOW_DRAW_RECT — see SYSCALLS.md");

#define SYS_WINDOW_EVENT       74ULL
_Static_assert(SYS_WINDOW_EVENT == 74ULL,
               "ABI drift: SYS_WINDOW_EVENT — see SYSCALLS.md");

#define SYS_WINDOW_SET_TITLE   75ULL
_Static_assert(SYS_WINDOW_SET_TITLE == 75ULL,
               "ABI drift: SYS_WINDOW_SET_TITLE — see SYSCALLS.md");

#define SYS_WINDOW_REDRAW      76ULL
_Static_assert(SYS_WINDOW_REDRAW == 76ULL,
               "ABI drift: SYS_WINDOW_REDRAW — see SYSCALLS.md");

#define SYS_WINDOW_FOCUS       77ULL
_Static_assert(SYS_WINDOW_FOCUS == 77ULL,
               "ABI drift: SYS_WINDOW_FOCUS — see SYSCALLS.md");

#define SYS_WINDOW_FOR_PID     78ULL
_Static_assert(SYS_WINDOW_FOR_PID == 78ULL,
               "ABI drift: SYS_WINDOW_FOR_PID — see SYSCALLS.md");

#define SYS_CURSOR_SET_SHAPE   79ULL
_Static_assert(SYS_CURSOR_SET_SHAPE == 79ULL,
               "ABI drift: SYS_CURSOR_SET_SHAPE — see SYSCALLS.md");

#define SYS_WINDOW_FLUSH       80ULL
_Static_assert(SYS_WINDOW_FLUSH == 80ULL,
               "ABI drift: SYS_WINDOW_FLUSH — see SYSCALLS.md");

#define SYS_WINDOW_GET_BOUNDS 81ULL
_Static_assert(SYS_WINDOW_GET_BOUNDS == 81ULL,
               "ABI drift: SYS_WINDOW_GET_BOUNDS — see SYSCALLS.md");

#define SYS_WINDOW_SET_BOUNDS 82ULL
_Static_assert(SYS_WINDOW_SET_BOUNDS == 82ULL,
               "ABI drift: SYS_WINDOW_SET_BOUNDS — see SYSCALLS.md");

#define SYS_WINDOW_MINIMIZE    83ULL
_Static_assert(SYS_WINDOW_MINIMIZE == 83ULL,
               "ABI drift: SYS_WINDOW_MINIMIZE — see SYSCALLS.md");

#define SYS_WINDOW_RESTORE     84ULL
_Static_assert(SYS_WINDOW_RESTORE == 84ULL,
               "ABI drift: SYS_WINDOW_RESTORE — see SYSCALLS.md");

#define SYS_WINDOW_STATE       85ULL
_Static_assert(SYS_WINDOW_STATE == 85ULL,
               "ABI drift: SYS_WINDOW_STATE — see SYSCALLS.md");

#define SYS_TIMEINFO           100ULL
_Static_assert(SYS_TIMEINFO == 100ULL, "ABI drift: SYS_TIMEINFO — see SYSCALLS.md");

#define SYS_MEMINFO            101ULL
_Static_assert(SYS_MEMINFO == 101ULL, "ABI drift: SYS_MEMINFO — see SYSCALLS.md");

#define SYS_PROCLIST           102ULL
_Static_assert(SYS_PROCLIST == 102ULL, "ABI drift: SYS_PROCLIST — see SYSCALLS.md");

/*
 * Range checks. Tests in tests/test_syscall_abi.c assert that the
 * kernel rejects unknown syscall numbers with ERR_INVAL and that no
 * implemented syscall number lives outside these ranges. New numbers
 * must be appended to one of the ranges below and a row added to
 * SYSCALLS.md in the same commit.
 */
_Static_assert(SYS_EXIT          >= 1ULL  && SYS_EXIT          <= 8ULL,  "ABI drift: process range");
_Static_assert(SYS_MMAP          >= 20ULL && SYS_MMAP          <= 21ULL, "ABI drift: memory range");
_Static_assert(SYS_OPEN          >= 40ULL && SYS_OPEN          <= 48ULL, "ABI drift: vfs range");
_Static_assert(SYS_IPC_SEND      >= 60ULL && SYS_IPC_SEND      <= 61ULL, "ABI drift: ipc range");
_Static_assert(SYS_WINDOW_CREATE >= 70ULL && SYS_WINDOW_STATE       <= 85ULL, "ABI drift: window range");
_Static_assert(SYS_TIMEINFO      >= 100ULL && SYS_PROCLIST     <= 102ULL, "ABI drift: info range");

#endif
