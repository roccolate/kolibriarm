#ifndef KOLIBRIARM_QEMU_VIRT_BOARD_H
#define KOLIBRIARM_QEMU_VIRT_BOARD_H

#include <stdint.h>

#include "drivers/board.h"

#define QEMU_VIRT_UART0_BASE     0x09000000ULL
#define QEMU_VIRT_UART0_IRQ      33U
#define QEMU_VIRT_GIC_DIST_BASE  0x08000000ULL
#define QEMU_VIRT_GIC_CPU_BASE   0x08010000ULL
#define QEMU_VIRT_GIC_MMIO_SIZE  0x00020000ULL
#define QEMU_VIRT_VIRTIO_MMIO_BASE 0x0a000000ULL
#define QEMU_VIRT_VIRTIO_MMIO_SIZE 0x00004000ULL
#define QEMU_VIRT_VIRTIO_MMIO_STRIDE 0x00000200ULL
#define QEMU_VIRT_VIRTIO_INPUT_IRQ  51U

/*
 * PCIe ECAM and MMIO window for the qemu-virt aarch64 machine.
 *
 * QEMU 8.2 virt defaults the ECAM to high memory (highmem=on) at
 * 0x4010000000 with a 256 MB window. The MMIO range is split
 * between a 0x10000000..0x3EFEFFFF low window and a high window
 * above 0x8000000000. We map the ECAM and both MMIO windows so
 * PCI enumeration and BAR access both work.
 */
#define QEMU_VIRT_PCIE_ECAM_BASE  0x4010000000ULL
#define QEMU_VIRT_PCIE_ECAM_SIZE  0x01000000ULL
#define QEMU_VIRT_PCIE_MMIO_BASE  0x10000000ULL
#define QEMU_VIRT_PCIE_MMIO_SIZE  0x30000000ULL
#define QEMU_VIRT_PCIE_MMIO_HIGH_BASE 0x8000000000ULL
#define QEMU_VIRT_PCIE_MMIO_HIGH_SIZE 0x8000000000ULL

#endif
