.section .text
.global mmu_enable_identity
.type mmu_enable_identity, %function

// void mmu_enable_identity(uint64_t *pgd)
mmu_enable_identity:
    dsb sy

    // Attr0: Normal WB/WA, Attr1: Device-nGnRnE.
    mov x1, #0xff
    msr mair_el1, x1

    // T0SZ=16 (48-bit VA), TTBR0 WB/WA inner-shareable, TG0=4KB,
    // EPD1=1 (TTBR1 disabled for now), IPS=40-bit PA.
    ldr x1, =0x0000000200803510
    msr tcr_el1, x1

    msr ttbr0_el1, x0
    isb

    tlbi vmalle1
    dsb ish
    isb

    mrs x1, sctlr_el1
    orr x1, x1, #0x1
    msr sctlr_el1, x1
    isb

    ret

.global mmu_read_sctlr_el1
.type mmu_read_sctlr_el1, %function

// uint64_t mmu_read_sctlr_el1(void)
mmu_read_sctlr_el1:
    mrs x0, sctlr_el1
    ret
