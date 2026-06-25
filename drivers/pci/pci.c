#include "pci/pci.h"

#include <stdint.h>

static inline uint32_t ecam_offset(uint8_t bus, uint8_t device,
                                   uint8_t function, uint8_t reg) {
    /*
     * PCIe ECAM addressing: each bus spans 1 MB, each device 32 KB,
     * each function 4 KB. The 256-byte legacy config space starts at
     * the device's base; we read 32-bit words at byte offsets.
     */
    uint32_t bus_off = (uint32_t)bus * 0x100000U;
    uint32_t dev_off = ((uint32_t)device & 0x1FU) * 0x8000U;
    uint32_t func_off = ((uint32_t)function & 0x07U) * 0x1000U;
    return bus_off + dev_off + func_off + (uint32_t)reg;
}

static inline volatile uint32_t *ecam_ptr(uint8_t bus, uint8_t device,
                                          uint8_t function, uint8_t reg) {
    return (volatile uint32_t *)(PCI_ECAM_BASE + ecam_offset(bus, device,
                                                          function, reg));
}

uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function,
                           uint8_t reg) {
    volatile uint32_t *p = ecam_ptr(bus, device, function, reg);
    return *p;
}

uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function,
                           uint8_t reg) {
    volatile uint16_t *p =
        (volatile uint16_t *)(PCI_ECAM_BASE + ecam_offset(bus, device,
                                                        function, reg));
    return *p;
}

uint8_t pci_config_read8(uint8_t bus, uint8_t device, uint8_t function,
                         uint8_t reg) {
    volatile uint8_t *p =
        (volatile uint8_t *)(PCI_ECAM_BASE + ecam_offset(bus, device,
                                                      function, reg));
    return *p;
}

void pci_config_write32(uint8_t bus, uint8_t device, uint8_t function,
                        uint8_t reg, uint32_t value) {
    volatile uint32_t *p = ecam_ptr(bus, device, function, reg);
    *p = value;
}

void pci_config_write16(uint8_t bus, uint8_t device, uint8_t function,
                        uint8_t reg, uint16_t value) {
    volatile uint16_t *p =
        (volatile uint16_t *)(PCI_ECAM_BASE + ecam_offset(bus, device,
                                                          function, reg));
    *p = value;
}

void pci_config_write8(uint8_t bus, uint8_t device, uint8_t function,
                       uint8_t reg, uint8_t value) {
    volatile uint8_t *p =
        (volatile uint8_t *)(PCI_ECAM_BASE + ecam_offset(bus, device,
                                                          function, reg));
    *p = value;
}

uint32_t pci_enumerate(pci_device_t *out_devices, uint32_t max_devices) {
    uint32_t count = 0;

    for (uint8_t bus = 0; bus < PCI_MAX_BUSES; bus++) {
        for (uint8_t device = 0; device < PCI_MAX_DEVICES; device++) {
            for (uint8_t func = 0; func < PCI_MAX_FUNCS; func++) {
                uint16_t vendor =
                    pci_config_read16(bus, device, func, PCI_CFG_VENDOR);
                if (vendor == 0xFFFFU) {
                    /* Function 0 not present. Skip the rest of this
                     * device's functions; for non-multifunction devices
                     * we don't waste cycles probing 1..7. */
                    if (func == 0) {
                        break;
                    }
                    continue;
                }

                if (count >= max_devices) {
                    return count;
                }

                pci_device_t *d = &out_devices[count++];
                d->bus = bus;
                d->device = device;
                d->function = func;
                d->vendor_id = vendor;
                d->device_id =
                    pci_config_read16(bus, device, func, PCI_CFG_DEVICE);
                uint32_t class_rev =
                    pci_config_read32(bus, device, func, PCI_CFG_CLASS_REV);
                /* class_rev is little-endian: bits 0-7 = rev, 8-15 = progIF,
                 * 16-23 = subclass, 24-31 = class. We pack class|class_code
                 * into a 16-bit field. */
                d->class_code = (uint16_t)(class_rev >> 16);
                d->prog_if = (uint8_t)(class_rev >> 8);
                d->header_type = (uint8_t)(
                    pci_config_read8(bus, device, func, PCI_CFG_HDR_TYPE) &
                    PCI_HDR_TYPE_MASK);
                for (uint32_t b = 0; b < 6; b++) {
                    d->bar[b] = pci_config_read32(
                        bus, device, func, (uint8_t)(PCI_CFG_BAR0 + b * 4U));
                }
            }
        }
    }

    return count;
}

uint64_t pci_bar_address(uint32_t bar_value, uint64_t next_bar_value,
                         int is_64bit) {
    if ((bar_value & 0x01U) != 0U) {
        /* I/O space BAR — the kernel does not use I/O space today. */
        return 0;
    }
    if (is_64bit) {
        uint64_t lo = (uint64_t)(bar_value & 0xFFFFFFF0U);
        uint64_t hi = (next_bar_value & 0xFFFFFFFFU) << 32;
        return hi | lo;
    }
    return (uint64_t)(bar_value & 0xFFFFFFF0U);
}

uint32_t pci_assign_bars(pci_device_t *devices, uint32_t device_count,
                         uint32_t base_address, uint32_t step) {
    (void)step;
    uint32_t cursor = base_address;
    uint32_t assigned = 0;
    for (uint32_t i = 0; i < device_count; i++) {
        pci_device_t *d = &devices[i];
        for (uint32_t b = 0; b < 6; b++) {
            uint8_t reg = (uint8_t)(PCI_CFG_BAR0 + b * 4U);
            /* Skip if the BAR already has a non-zero address
             * (bits 4..31). The type bits (0..3) are allowed to be
             * non-zero -- the address field is what tells us the
             * BAR has been programmed. */
            if ((d->bar[b] & 0xFFFFFFF0U) != 0U) {
                continue;
            }
            /* Probe the size: write 0xFFFFFFFF and read back. The
             * low bits stay as the type field; the address bits
             * become the bitwise complement of the size. */
            pci_config_write32(d->bus, d->device, d->function, reg,
                               0xFFFFFFFFU);
            uint32_t probe = pci_config_read32(d->bus, d->device,
                                                d->function, reg);
            /* Unimplemented BARs return 0. */
            if (probe == 0U) {
                continue;
            }
            uint32_t flags = probe & 0x0FU;
            uint32_t mem_type = probe & 0x06U;
            /* Only handle 32-bit and 64-bit MMIO BARs. */
            if (mem_type != 0x00U && mem_type != 0x04U) {
                continue;
            }
            uint32_t size_mask = probe & 0xFFFFFFF0U;
            uint32_t size = (~size_mask) + 1U;
            if (size == 0U || size > 0x10000000U) {
                /* Skip BARs with absurd sizes (alignment to 256MB
                 * suggests we read garbage). */
                pci_config_write32(d->bus, d->device, d->function, reg,
                                   d->bar[b]);
                continue;
            }
            /* Align the cursor to the size. */
            uint32_t aligned = (cursor + size - 1U) & ~(size - 1U);
            pci_config_write32(d->bus, d->device, d->function, reg,
                               aligned | flags);
            d->bar[b] = aligned | flags;
            if (mem_type == 0x04U && b + 1U < 6U) {
                pci_config_write32(d->bus, d->device, d->function,
                                   (uint8_t)(reg + 4U), 0);
                d->bar[b + 1U] = 0;
                b++;
            }
            cursor = aligned + size;
            assigned++;
        }
    }
    return assigned;
}
