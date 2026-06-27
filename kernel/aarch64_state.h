#ifndef KOLIBRIARM_KERNEL_AARCH64_STATE_H
#define KOLIBRIARM_KERNEL_AARCH64_STATE_H

/*
 * Saved Program Status Register values used when returning through
 * the EL0/EL1 exception frame.
 *
 * Keep these architectural constants out of generic layout headers: they
 * describe CPU exception-return state, not memory placement.
 */
#define AARCH64_SPSR_EL0T_DAF_MASKED  0x340ULL
#define AARCH64_SPSR_EL1H_DAIF_MASKED 0x3c5ULL

#endif
