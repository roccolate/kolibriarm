#ifndef KOLIBRIARM_KERNEL_USER_EXIT_H
#define KOLIBRIARM_KERNEL_USER_EXIT_H

/*
 * Observable EL0 process exit codes that are not supplied by the process
 * itself. Waiters can see these values, so they are part of the process ABI.
 *
 * Keep these values stable with SYSCALLS.md and the syscall ABI tests.
 */
#define KERNEL_USER_KILL_EXIT_CODE 0x80ULL
#define KERNEL_USER_FAULT_EXIT_CODE 0xfffffffffffffff0ULL

#endif
