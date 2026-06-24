// libkarm/crt0.S
//
// Shared `_start` for every KolibriARM userland app.
//
// The kernel delivers `x0 = argc` and `x1 = &argv[0]` for
// sys_spawn_argv (8) per the AArch64 procedure-call ABI. Apps spawned
// via plain sys_spawn (without argv) land here with `x0 = 0`,
// `x1 = 0`; crt0 forwards that as `argc = 0`, `argv = NULL` without
// crashing.
//
// _start calls `int main(int argc, char **argv)` and forwards the
// return value to sys_exit (1). The integer return value is
// sign-extended to long so negative exit codes survive.
//
// If `main` ever returns for any reason (it normally does not — the
// app's main either loops or calls kli_exit), the kernel still sees
// a clean SYS_EXIT trap rather than a random instruction fetch.

.section .user.image.text, "ax"

.global _start
.type _start, %function

.extern main

_start:
    // x0 = argc, x1 = argv already per AArch64 PCS at process entry.
    bl main
    // main returns int in w0. Sign-extend to long for sys_exit.
    sxtw x0, w0
    mov x8, #1                 // SYS_EXIT
    svc #0

1:
    wfe
    b 1b
