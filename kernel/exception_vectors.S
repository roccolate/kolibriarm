.section .text
.global exception_init
.type exception_init, %function

exception_init:
    ldr x0, =exception_vectors
    msr vbar_el1, x0
    isb
    ret

.global exception_vector_base
.type exception_vector_base, %function

exception_vector_base:
    mrs x0, vbar_el1
    ret

.macro VECTOR label, kind
.align 7
\label:
    sub sp, sp, #32
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]

    mrs x0, esr_el1
    mrs x1, far_el1
    mrs x2, elr_el1
    mov x3, #\kind
    bl exception_handler

1:
    wfe
    b 1b
.endm

.macro IRQ_VECTOR label
.align 7
\label:
    b irq_entry
.endm

.macro IRQ_ENTRY
irq_entry:
    sub sp, sp, #256
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]

    bl irq_handler

    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    add sp, sp, #256
    eret
.endm

.align 11
.global exception_vectors
.type exception_vectors, %function

exception_vectors:
    VECTOR current_el_sp0_sync,   0
    IRQ_VECTOR current_el_sp0_irq
    VECTOR current_el_sp0_fiq,    2
    VECTOR current_el_sp0_serror, 3

    VECTOR current_el_spx_sync,   4
    IRQ_VECTOR current_el_spx_irq
    VECTOR current_el_spx_fiq,    6
    VECTOR current_el_spx_serror, 7

    VECTOR lower_el_a64_sync,     8
    IRQ_VECTOR lower_el_a64_irq
    VECTOR lower_el_a64_fiq,      10
    VECTOR lower_el_a64_serror,   11

    VECTOR lower_el_a32_sync,     12
    IRQ_VECTOR lower_el_a32_irq
    VECTOR lower_el_a32_fiq,      14
    VECTOR lower_el_a32_serror,   15

IRQ_ENTRY
