#include "usb/uhci.h"

#include <stdint.h>

#include "uart/pl011.h"

/*
 * Minimal UHCI driver (poll mode, no interrupts).
 *
 * Each controller owns:
 *   - a 4 KB-aligned frame list (1024 dwords = 4096 bytes)
 *   - a pool of transfer descriptors
 *   - one queue head per active transfer
 *
 * Memory comes from a static pool because UHCI's frame list and TDs
 * must live in physical memory the controller can DMA into. We use a
 * 16 KB scratch buffer per controller and split it into the frame
 * list (4 KB) plus a 256-entry TD pool (16 bytes per TD).
 *
 * Polling is the only mode the driver exposes; the kernel calls
 * uhci_poll_inputs from the input pump after the controllers have
 * been initialized. The driver relies on the existing input_queue
 * (drivers/input/input.h) for delivery into EL0 apps.
 */

#define UHCI_FRAME_LIST_ENTRIES 1024U
#define UHCI_TD_POOL_SIZE        256U
#define UHCI_CTRL_POOL_SIZE       16384U

#define UHCI_PORTS_PER_CTRL 2U
#define UHCI_MAX_CONTROLLERS 2U

typedef struct {
    /* Frame list base + transfer descriptor pool (16 KB each). */
    uint8_t frame_list_storage[4096] __attribute__((aligned(4096)));
    uint8_t td_pool_storage[16384] __attribute__((aligned(16)));
    uint32_t td_next;     /* next free TD index */
} uhci_priv_t;

/* Transfer descriptor: 4 dwords (16 bytes) plus 4 dwords of link +
 * buffer pointers. UHCI element layout: */
typedef struct {
    uint32_t dword0;   /* status + actual length + error count */
    uint32_t dword1;   /* token (PID + device + endpoint + data toggle + max) */
    uint32_t dword2;   /* buffer pointer (low) */
    uint32_t dword3;   /* buffer pointer (high, unused) */
    uint32_t dword4;   /* reserved / link */
    uint32_t dword5;   /* reserved */
    uint32_t dword6;   /* reserved */
    uint32_t dword7;   /* reserved */
} uhci_td_t;

/* Element dword1 (token) layout. */
#define UHCI_TOKEN_PID_MASK     0x000000FFU
#define UHCI_TOKEN_DEV_SHIFT    8
#define UHCI_TOKEN_EP_SHIFT     18
#define UHCI_TOKEN_DT            (1U << 24)
#define UHCI_TOKEN_MAX_SHIFT     16
#define UHCI_TOKEN_MAX_MASK     0x000007FFU

#define UHCI_TD_STATUS_ACTIVE   (1U << 23)
#define UHCI_TD_STATUS_ERROR    (1U << 22)
#define UHCI_TD_STATUS_IOC      (1U << 24)
#define UHCI_TD_STATUS_ACTLEN_MASK 0x000007FFU

static uhci_priv_t g_uhci_priv[UHCI_MAX_CONTROLLERS];
static uhci_controller_t g_controllers[UHCI_MAX_CONTROLLERS];
static uint32_t g_controller_count;

static inline uint16_t uhci_reg16(uint64_t base, uint32_t off) {
    return *(volatile uint16_t *)(base + off);
}

static inline void uhci_write16(uint64_t base, uint32_t off, uint16_t v) {
    *(volatile uint16_t *)(base + off) = v;
}

static inline void uhci_write32(uint64_t base, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(base + off) = v;
}

static inline uint32_t uhci_td_pool_base(uint32_t idx) {
    return (uint32_t)(uintptr_t)g_uhci_priv[idx].td_pool_storage;
}

static inline uhci_td_t *td_alloc(uint32_t idx) {
    if (g_uhci_priv[idx].td_next >= UHCI_TD_POOL_SIZE) {
        return 0;
    }
    uhci_td_t *td = (uhci_td_t *)(g_uhci_priv[idx].td_pool_storage +
                                  g_uhci_priv[idx].td_next * 16U);
    g_uhci_priv[idx].td_next++;
    return td;
}

static inline uint32_t td_phys(uhci_td_t *td) {
    return (uint32_t)(uintptr_t)td;
}

uint32_t uhci_pci_probe(uhci_controller_t *out_controllers,
                        uint32_t max_count) {
    pci_device_t devices[PCI_MAX_BUSES * PCI_MAX_DEVICES * PCI_MAX_FUNCS];
    uint32_t n = pci_enumerate(devices,
                               PCI_MAX_BUSES * PCI_MAX_DEVICES *
                                   PCI_MAX_FUNCS);
    uint32_t found = 0;
    for (uint32_t i = 0; i < n && found < max_count; i++) {
        if (devices[i].class_code != PCI_CLASS_USB) {
            continue;
        }
        uint32_t bar0 = devices[i].bar[0];
        if ((bar0 & 0x01U) != 0U) {
            continue; /* I/O space BAR not supported. */
        }
        out_controllers[found].mmio_base = (uint64_t)(bar0 & 0xFFFFFFF0U);
        out_controllers[found].device_addr = 0;
        out_controllers[found].endpoint_in = 0;
        out_controllers[found].endpoint_out = 0;
        out_controllers[found].max_packet = 8;
        out_controllers[found].hid_report_size = 0;
        found++;
    }
    g_controller_count = found;
    return found;
}

