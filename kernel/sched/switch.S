.section .text

// Cooperative kernel-thread context switch.
//
// The C scheduler owns thread selection. This boundary only preserves the
// AArch64 callee-saved register set plus SP/LR, matching sched_context_t in
// sched.c. New kernel threads enter through sched_thread_trampoline with x19 as
// the entry function and x20 as its argument.
.global switch_context
.type switch_context, %function

// void switch_context(sched_context_t *old_context, sched_context_t *new_context)
switch_context:
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    str x29, [x0, #80]
    mov x2, sp
    str x2, [x0, #88]
    str x30, [x0, #96]

    ldp x19, x20, [x1, #0]
    ldp x21, x22, [x1, #16]
    ldp x23, x24, [x1, #32]
    ldp x25, x26, [x1, #48]
    ldp x27, x28, [x1, #64]
    ldr x29, [x1, #80]
    ldr x2, [x1, #88]
    mov sp, x2
    ldr x30, [x1, #96]
    ret

.global sched_thread_trampoline
.type sched_thread_trampoline, %function

sched_thread_trampoline:
    mov x0, x20
    blr x19
    bl sched_thread_exit

1:
    wfe
    b 1b
