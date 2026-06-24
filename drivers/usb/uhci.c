#include "usb/uhci.h"

#include <stdint.h>

/*
 * Minimal UHCI driver (poll mode).
 *
 * One frame list (4 KB, 1024 entries) plus a 32-TD pool (512 B)
 * per controller. Transfers run synchronously on the caller's
 * thread; we mask the controller's IRQ line so a stray USBINT
 * during a transfer does not trip the GIC.
 *
 * Layout (all identity-mapped physical memory):
 *
 *   frame_list[0..1023]   uint32_t, terminated on init
 *   td_pool[0..31]        16-byte TDs, indexed by td_next
 *   xfer_buffer[64]       data stage scratch (used by control xfer)
 *
 * Frame list slots 0..2 are reserved for the active control xfer
 * chain; after the xfer finishes the slots are restored to TERM.
 * The remaining 1021 slots stay on the terminator forever.
 */

#define UHCI_FRAME_LIST_ENTRIES 1024U
#define UHCI_TD_POOL_SIZE        32U

typedef struct {
    uint8_t frame_list_storage[4096] __attribute__((aligned(4096)));
    uint8_t td_pool_storage[512] __attribute__((aligned(16)));
    uint8_t xfer_buffer[64] __attribute__((aligned(4)));
    uint8_t setup_buffer[16] __attribute__((aligned(4)));
    uint32_t td_next;
    uint8_t in_xfer;
} uhci_priv_t;

typedef struct __attribute__((aligned(16))) {
    uint32_t link;     /* Bits 0..3: T/QH/depth. Bits 4..31: phys addr. */
    uint32_t status;   /* Bit 23 ACTIVE, 24 IOC, 26 LS, 27-28 C_ERR. */
    uint32_t token;    /* Bits 0-7 PID, 8-14 dev, 15-18 ep, 19 DT, 21-31 max. */
    uint32_t buffer;   /* Physical address of the data buffer. */
} uhci_td_t;

static uhci_priv_t g_priv[2];
static uhci_controller_t g_controllers[2];
static uint32_t g_controller_count;

static inline uint16_t uhci_reg16(uint64_t base, uint32_t off) {
    return *(volatile uint16_t *)(base + off);
}

static inline uint32_t uhci_reg32(uint64_t base, uint32_t off) {
    return *(volatile uint32_t *)(base + off);
}

static inline void uhci_write16(uint64_t base, uint32_t off, uint16_t v) {
    *(volatile uint16_t *)(base + off) = v;
}

static inline void uhci_write32(uint64_t base, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(base + off) = v;
}

static inline uint32_t td_phys(const uhci_td_t *td) {
    return (uint32_t)(uintptr_t)td;
}

static uhci_td_t *td_alloc(uint32_t idx) {
    if (g_priv[idx].td_next >= UHCI_TD_POOL_SIZE) {
        return 0;
    }
    uhci_td_t *td =
        (uhci_td_t *)(g_priv[idx].td_pool_storage +
                      g_priv[idx].td_next * 16U);
    g_priv[idx].td_next++;
    return td;
}

static void td_reset_pool(uint32_t idx) {
    g_priv[idx].td_next = 0;
}

/*
 * Make a token (TD dword 1) from a PID, device address, endpoint
 * number, data-toggle bit, and max packet size. UHCI encodes the
 * max packet size in bits 21..31 (11 bits) which limits it to 1024
 * bytes -- well above the 8/16/32/64 used by HID endpoints.
 */
static uint32_t td_make_token(uint8_t pid, uint8_t dev, uint8_t ep,
                              uint8_t data_toggle, uint16_t max_packet) {
    uint32_t t = (uint32_t)pid;
    t |= ((uint32_t)(dev & 0x7FU)) << 8U;
    t |= ((uint32_t)(ep & 0x0FU)) << 15U;
    if (data_toggle) {
        t |= (1U << 19U);
    }
    t |= ((uint32_t)(max_packet & 0x7FFU)) << 21U;
    return t;
}

uint32_t uhci_pci_probe(uhci_controller_t *out, uint32_t max_count) {
    pci_device_t devices[PCI_MAX_BUSES * PCI_MAX_DEVICES * PCI_MAX_FUNCS];
    uint32_t n = pci_enumerate(devices,
                               PCI_MAX_BUSES * PCI_MAX_DEVICES *
                                   PCI_MAX_FUNCS);
    uint32_t found = 0;
    for (uint32_t i = 0; i < n && found < max_count; i++) {
        if (devices[i].class_code != PCI_CLASS_USB) {
            continue;
        }
        for (uint32_t b = 0; b < 6; b++) {
            uint32_t bar = devices[i].bar[b];
            if ((bar & 0x01U) != 0U) {
                continue;
            }
            out[found].mmio_base = (uint64_t)(bar & 0xFFFFFFF0U);
            out[found].device_addr = 0;
            out[found].endpoint_in = 0;
            out[found].max_packet = 8;
            out[found].priv_index = (uint8_t)found;
            found++;
            break;
        }
    }
    g_controller_count = found;
    return found;
}

