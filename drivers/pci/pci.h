#ifndef KOLIBRIARM_DRIVERS_PCI_PCI_H
#define KOLIBRIARM_DRIVERS_PCI_PCI_H

#include <stdint.h>

/*
 * Minimal PCI / PCIe enumeration for the KolibriARM kernel.
 *
 * The code targets the PCIe Enhanced Configuration Access Mechanism
 * (ECAM). On QEMU's aarch64 virt machine ECAM lives at 0xF0000000
 * for a 16 MB window. Each device's 4 KB config space is reached via
 *
 *     addr = ECAM_BASE + (bus << 20) | (device << 15) | (func << 12) + reg
 *
 * where device is 0..31, func is 0..7, and reg is a byte offset within
 * the 256-byte legacy config space.
 *
 * We do not implement PCI I/O ports (the aarch64 virt machine does not
 * expose them) and we do not support PCIe extended config space beyond
 * 256 bytes per function; the drivers we ship today only need the
 * legacy registers.
 */

#define PCI_ECAM_BASE       0x4010000000ULL
#define PCI_ECAM_SIZE       0x01000000ULL
#define PCI_MAX_BUSES       1U
#define PCI_MAX_DEVICES     32U
#define PCI_MAX_FUNCS       8U

/* Class code / subclass / progIF masks in the high config words. */
#define PCI_CLASS_SERIAL    0x0700U
#define PCI_CLASS_SATA      0x0106U
#define PCI_CLASS_NET       0x0200U
#define PCI_CLASS_DISPLAY   0x0300U
#define PCI_CLASS_HID       0x0900U /* Boot interface subclass 0x01 */
#define PCI_CLASS_USB        0x0C03U /* Serial bus, USB */

/* Standard config space registers. */
#define PCI_CFG_VENDOR       0x00U
#define PCI_CFG_DEVICE       0x02U
#define PCI_CFG_COMMAND      0x04U
#define PCI_CFG_STATUS       0x06U
#define PCI_CFG_CLASS_REV    0x08U
#define PCI_CFG_HDR_TYPE     0x0EU
#define PCI_CFG_BAR0         0x10U
#define PCI_CFG_BAR1         0x14U
#define PCI_CFG_BAR2         0x18U
#define PCI_CFG_BAR3         0x1CU
#define PCI_CFG_BAR4         0x20U
#define PCI_CFG_BAR5         0x24U

/* Command register bits. */
#define PCI_CMD_IO_SPACE     (1U << 0)
#define PCI_CMD_MEM_SPACE    (1U << 1)
#define PCI_CMD_BUS_MASTER   (1U << 2)

/* Status register bits. */
#define PCI_STS_CAP_LIST     (1U << 4)

/* Header type low 7 bits. */
#define PCI_HDR_TYPE_MASK    0x7FU
#define PCI_HDR_TYPE_PCI     0x00U

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t class_code;
    uint8_t header_type;
    uint32_t bar[6];
} pci_device_t;

/*
 * Read a 32-bit word from the device's config space. Returns 0xFFFF
 * when the device does not respond (no device or bad address).
 */
uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function,
                           uint8_t reg);

uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function,
                           uint8_t reg);

uint8_t pci_config_read8(uint8_t bus, uint8_t device, uint8_t function,
                         uint8_t reg);

/*
 * Walk the ECAM and copy every present device into out_devices.
 * Returns the number of devices written; caps at max_devices.
 * Only bus 0 is scanned; the QEMU aarch64 virt machine only puts
 * built-in devices on bus 0.
 */
uint32_t pci_enumerate(pci_device_t *out_devices, uint32_t max_devices);

/* Assign MMIO BAR addresses for every enumerated device whose BAR
 * is unimplemented (typically because QEMU's direct boot leaves
 * them zero). Writes the assigned address back to the device and
 * returns the number of BARs assigned. */
uint32_t pci_assign_bars(pci_device_t *devices, uint32_t device_count,
                         uint32_t base_address, uint32_t step);

/* Write a 32-bit value to a device's config space. Used to assign
 * BAR addresses when the firmware (or absence thereof) left them
 * zero. */
void pci_config_write32(uint8_t bus, uint8_t device, uint8_t function,
                        uint8_t reg, uint32_t value);

void pci_config_write16(uint8_t bus, uint8_t device, uint8_t function,
                        uint8_t reg, uint16_t value);

void pci_config_write8(uint8_t bus, uint8_t device, uint8_t function,
                       uint8_t reg, uint8_t value);

/*
 * Translate a PCI BAR value into a 64-bit MMIO base address. Type 0
 * headers expose 32-bit BARs only; bit 0 of the BAR indicates whether
 * the BAR is an MMIO range or an I/O range; bits 1..2 indicate the
 * type. For 64-bit MMIO we OR the next BAR into the high half.
 */
uint64_t pci_bar_address(uint32_t bar_value, uint64_t next_bar_value,
                         int is_64bit);

#endif