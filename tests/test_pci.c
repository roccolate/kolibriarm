#include <stdint.h>

#include "unity/unity.h"
#include "../drivers/pci/pci.h"

void test_pci_bar_address_extracts_32bit_mmio(void) {
    /* Standard 32-bit MMIO BAR: bits 0..3 are flags; bits 4..31 are base. */
    uint32_t bar = 0xFE000000U | 0x04U; /* prefetchable */
    TEST_ASSERT_EQUAL_UINT64(0xFE000000ULL, pci_bar_address(bar, 0, 0));
}

void test_pci_bar_address_ignores_io_space_bar(void) {
    /* Bit 0 set: I/O space BAR; not a usable MMIO base. */
    uint32_t bar = 0x0000F000U | 0x01U;
    TEST_ASSERT_EQUAL_UINT64(0ULL, pci_bar_address(bar, 0, 0));
}

void test_pci_bar_address_combines_64bit_pair(void) {
    /*
     * 64-bit BARs use two consecutive registers: low (bits 4..31) and
     * high (the upper 32 bits of the address). The low BAR also has
     * the prefetchable bit set.
     */
    uint32_t low = 0xFE000000U | 0x04U;
    uint32_t high = 0x00000001U;
    TEST_ASSERT_EQUAL_UINT64(0x1FE000000ULL,
                             pci_bar_address(low, high, 1));
}

void test_pci_bar_address_low_32bit_ignores_next_bar(void) {
    /* 32-bit BAR: the second register is unrelated even if non-zero. */
    uint32_t low = 0xFE000000U | 0x04U;
    TEST_ASSERT_EQUAL_UINT64(0xFE000000ULL,
                             pci_bar_address(low, 0xFFFFFFFFU, 0));
}