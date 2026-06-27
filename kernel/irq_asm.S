.section .text

// IRQ mask helpers.
//
// DAIF bit #2 is the AArch64 IRQ mask. These helpers are intentionally tiny so
// C code can bracket scheduler-critical regions without learning DAIF encoding
// details. The ISB makes the mask change visible before the caller continues.
.global irq_enable
.type irq_enable, %function

irq_enable:
    msr daifclr, #2
    isb
    ret

.global irq_disable
.type irq_disable, %function

irq_disable:
    msr daifset, #2
    isb
    ret
