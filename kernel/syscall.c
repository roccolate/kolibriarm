#include "kernel/syscall.h"

#include <stdint.h>

#include "kernel/process.h"
#include "kernel/sched/sched.h"
#include "kernel/user_demo.h"
#include "kernel/user_vm.h"
#include "uart/pl011.h"

#define SYS_EXIT  1ULL
#define SYS_YIELD 2ULL
#define SYS_GETPID 3ULL
#define SYS_MMAP 20ULL
#define SYS_MUNMAP 21ULL
#define SYS_WRITE 43ULL

#define FD_STDOUT 1ULL
#define FD_STDERR 2ULL

#define ERR_BADF  (-5LL)
#define ERR_INVAL (-7LL)

#define SPSR_EL1H_MASKED 0x3c5ULL

static int user_range_contains(uint64_t ptr, uint64_t len) {
    return process_user_range_contains(process_current(), ptr, len);
}

static int64_t sys_write(uint64_t fd, uint64_t buf, uint64_t len) {
    const char *text = (const char *)(uintptr_t)buf;

    if (fd != FD_STDOUT && fd != FD_STDERR) {
        return ERR_BADF;
    }

    if (!user_range_contains(buf, len)) {
        return ERR_INVAL;
    }

    for (uint64_t i = 0; i < len; i++) {
        uart_putc(text[i]);
    }

    return (int64_t)len;
}

static int64_t sys_munmap(process_t *process, uint64_t addr, uint64_t size) {
    return user_vm_unmap_anonymous(process, addr, size);
}

static int64_t sys_mmap(process_t *process, uint64_t hint, uint64_t size, uint64_t flags) {
    return user_vm_map_anonymous(process, hint, size, flags);
}

static int sys_yield_process(exception_frame_t *frame) {
    process_t *current = process_current();
    process_t *next = process_next_runnable(current);

    if (current == 0 || next == 0) {
        return 0;
    }

    if (current->state == PROCESS_RUNNING) {
        current->state = PROCESS_READY;
    }

    next->state = PROCESS_RUNNING;
    process_set_current(next);
    process_activate_context(next, frame);

    return 1;
}

static void sys_exit(exception_frame_t *frame, uint64_t code) {
    process_t *current = process_current();
    process_t *next;

    process_mark_exited(current, code);

    uart_puts("USER exit: ");
    static const char digits[] = "0123456789abcdef";
    uart_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        uart_putc(digits[(code >> shift) & 0xf]);
    }
    uart_puts("\n");

    next = process_next_runnable(current);
    if (next != 0) {
        next->state = PROCESS_RUNNING;
        process_set_current(next);
        process_activate_context(next, frame);
        return;
    }

    frame->x[0] = code;
    frame->elr = user_demo_return_address();
    frame->spsr = SPSR_EL1H_MASKED;
}

void syscall_dispatch(exception_frame_t *frame) {
    process_t *current = process_current();

    if (current != 0) {
        process_save_context(current, frame->x, frame->elr, frame->spsr,
                             frame->sp_el0);
    }

    switch (frame->x[8]) {
    case SYS_EXIT:
        sys_exit(frame, frame->x[0]);
        break;
    case SYS_YIELD:
        if (!sys_yield_process(frame)) {
            sched_yield();
            frame->x[0] = 0;
        }
        break;
    case SYS_GETPID:
        if (current == 0) {
            frame->x[0] = (uint64_t)ERR_INVAL;
        } else {
            frame->x[0] = current->pid;
        }
        break;
    case SYS_MMAP:
        frame->x[0] = (uint64_t)sys_mmap(current, frame->x[0], frame->x[1], frame->x[2]);
        break;
    case SYS_MUNMAP:
        frame->x[0] = (uint64_t)sys_munmap(current, frame->x[0], frame->x[1]);
        break;
    case SYS_WRITE:
        frame->x[0] = (uint64_t)sys_write(frame->x[0], frame->x[1], frame->x[2]);
        break;
    default:
        frame->x[0] = (uint64_t)ERR_INVAL;
        break;
    }

    process_t *after = process_current();
    if (after != 0) {
        after->regs[0] = frame->x[0];
        after->pc = frame->elr;
        after->pstate = frame->spsr;
        after->sp = frame->sp_el0;
    }
}
