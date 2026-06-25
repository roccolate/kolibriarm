// libkarm/syscall.S
//
// The only place in all of userland that issues the `svc #0` instruction.
// Each trampoline takes a syscall number plus 0..7 register arguments and
// returns the raw `x0` value (long). The typed C wrappers in syscall.h
// call into these; they do no interpretation of the return value.
//
// KolibriARM syscall ABI (kernel/syscall.c):
//   x0..x6 = arguments
//   x8     = syscall number
//   x0     = return value (negative on error)
//
// All trampolines are C-callable: they take the syscall number as the
// first C argument (in x0), shuffle it into x8, move the remaining
// arguments into x0..x6, and trap. x0..x7 are caller-saved across `svc`
// by the AArch64 ABI, which matches the kernel's exception path.

.section .user.image.text, "ax"

.global __syscall0
.global __syscall1
.global __syscall2
.global __syscall3
.global __syscall4
.global __syscall5
.global __syscall6
.global __syscall7

.type __syscall0, %function
.type __syscall1, %function
.type __syscall2, %function
.type __syscall3, %function
.type __syscall4, %function
.type __syscall5, %function
.type __syscall6, %function
.type __syscall7, %function

// long __syscall0(long n)
__syscall0:
    mov x8, x0
    svc #0
    ret

// long __syscall1(long n, long a0)
__syscall1:
    mov x8, x0
    mov x0, x1
    svc #0
    ret

// long __syscall2(long n, long a0, long a1)
__syscall2:
    mov x8, x0
    mov x0, x1
    mov x1, x2
    svc #0
    ret

// long __syscall3(long n, long a0, long a1, long a2)
__syscall3:
    mov x8, x0
    mov x0, x1
    mov x1, x2
    mov x2, x3
    svc #0
    ret

// long __syscall4(long n, long a0, long a1, long a2, long a3)
__syscall4:
    mov x8, x0
    mov x0, x1
    mov x1, x2
    mov x2, x3
    mov x3, x4
    svc #0
    ret

// long __syscall5(long n, long a0, long a1, long a2, long a3, long a4)
__syscall5:
    mov x8, x0
    mov x0, x1
    mov x1, x2
    mov x2, x3
    mov x3, x4
    mov x4, x5
    svc #0
    ret

// long __syscall6(long n, long a0, long a1, long a2, long a3, long a4, long a5)
__syscall6:
    mov x8, x0
    mov x0, x1
    mov x1, x2
    mov x2, x3
    mov x3, x4
    mov x4, x5
    mov x5, x6
    svc #0
    ret

// long __syscall7(long n, long a0, long a1, long a2, long a3, long a4, long a5, long a6)
__syscall7:
    mov x8, x0
    mov x0, x1
    mov x1, x2
    mov x2, x3
    mov x3, x4
    mov x4, x5
    mov x5, x6
    mov x6, x7
    svc #0
    ret
