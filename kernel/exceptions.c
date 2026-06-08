#include "kernel/exceptions.h"

#include <stdint.h>

#include "uart/pl011.h"

static void print_hex64(uint64_t value) {
    static const char digits[] = "0123456789abcdef";

    uart_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        uart_putc(digits[(value >> shift) & 0xf]);
    }
}

static const char *exception_name(uint64_t kind) {
    switch (kind) {
    case 0:
        return "current EL SP0 sync";
    case 1:
        return "current EL SP0 irq";
    case 2:
        return "current EL SP0 fiq";
    case 3:
        return "current EL SP0 serror";
    case 4:
        return "current EL SPx sync";
    case 5:
        return "current EL SPx irq";
    case 6:
        return "current EL SPx fiq";
    case 7:
        return "current EL SPx serror";
    case 8:
        return "lower EL AArch64 sync";
    case 9:
        return "lower EL AArch64 irq";
    case 10:
        return "lower EL AArch64 fiq";
    case 11:
        return "lower EL AArch64 serror";
    case 12:
        return "lower EL AArch32 sync";
    case 13:
        return "lower EL AArch32 irq";
    case 14:
        return "lower EL AArch32 fiq";
    case 15:
        return "lower EL AArch32 serror";
    default:
        return "unknown";
    }
}

void exception_handler(uint64_t esr, uint64_t far, uint64_t elr, uint64_t kind) {
    uart_puts("\nEXCEPTION: ");
    uart_puts(exception_name(kind));
    uart_puts("\nESR_EL1: ");
    print_hex64(esr);
    uart_puts("\nFAR_EL1: ");
    print_hex64(far);
    uart_puts("\nELR_EL1: ");
    print_hex64(elr);
    uart_puts("\n");

    for (;;) {
        __asm__ volatile("wfe");
    }
}