int uhci_init(uhci_controller_t *ctrl) {
    uint32_t idx = (uint32_t)(ctrl - g_controllers);
    if (idx >= UHCI_MAX_CONTROLLERS) {
        return -1;
    }
    uint64_t base = ctrl->mmio_base;
    if (base == 0) {
        return -1;
    }

    g_uhci_priv[idx].td_next = 0;

    /* Global reset, then wait. */
    uhci_write16(base, UHCI_REG_CMD, UHCI_CMD_GRESET);
    for (volatile int i = 0; i < 100000; i++) {
        /* Spin until host clears GRESET. */
        if ((uhci_reg16(base, UHCI_REG_CMD) & UHCI_CMD_GRESET) == 0) {
            break;
        }
    }
    uhci_write16(base, UHCI_REG_CMD, 0);
    uhci_write16(base, UHCI_REG_STS, 0xFFFFU); /* Clear status. */
    uhci_write16(base, UHCI_REG_INTR, 0);      /* Disable all IRQs. */

    /* Frame list: every entry points to a single terminating QH/TD. */
    uint32_t fl_base = (uint32_t)(uintptr_t)g_uhci_priv[idx].frame_list_storage;
    for (uint32_t i = 0; i < UHCI_FRAME_LIST_ENTRIES; i++) {
        *(volatile uint32_t *)(g_uhci_priv[idx].frame_list_storage + i * 4U) =
            UHCI_PTR_TERM;
    }
    uhci_write32(base, UHCI_REG_FLBASE, fl_base);

    /* Configure and run. */
    uhci_write16(base, UHCI_REG_CMD,
                 UHCI_CMD_CF | UHCI_CMD_MAXP | UHCI_CMD_RS);
    return 0;
}

int uhci_port_reset(uhci_controller_t *ctrl, uint8_t port_index) {
    uint64_t base = ctrl->mmio_base;
    uint32_t port_off = UHCI_REG_PORTSC0 + (uint32_t)port_index * 2U;
    uint16_t v = uhci_reg16(base, port_off);
    if ((v & UHCI_PORT_CCS) == 0) {
        return 0; /* Nothing connected. */
    }
    /* Reset: drive bit 9 high for 50 ms (worst case). The QEMU virt
     * machine does not model debounce, so a few hundred iterations is
     * plenty. */
    v |= UHCI_PORT_RESET;
    uhci_write16(base, port_off, v);
    for (volatile int i = 0; i < 50000; i++) {
        /* Wait until the controller clears the reset bit. */
    }
    v &= ~UHCI_PORT_RESET;
    /* Acknowledge the connect-status-change bit. */
    v = (uint16_t)(v & ~UHCI_PORT_RWC);
    uhci_write16(base, port_off, v);
    /* Enable the port. */
    v = uhci_reg16(base, port_off);
    v |= UHCI_PORT_PE;
    uhci_write16(base, port_off, v);
    for (volatile int i = 0; i < 10000; i++) {
        /* Spin for the enable bit to settle. */
    }
    v = uhci_reg16(base, port_off);
    return (v & UHCI_PORT_PE) != 0 ? 1 : 0;
}

/*
 * Issue a single control transfer on endpoint 0. We set up a single
 * SETUP TD followed by an optional DATA TD followed by a STATUS TD.
 * All three are linked into one frame list entry. The function polls
 * the active bit and returns 0 on success, -1 on timeout.
 */
int uhci_control_transfer(uhci_controller_t *ctrl, const void *setup,
                         uint8_t setup_size, void *data, uint16_t data_size) {
    (void)ctrl;
    (void)setup;
    (void)setup_size;
    (void)data;
    (void)data_size;
    /*
     * Full control transfer implementation is the next step after
     * the foundational PCI scan + UHCI init land cleanly. The hooks
     * are in place: callers can build a setup packet, allocate TDs,
     * link them into the frame list, and poll for completion.
     *
     * For now, this stub returns -1 so callers can fall back to the
     * existing UART / virtio-input paths while the real implementation
     * is staged in.
     */
    return -1;
}

int uhci_interrupt_in(uhci_controller_t *ctrl, uint8_t endpoint,
                      void *buf, uint16_t buf_size) {
    (void)ctrl;
    (void)endpoint;
    (void)buf;
    (void)buf_size;
    /* See uhci_control_transfer: same staged status. */
    return -1;
}