int uhci_init(uhci_controller_t *ctrl) {
    if (ctrl == 0 || ctrl->priv_index >= 2U) {
        return -1;
    }
    uint32_t idx = ctrl->priv_index;
    uint64_t base = ctrl->mmio_base;
    if (base == 0) {
        return -1;
    }
    /* Stop the controller if it is running. */
    uhci_write16(base, UHCI_REG_CMD, 0);
    uhci_write16(base, UHCI_REG_STS, 0xFFFFU);
    uhci_write16(base, UHCI_REG_INTR, 0);

    /* Global reset. QEMU returns within a few hundred iterations. */
    uhci_write16(base, UHCI_REG_CMD, UHCI_CMD_GRESET);
    for (volatile int i = 0; i < 200000; i++) {
        if ((uhci_reg16(base, UHCI_REG_CMD) & UHCI_CMD_GRESET) == 0) {
            break;
        }
    }

    td_reset_pool(idx);
    g_priv[idx].in_xfer = 0;

    /* Terminate every frame list entry. */
    for (uint32_t i = 0; i < UHCI_FRAME_LIST_ENTRIES; i++) {
        *(volatile uint32_t *)(g_priv[idx].frame_list_storage + i * 4U) =
            UHCI_PTR_TERM;
    }
    uhci_write32(base, UHCI_REG_FLBASE,
                 (uint32_t)(uintptr_t)g_priv[idx].frame_list_storage);
    uhci_write16(base, UHCI_REG_FRNUM, 0);
    uhci_write16(base, UHCI_REG_STS, 0xFFFFU);
    /* Configure + run, with 64-byte packet TDs (we keep TDs at 16 B
     * which is the standard layout). */
    uhci_write16(base, UHCI_REG_CMD, UHCI_CMD_CF | UHCI_CMD_RS);
    return 0;
}

int uhci_port_reset(uhci_controller_t *ctrl, uint8_t port_index) {
    uint64_t base = ctrl->mmio_base;
    uint32_t port_off = UHCI_REG_PORTSC0 + (uint32_t)port_index * 2U;
    uint16_t v = uhci_reg16(base, port_off);
    if ((v & UHCI_PORT_CCS) == 0) {
        return 0;
    }
    v |= UHCI_PORT_RESET;
    uhci_write16(base, port_off, v);
    for (volatile int i = 0; i < 100000; i++) {
        /* spin until the controller clears the bit */
    }
    v &= ~UHCI_PORT_RESET;
    v &= (uint16_t)~UHCI_PORT_RWC;
    uhci_write16(base, port_off, v);
    for (volatile int i = 0; i < 10000; i++) {
        /* settle */
    }
    v = uhci_reg16(base, port_off);
    return (v & UHCI_PORT_PE) != 0 ? 1 : 0;
}

static int uhci_wait_td(volatile uhci_td_t *td, uint32_t spin_limit) {
    for (uint32_t i = 0; i < spin_limit; i++) {
        if ((td->status & UHCI_TD_ACTIVE_BIT) == 0) {
            return 0;
        }
    }
    return -1;
}

static void uhci_link_frame(uint32_t idx, uint32_t frame, uhci_td_t *td) {
    *(volatile uint32_t *)(g_priv[idx].frame_list_storage + frame * 4U) =
        td_phys(td);
}

static void uhci_unlink_frames(uint32_t idx, uint32_t start, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        *(volatile uint32_t *)(g_priv[idx].frame_list_storage +
                               (start + i) * 4U) = UHCI_PTR_TERM;
    }
}

