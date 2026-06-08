.section .text
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
