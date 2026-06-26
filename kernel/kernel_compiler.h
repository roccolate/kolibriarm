#ifndef KOLIBRIARM_KERNEL_COMPILER_H
#define KOLIBRIARM_KERNEL_COMPILER_H

#define KERNEL_ALIGNED(n) __attribute__((aligned(n)))
#define KERNEL_ALWAYS_INLINE inline __attribute__((always_inline))
#define KERNEL_PACKED __attribute__((packed))
#define KERNEL_PACKED_ALIGNED(n) __attribute__((packed, aligned(n)))
#define KERNEL_SECTION(name) __attribute__((section(name)))
#define KERNEL_UNUSED __attribute__((unused))
#define KERNEL_PRINTF_FORMAT(fmt_index, first_arg) \
    __attribute__((format(printf, fmt_index, first_arg)))

#endif