int uhci_control_transfer(uhci_controller_t *ctrl, const void *setup,
                          uint8_t setup_size, void *data,
                          uint16_t data_size) {
    uint32_t idx = (uint32_t)(ctrl - g_controllers);
    if (idx >= 2U || setup == 0) {
        return -1;
    }
    if (g_priv[idx].in_xfer) {
        return -1;
    }
    g_priv[idx].in_xfer = 1;
    /* Clear the entire TD pool: the active bit is in dword 1 of the
     * TD which means we have to walk every entry. */
    td_reset_pool(idx);

    /* Copy the setup packet into our static buffer. UHCI requires
     * DMA-able memory and the caller's buffer may not be. */
    if (setup_size > 16U) {
        setup_size = 16U;
    }
    for (uint8_t i = 0; i < setup_size; i++) {
        g_priv[idx].setup_buffer[i] = ((const uint8_t *)setup)[i];
    }
    /* Copy data into our static buffer for OUT transfers. */
    if (data_size > 64U) {
        data_size = 64U;
    }
    if (data_size > 0 && data != 0) {
        for (uint16_t i = 0; i < data_size; i++) {
            g_priv[idx].xfer_buffer[i] = ((const uint8_t *)data)[i];
        }
    }

    uhci_td_t *setup_td = td_alloc(idx);
    uhci_td_t *data_td = (data_size > 0) ? td_alloc(idx) : 0;
    uhci_td_t *status_td = td_alloc(idx);
    if (setup_td == 0 || status_td == 0) {
        g_priv[idx].in_xfer = 0;
        return -1;
    }

    /* Determine direction from the setup bmRequestType. */
    const uint8_t *sb = (const uint8_t *)setup;
    uint8_t is_in = (uint8_t)((sb[0] & 0x80U) != 0U);

    /* SETUP TD. */
    setup_td->link = td_phys(status_td);
    setup_td->status = UHCI_TD_ACTIVE_BIT | UHCI_TD_ERR_COUNT_MASK | 3U;
    setup_td->token = td_make_token(UHCI_PID_SETUP, 0, 0, 0, 8);
    setup_td->buffer = (uint32_t)(uintptr_t)g_priv[idx].setup_buffer;

    if (data_td != 0) {
        data_td->link = td_phys(status_td);
        data_td->status = UHCI_TD_ACTIVE_BIT | UHCI_TD_ERR_COUNT_MASK | 3U;
        if (is_in) {
            data_td->token = td_make_token(UHCI_PID_IN, 0, 0, 1, 8);
        } else {
            data_td->token = td_make_token(UHCI_PID_OUT, 0, 0, 1, 8);
        }
        data_td->buffer = (uint32_t)(uintptr_t)g_priv[idx].xfer_buffer;
    } else {
        setup_td->link = td_phys(status_td);
    }

    /* STATUS TD (always opposite of the data stage direction). */
    status_td->link = UHCI_PTR_TERM;
    status_td->status = UHCI_TD_ACTIVE_BIT | UHCI_TD_ERR_COUNT_MASK | 3U |
                        UHCI_TD_IOC;
    if (is_in) {
        status_td->token = td_make_token(UHCI_PID_OUT, 0, 0, 1, 0);
    } else {
        status_td->token = td_make_token(UHCI_PID_IN, 0, 0, 1, 0);
    }
    status_td->buffer = 0;

    /* Insert the chain in frame list slots 0, 1, 2. The hardware
     * walks them in order once per frame, so the three TDs execute
     * back-to-back. */
    uhci_link_frame(idx, 0, setup_td);
    if (data_td != 0) {
        uhci_link_frame(idx, 1, data_td);
    } else {
        uhci_link_frame(idx, 1, status_td);
    }
    uhci_link_frame(idx, 2, status_td);

    /* Make sure the host controller is running. */
    uint16_t cmd = uhci_reg16(ctrl->mmio_base, UHCI_REG_CMD);
    if ((cmd & UHCI_CMD_RS) == 0) {
        uhci_write16(ctrl->mmio_base, UHCI_REG_CMD,
                     cmd | UHCI_CMD_RS);
    }

    int rc = uhci_wait_td(status_td, 1000000U);
    if (rc < 0) {
        uhci_unlink_frames(idx, 0, 3);
        td_reset_pool(idx);
        g_priv[idx].in_xfer = 0;
        return -1;
    }
    if ((status_td->status & (UHCI_TD_BITSTUFF | UHCI_TD_CRC | UHCI_TD_NAK |
                              UHCI_TD_BABBLE | UHCI_TD_DBUFFER |
                              UHCI_TD_STALLED)) != 0U) {
        uhci_unlink_frames(idx, 0, 3);
        td_reset_pool(idx);
        g_priv[idx].in_xfer = 0;
        return -1;
    }
    if (is_in && data_size > 0 && data != 0) {
        uint16_t actual = (uint16_t)(status_td->status & 0x7FFU);
        if (actual > data_size) {
            actual = data_size;
        }
        for (uint16_t i = 0; i < actual; i++) {
            ((uint8_t *)data)[i] = g_priv[idx].xfer_buffer[i];
        }
    }
    uhci_unlink_frames(idx, 0, 3);
    td_reset_pool(idx);
    g_priv[idx].in_xfer = 0;
    return 0;
}

int uhci_interrupt_in(uhci_controller_t *ctrl, uint8_t endpoint,
                      void *buf, uint16_t buf_size) {
    (void)ctrl;
    (void)endpoint;
    (void)buf;
    (void)buf_size;
    return -1;
}