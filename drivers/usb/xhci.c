#include "usb/xhci.h"

#include <stdint.h>

#include "kernel/kernel_compiler.h"
#include "kernel/kstring.h"
#include "kernel/mm/pmm.h"
#include "usb/usb.h"

#define XHCI_CAP_CAPLENGTH       0x00U
#define XHCI_CAP_HCSPARAMS1      0x04U
#define XHCI_CAP_HCSPARAMS2      0x08U
#define XHCI_CAP_HCCPARAMS1      0x10U
#define XHCI_CAP_DBOFF           0x14U
#define XHCI_CAP_RTSOFF          0x18U

#define XHCI_OP_USBCMD           0x00U
#define XHCI_OP_USBSTS           0x04U
#define XHCI_OP_PAGESIZE         0x08U
#define XHCI_OP_CRCR             0x18U
#define XHCI_OP_DCBAAP           0x30U
#define XHCI_OP_CONFIG           0x38U
#define XHCI_OP_PORTSC0          0x400U
#define XHCI_PORT_REG_STRIDE     0x10U

#define XHCI_RT_IMAN0            0x20U
#define XHCI_RT_ERSTSZ0          0x28U
#define XHCI_RT_ERSTBA0          0x30U
#define XHCI_RT_ERDP0            0x38U

#define XHCI_CMD_RS              (1U << 0)
#define XHCI_CMD_HCRST           (1U << 1)
#define XHCI_STS_HCH             (1U << 0)
#define XHCI_STS_CNR             (1U << 11)

#define XHCI_PORT_CCS            (1U << 0)
#define XHCI_PORT_PED            (1U << 1)
#define XHCI_PORT_PR             (1U << 4)
#define XHCI_PORT_PP             (1U << 9)
#define XHCI_PORT_SPEED_SHIFT    10U
#define XHCI_PORT_SPEED_MASK     (0xFU << XHCI_PORT_SPEED_SHIFT)
#define XHCI_PORT_CSC            (1U << 17)
#define XHCI_PORT_PEC            (1U << 18)
#define XHCI_PORT_WRC            (1U << 19)
#define XHCI_PORT_OCC            (1U << 20)
#define XHCI_PORT_PRC            (1U << 21)
#define XHCI_PORT_PLC            (1U << 22)
#define XHCI_PORT_CEC            (1U << 23)
#define XHCI_PORT_CHANGE_BITS    (XHCI_PORT_CSC | XHCI_PORT_PEC | \
                                  XHCI_PORT_WRC | XHCI_PORT_OCC | \
                                  XHCI_PORT_PRC | XHCI_PORT_PLC | \
                                  XHCI_PORT_CEC)

#define XHCI_SPEED_FULL          1U
#define XHCI_SPEED_LOW           2U
#define XHCI_SPEED_HIGH          3U
#define XHCI_SPEED_SUPER         4U

#define XHCI_TRB_TYPE_SHIFT      10U
#define XHCI_TRB_TYPE_MASK       (0x3FU << XHCI_TRB_TYPE_SHIFT)
#define XHCI_TRB_CYCLE           (1U << 0)
#define XHCI_TRB_TC              (1U << 1)
#define XHCI_TRB_ISP             (1U << 2)
#define XHCI_TRB_IOC             (1U << 5)
#define XHCI_TRB_IDT             (1U << 6)
#define XHCI_TRB_DIR             (1U << 16)

#define XHCI_TRB_NORMAL          1U
#define XHCI_TRB_SETUP_STAGE     2U
#define XHCI_TRB_DATA_STAGE      3U
#define XHCI_TRB_STATUS_STAGE    4U
#define XHCI_TRB_LINK            6U
#define XHCI_TRB_ENABLE_SLOT     9U
#define XHCI_TRB_ADDRESS_DEVICE  11U
#define XHCI_TRB_CONFIGURE_EP    12U
#define XHCI_TRB_TRANSFER_EVENT  32U
#define XHCI_TRB_COMMAND_EVENT   33U

#define XHCI_CC_SUCCESS          1U
#define XHCI_CC_SHORT_PACKET     13U

#define XHCI_EP_TYPE_CONTROL     4U
#define XHCI_EP_TYPE_INTR_IN     7U

#define XHCI_COMMAND_RING_SIZE   64U
#define XHCI_EVENT_RING_SIZE     64U
#define XHCI_TRANSFER_RING_SIZE  32U
#define XHCI_CONTEXT_BYTES_MAX   64U
#define XHCI_CONTEXT_COUNT       32U
#define XHCI_INPUT_CONTEXT_COUNT 33U
#define XHCI_CONTROL_BUF_SIZE    512U
#define XHCI_INTR_BUF_SIZE       64U
#define XHCI_MAX_SCRATCHPADS     32U
#define XHCI_COMPLETION_STASH    8U

typedef struct {
    volatile uint64_t parameter;
    volatile uint32_t status;
    volatile uint32_t control;
} KERNEL_ALIGNED(16) xhci_trb_t;

typedef struct {
    uint64_t ring_base;
    uint32_t ring_size;
    uint32_t reserved;
} KERNEL_PACKED_ALIGNED(16) xhci_erst_entry_t;

typedef struct {
    xhci_trb_t *trbs;
    uint32_t size;
    uint32_t enqueue;
    uint8_t cycle;
} xhci_ring_t;

typedef struct {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
    uint8_t used;
} xhci_stashed_event_t;

typedef struct {
    xhci_trb_t command_ring[XHCI_COMMAND_RING_SIZE] KERNEL_ALIGNED(64);
    xhci_trb_t event_ring[XHCI_EVENT_RING_SIZE] KERNEL_ALIGNED(64);
    xhci_trb_t transfer_ring[XHCI_MAX_DEVICES][XHCI_MAX_ENDPOINTS]
        [XHCI_TRANSFER_RING_SIZE] KERNEL_ALIGNED(64);
    xhci_erst_entry_t erst[1] KERNEL_ALIGNED(64);
    uint64_t dcbaa[XHCI_MAX_SLOTS + 1U] KERNEL_ALIGNED(64);
    uint64_t scratchpad_ptrs[XHCI_MAX_SCRATCHPADS] KERNEL_ALIGNED(64);
    uint8_t input_context[XHCI_CONTEXT_BYTES_MAX * XHCI_INPUT_CONTEXT_COUNT]
        KERNEL_ALIGNED(64);
    uint8_t device_context[XHCI_MAX_DEVICES][XHCI_CONTEXT_BYTES_MAX *
        XHCI_CONTEXT_COUNT] KERNEL_ALIGNED(64);
    uint8_t control_buffer[XHCI_CONTROL_BUF_SIZE] KERNEL_ALIGNED(64);
    uint8_t setup_buffer[16] KERNEL_ALIGNED(16);
    uint8_t interrupt_buffer[XHCI_MAX_DEVICES][XHCI_MAX_ENDPOINTS]
        [XHCI_INTR_BUF_SIZE] KERNEL_ALIGNED(64);
    xhci_ring_t command;
    xhci_ring_t endpoint[XHCI_MAX_DEVICES][XHCI_MAX_ENDPOINTS];
    xhci_trb_t *pending_intr_trb[XHCI_MAX_DEVICES][XHCI_MAX_ENDPOINTS];
    uint16_t pending_intr_len[XHCI_MAX_DEVICES][XHCI_MAX_ENDPOINTS];
    uint8_t endpoint_configured[XHCI_MAX_DEVICES][XHCI_MAX_ENDPOINTS];
    uint8_t device_used[XHCI_MAX_DEVICES];
    xhci_device_t default_device;
    uint8_t has_default_device;
    xhci_stashed_event_t stashed[XHCI_COMPLETION_STASH];
    uint8_t event_dequeue;
    uint8_t event_cycle;
    uint8_t scratchpad_count;
    uint8_t in_control_xfer;
} xhci_priv_t;

static xhci_priv_t g_priv[XHCI_MAX_CONTROLLERS];

static inline uint32_t xhci_read32(uint64_t base, uint32_t off) {
    return *(volatile uint32_t *)(base + off);
}

static inline void xhci_write32(uint64_t base, uint32_t off, uint32_t value) {
    *(volatile uint32_t *)(base + off) = value;
}

static void xhci_write64(uint64_t base, uint32_t off, uint64_t value) {
    xhci_write32(base, off, (uint32_t)value);
    xhci_write32(base, off + 4U, (uint32_t)(value >> 32));
}

static void xhci_sync(void) {
#ifndef ARMONIOS_TEST
    __asm__ volatile("dsb sy" ::: "memory");
#endif
}


static uint32_t trb_type(uint32_t type) {
    return type << XHCI_TRB_TYPE_SHIFT;
}

static uint32_t trb_get_type(uint32_t control) {
    return (control & XHCI_TRB_TYPE_MASK) >> XHCI_TRB_TYPE_SHIFT;
}

static uint32_t trb_completion_code(uint32_t status) {
    return status >> 24U;
}

static uint64_t dma_addr(const void *ptr) {
    return (uint64_t)(uintptr_t)ptr;
}

static void trb_set(xhci_trb_t *trb, uint64_t parameter,
                    uint32_t status, uint32_t control) {
    trb->parameter = parameter;
    trb->status = status;
    xhci_sync();
    trb->control = control;
    xhci_sync();
}

static void ring_init(xhci_ring_t *ring, xhci_trb_t *storage,
                      uint32_t size) {
    kmemzero(storage, size * (uint32_t)sizeof(xhci_trb_t));
    ring->trbs = storage;
    ring->size = size;
    ring->enqueue = 0;
    ring->cycle = 1;

    trb_set(&storage[size - 1U], dma_addr(storage), 0,
            trb_type(XHCI_TRB_LINK) | XHCI_TRB_TC | XHCI_TRB_CYCLE);
}

static xhci_trb_t *ring_push(xhci_ring_t *ring, uint64_t parameter,
                             uint32_t status, uint32_t control) {
    if (ring == 0 || ring->trbs == 0 || ring->enqueue >= ring->size - 1U) {
        return 0;
    }

    xhci_trb_t *trb = &ring->trbs[ring->enqueue];
    uint32_t cycle = ring->cycle ? XHCI_TRB_CYCLE : 0U;
    trb_set(trb, parameter, status, control | cycle);

    ring->enqueue++;
    if (ring->enqueue == ring->size - 1U) {
        uint32_t link_cycle = ring->cycle ? XHCI_TRB_CYCLE : 0U;
        trb_set(&ring->trbs[ring->size - 1U], dma_addr(ring->trbs), 0,
                trb_type(XHCI_TRB_LINK) | XHCI_TRB_TC | link_cycle);
        ring->enqueue = 0;
        ring->cycle ^= 1U;
    }

    return trb;
}

static uint32_t *input_control_context(xhci_controller_t *ctrl) {
    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    return (uint32_t *)priv->input_context;
}

static uint32_t *input_device_context(xhci_controller_t *ctrl,
                                      uint8_t context_index) {
    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    uint32_t off = ((uint32_t)context_index + 1U) *
                   (uint32_t)ctrl->context_size;
    return (uint32_t *)(priv->input_context + off);
}

static uint8_t endpoint_dci(uint8_t endpoint) {
    uint8_t ep_num = (uint8_t)(endpoint & 0x0FU);
    if (ep_num == 0U) {
        return 1U;
    }
    return (uint8_t)(ep_num * 2U + (((endpoint & 0x80U) != 0U) ? 1U : 0U));
}

static uint32_t portsc_offset(uint8_t port_index) {
    return XHCI_OP_PORTSC0 + (uint32_t)port_index * XHCI_PORT_REG_STRIDE;
}

static void port_clear_changes(xhci_controller_t *ctrl, uint32_t off) {
    uint32_t portsc = xhci_read32(ctrl->op_base, off);
    xhci_write32(ctrl->op_base, off,
                 (portsc & ~XHCI_PORT_CHANGE_BITS) |
                     XHCI_PORT_CHANGE_BITS);
}

static void doorbell(xhci_controller_t *ctrl, uint8_t slot_id,
                     uint8_t target) {
    xhci_write32(ctrl->db_base, (uint32_t)slot_id * 4U, target);
}

static void command_doorbell(xhci_controller_t *ctrl) {
    xhci_write32(ctrl->db_base, 0, 0);
}

static void event_ack(xhci_controller_t *ctrl) {
    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    uint64_t next = dma_addr(&priv->event_ring[priv->event_dequeue]);
    xhci_write64(ctrl->rt_base, XHCI_RT_ERDP0, next | (1ULL << 3));
}

static int event_next(xhci_controller_t *ctrl, xhci_trb_t *out) {
    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    xhci_trb_t *event = &priv->event_ring[priv->event_dequeue];
    uint32_t control = event->control;
    uint8_t cycle = (uint8_t)(control & XHCI_TRB_CYCLE);
    if (cycle != priv->event_cycle) {
        return 0;
    }

    out->parameter = event->parameter;
    out->status = event->status;
    out->control = control;

    priv->event_dequeue++;
    if (priv->event_dequeue >= XHCI_EVENT_RING_SIZE) {
        priv->event_dequeue = 0;
        priv->event_cycle ^= 1U;
    }
    event_ack(ctrl);
    return 1;
}

static void stash_transfer_event(xhci_controller_t *ctrl,
                                 const xhci_trb_t *event) {
    if (trb_get_type(event->control) != XHCI_TRB_TRANSFER_EVENT) {
        return;
    }
    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    for (uint8_t i = 0; i < XHCI_COMPLETION_STASH; i++) {
        if (!priv->stashed[i].used) {
            priv->stashed[i].parameter = event->parameter;
            priv->stashed[i].status = event->status;
            priv->stashed[i].control = event->control;
            priv->stashed[i].used = 1;
            return;
        }
    }
}

static int take_stashed_transfer(xhci_controller_t *ctrl, uint64_t trb_ptr,
                                 uint8_t slot_id, uint8_t dci,
                                 xhci_trb_t *out) {
    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    for (uint8_t i = 0; i < XHCI_COMPLETION_STASH; i++) {
        if (!priv->stashed[i].used) {
            continue;
        }
        if (priv->stashed[i].parameter != trb_ptr) {
            continue;
        }
        if (((priv->stashed[i].control >> 16U) & 0x1FU) != dci) {
            continue;
        }
        if (((priv->stashed[i].control >> 24U) & 0xFFU) != slot_id) {
            continue;
        }
        out->parameter = priv->stashed[i].parameter;
        out->status = priv->stashed[i].status;
        out->control = priv->stashed[i].control;
        priv->stashed[i].used = 0;
        return 1;
    }
    return 0;
}

static int wait_command(xhci_controller_t *ctrl, xhci_trb_t *command,
                        uint8_t *slot_id) {
    uint64_t command_ptr = dma_addr(command);
    xhci_trb_t event;

    for (uint32_t spin = 0; spin < 1000000U; spin++) {
        if (event_next(ctrl, &event) == 0) {
            continue;
        }

        uint32_t type = trb_get_type(event.control);
        if (type == XHCI_TRB_TRANSFER_EVENT) {
            stash_transfer_event(ctrl, &event);
            continue;
        }
        if (type != XHCI_TRB_COMMAND_EVENT) {
            continue;
        }
        if (event.parameter != command_ptr) {
            continue;
        }

        if (trb_completion_code(event.status) != XHCI_CC_SUCCESS) {
            return -1;
        }
        if (slot_id != 0) {
            *slot_id = (uint8_t)(event.control >> 24U);
        }
        return 0;
    }

    return -1;
}

static int completion_ok(uint32_t code) {
    return code == XHCI_CC_SUCCESS || code == XHCI_CC_SHORT_PACKET;
}

static int wait_transfer(xhci_controller_t *ctrl, xhci_trb_t *done_trb,
                         uint8_t slot_id, uint8_t dci, uint32_t spin_limit,
                         uint16_t requested, uint16_t *actual) {
    uint64_t done_ptr = dma_addr(done_trb);
    xhci_trb_t event;

    if (take_stashed_transfer(ctrl, done_ptr, slot_id, dci, &event)) {
        uint32_t code = trb_completion_code(event.status);
        if (!completion_ok(code)) {
            return -1;
        }
        uint32_t residue = event.status & 0x00FFFFFFU;
        if (residue > requested) {
            residue = requested;
        }
        if (actual != 0) {
            *actual = (uint16_t)(requested - residue);
        }
        return 0;
    }

    for (uint32_t spin = 0; spin < spin_limit; spin++) {
        if (event_next(ctrl, &event) == 0) {
            continue;
        }

        if (trb_get_type(event.control) != XHCI_TRB_TRANSFER_EVENT) {
            continue;
        }
        if (event.parameter != done_ptr) {
            stash_transfer_event(ctrl, &event);
            continue;
        }
        if (((event.control >> 16U) & 0x1FU) != dci) {
            stash_transfer_event(ctrl, &event);
            continue;
        }
        if (((event.control >> 24U) & 0xFFU) != slot_id) {
            stash_transfer_event(ctrl, &event);
            continue;
        }

        uint32_t code = trb_completion_code(event.status);
        if (!completion_ok(code)) {
            return -1;
        }

        uint32_t residue = event.status & 0x00FFFFFFU;
        if (residue > requested) {
            residue = requested;
        }
        if (actual != 0) {
            *actual = (uint16_t)(requested - residue);
        }
        return 0;
    }

    return 1;
}

static uint8_t max_slots_from_hcs1(uint32_t hcs1) {
    uint32_t slots = hcs1 & 0xFFU;
    if (slots == 0U || slots > XHCI_MAX_SLOTS) {
        slots = XHCI_MAX_SLOTS;
    }
    return (uint8_t)slots;
}

static uint8_t max_ports_from_hcs1(uint32_t hcs1) {
    uint32_t ports = (hcs1 >> 24U) & 0xFFU;
    if (ports > XHCI_MAX_PORTS) {
        ports = XHCI_MAX_PORTS;
    }
    return (uint8_t)ports;
}

static uint8_t scratchpads_from_hcs2(uint32_t hcs2) {
    uint32_t lo = (hcs2 >> 21U) & 0x1FU;
    uint32_t hi = (hcs2 >> 27U) & 0x1FU;
    uint32_t count = (hi << 5U) | lo;
    if (count > XHCI_MAX_SCRATCHPADS) {
        return 0xFFU;
    }
    return (uint8_t)count;
}

static uint16_t ep0_packet_for_speed(uint8_t speed) {
    if (speed == XHCI_SPEED_SUPER) {
        return 512U;
    }
    if (speed == XHCI_SPEED_HIGH) {
        return 64U;
    }
    return 8U;
}

static uint8_t interval_for_interrupt(uint8_t port_speed,
                                      uint8_t usb_interval) {
    if (usb_interval == 0U) {
        return 0U;
    }
    if (port_speed == XHCI_SPEED_HIGH ||
        port_speed == XHCI_SPEED_SUPER) {
        return (uint8_t)(usb_interval - 1U);
    }

    uint16_t microframes = (uint16_t)usb_interval * 8U;
    uint8_t interval = 0;
    while (microframes > 1U && interval < 15U) {
        microframes = (uint16_t)((microframes + 1U) >> 1U);
        interval++;
    }
    return interval;
}

uint32_t xhci_pci_probe(xhci_controller_t *out, uint32_t max_count) {
    if (out == 0 || max_count == 0) {
        return 0;
    }

    pci_device_t devices[PCI_MAX_BUSES * PCI_MAX_DEVICES * PCI_MAX_FUNCS];
    uint32_t n = pci_enumerate(devices,
                               PCI_MAX_BUSES * PCI_MAX_DEVICES *
                                   PCI_MAX_FUNCS);
    uint32_t found = 0;

    for (uint32_t i = 0; i < n && found < max_count; i++) {
        if (devices[i].class_code != PCI_CLASS_USB ||
            devices[i].prog_if != PCI_USB_PROGIF_XHCI) {
            continue;
        }

        for (uint32_t b = 0; b < 6; b++) {
            uint32_t bar = devices[i].bar[b];
            if ((bar & 0x01U) != 0U) {
                continue;
            }

            uint8_t is_64 = ((bar & 0x06U) == 0x04U) ? 1U : 0U;
            uint64_t next = (b + 1U < 6U) ? devices[i].bar[b + 1U] : 0;
            uint64_t base = pci_bar_address(bar, next, is_64);
            if (base == 0) {
                continue;
            }

            uint16_t cmd = pci_config_read16(devices[i].bus,
                                             devices[i].device,
                                             devices[i].function,
                                             PCI_CFG_COMMAND);
            cmd |= PCI_CMD_MEM_SPACE | PCI_CMD_BUS_MASTER;
            pci_config_write16(devices[i].bus, devices[i].device,
                               devices[i].function, PCI_CFG_COMMAND, cmd);

            out[found].mmio_base = base;
            out[found].op_base = 0;
            out[found].rt_base = 0;
            out[found].db_base = 0;
            out[found].bus = devices[i].bus;
            out[found].device = devices[i].device;
            out[found].function = devices[i].function;
            out[found].max_slots = 0;
            out[found].max_ports = 0;
            out[found].context_size = 32;
            out[found].priv_index = (uint8_t)found;
            out[found].slot_id = 0;
            out[found].port_id = 0;
            out[found].port_speed = 0;
            out[found].device_addr = 0;
            out[found].ep0_max_packet = 8;
            found++;
            break;
        }
    }

    return found;
}

static int setup_scratchpads(xhci_controller_t *ctrl, uint8_t count) {
    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    priv->scratchpad_count = count;
    if (count == 0U) {
        priv->dcbaa[0] = 0;
        return 0;
    }

    for (uint8_t i = 0; i < count; i++) {
        uint64_t page = pmm_alloc_page();
        if (page == 0) {
            return -1;
        }
        kmemzero((void *)(uintptr_t)page, (uint32_t)PAGE_SIZE);
        priv->scratchpad_ptrs[i] = page;
    }
    priv->dcbaa[0] = dma_addr(priv->scratchpad_ptrs);
    return 0;
}

int xhci_init(xhci_controller_t *ctrl) {
    if (ctrl == 0 || ctrl->priv_index >= XHCI_MAX_CONTROLLERS ||
        ctrl->mmio_base == 0) {
        return -1;
    }

    uint64_t cap = ctrl->mmio_base;
    uint8_t cap_len = (uint8_t)(xhci_read32(cap, XHCI_CAP_CAPLENGTH) & 0xFFU);
    uint32_t hcs1 = xhci_read32(cap, XHCI_CAP_HCSPARAMS1);
    uint32_t hcs2 = xhci_read32(cap, XHCI_CAP_HCSPARAMS2);
    uint32_t hcc1 = xhci_read32(cap, XHCI_CAP_HCCPARAMS1);
    uint32_t dboff = xhci_read32(cap, XHCI_CAP_DBOFF) & ~0x3U;
    uint32_t rtsoff = xhci_read32(cap, XHCI_CAP_RTSOFF) & ~0x1FU;
    uint8_t scratchpads = scratchpads_from_hcs2(hcs2);
    if (cap_len == 0 || scratchpads == 0xFFU) {
        return -1;
    }

    ctrl->op_base = cap + cap_len;
    ctrl->db_base = cap + dboff;
    ctrl->rt_base = cap + rtsoff;
    ctrl->max_slots = max_slots_from_hcs1(hcs1);
    ctrl->max_ports = max_ports_from_hcs1(hcs1);
    ctrl->context_size = (hcc1 & (1U << 2)) != 0U ? 64U : 32U;
    ctrl->slot_id = 0;
    ctrl->port_id = 0;
    ctrl->port_speed = 0;
    ctrl->device_addr = 0;
    ctrl->ep0_max_packet = 8;

    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    kmemzero(priv, sizeof(*priv));

    uint32_t cmd = xhci_read32(ctrl->op_base, XHCI_OP_USBCMD);
    if ((cmd & XHCI_CMD_RS) != 0U) {
        xhci_write32(ctrl->op_base, XHCI_OP_USBCMD, cmd & ~XHCI_CMD_RS);
        for (uint32_t i = 0; i < 1000000U; i++) {
            if ((xhci_read32(ctrl->op_base, XHCI_OP_USBSTS) &
                 XHCI_STS_HCH) != 0U) {
                break;
            }
        }
    }

    xhci_write32(ctrl->op_base, XHCI_OP_USBCMD, XHCI_CMD_HCRST);
    for (uint32_t i = 0; i < 1000000U; i++) {
        if ((xhci_read32(ctrl->op_base, XHCI_OP_USBCMD) &
             XHCI_CMD_HCRST) == 0U) {
            break;
        }
    }
    for (uint32_t i = 0; i < 1000000U; i++) {
        if ((xhci_read32(ctrl->op_base, XHCI_OP_USBSTS) &
             XHCI_STS_CNR) == 0U) {
            break;
        }
    }

    if ((xhci_read32(ctrl->op_base, XHCI_OP_PAGESIZE) & 0x01U) == 0U) {
        return -1;
    }

    ring_init(&priv->command, priv->command_ring, XHCI_COMMAND_RING_SIZE);
    priv->event_dequeue = 0;
    priv->event_cycle = 1;

    if (setup_scratchpads(ctrl, scratchpads) != 0) {
        return -1;
    }

    xhci_write64(ctrl->op_base, XHCI_OP_CRCR,
                 dma_addr(priv->command_ring) | XHCI_TRB_CYCLE);
    xhci_write64(ctrl->op_base, XHCI_OP_DCBAAP, dma_addr(priv->dcbaa));
    xhci_write32(ctrl->op_base, XHCI_OP_CONFIG, ctrl->max_slots);

    priv->erst[0].ring_base = dma_addr(priv->event_ring);
    priv->erst[0].ring_size = XHCI_EVENT_RING_SIZE;
    priv->erst[0].reserved = 0;

    xhci_write32(ctrl->rt_base, XHCI_RT_IMAN0, 0);
    xhci_write32(ctrl->rt_base, XHCI_RT_ERSTSZ0, 1);
    xhci_write64(ctrl->rt_base, XHCI_RT_ERSTBA0, dma_addr(priv->erst));
    xhci_write64(ctrl->rt_base, XHCI_RT_ERDP0, dma_addr(priv->event_ring));

    xhci_write32(ctrl->op_base, XHCI_OP_USBCMD, XHCI_CMD_RS);
    for (uint32_t i = 0; i < 1000000U; i++) {
        if ((xhci_read32(ctrl->op_base, XHCI_OP_USBSTS) &
             XHCI_STS_HCH) == 0U) {
            return 0;
        }
    }

    return -1;
}

int xhci_port_reset(xhci_controller_t *ctrl, uint8_t port_index) {
    if (ctrl == 0 || ctrl->op_base == 0 || port_index >= ctrl->max_ports) {
        return 0;
    }

    uint32_t off = portsc_offset(port_index);
    uint32_t portsc = xhci_read32(ctrl->op_base, off);
    if ((portsc & XHCI_PORT_CCS) == 0U) {
        return 0;
    }

    xhci_write32(ctrl->op_base, off,
                 (portsc & ~XHCI_PORT_CHANGE_BITS) | XHCI_PORT_PP);
    port_clear_changes(ctrl, off);

    portsc = xhci_read32(ctrl->op_base, off);
    xhci_write32(ctrl->op_base, off,
                 (portsc & ~XHCI_PORT_CHANGE_BITS) |
                     XHCI_PORT_PP | XHCI_PORT_PR);

    for (uint32_t i = 0; i < 1000000U; i++) {
        portsc = xhci_read32(ctrl->op_base, off);
        if ((portsc & XHCI_PORT_PR) == 0U) {
            break;
        }
    }

    port_clear_changes(ctrl, off);
    for (uint32_t i = 0; i < 100000U; i++) {
        portsc = xhci_read32(ctrl->op_base, off);
        if ((portsc & XHCI_PORT_PED) != 0U) {
            if (ctrl->port_id == 0U) {
                ctrl->port_id = (uint8_t)(port_index + 1U);
                ctrl->port_speed = (uint8_t)(
                    (portsc & XHCI_PORT_SPEED_MASK) >>
                    XHCI_PORT_SPEED_SHIFT);
                ctrl->ep0_max_packet =
                    ep0_packet_for_speed(ctrl->port_speed);
            }
            return 1;
        }
    }

    return 0;
}

static int command_enable_slot(xhci_controller_t *ctrl, uint8_t *slot_id) {
    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    xhci_trb_t *cmd = ring_push(&priv->command, 0, 0,
                                trb_type(XHCI_TRB_ENABLE_SLOT));
    if (cmd == 0) {
        return -1;
    }
    command_doorbell(ctrl);
    return wait_command(ctrl, cmd, slot_id);
}

static int command_address_device(xhci_controller_t *ctrl, uint8_t slot_id) {
    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    xhci_trb_t *cmd = ring_push(&priv->command, dma_addr(priv->input_context),
                                0,
                                trb_type(XHCI_TRB_ADDRESS_DEVICE) |
                                    ((uint32_t)slot_id << 24U));
    if (cmd == 0) {
        return -1;
    }
    command_doorbell(ctrl);
    return wait_command(ctrl, cmd, 0);
}

static int command_configure_endpoint(xhci_controller_t *ctrl,
                                      uint8_t slot_id) {
    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    xhci_trb_t *cmd = ring_push(&priv->command, dma_addr(priv->input_context),
                                0,
                                trb_type(XHCI_TRB_CONFIGURE_EP) |
                                    ((uint32_t)slot_id << 24U));
    if (cmd == 0) {
        return -1;
    }
    command_doorbell(ctrl);
    return wait_command(ctrl, cmd, 0);
}

static void fill_slot_context(xhci_controller_t *ctrl,
                              const xhci_device_t *dev,
                              uint8_t entries) {
    uint32_t *slot = input_device_context(ctrl, 0);
    slot[0] = ((uint32_t)dev->port_speed << 20U) |
              ((uint32_t)entries << 27U);
    slot[1] = (uint32_t)dev->port_id << 16U;
    slot[2] = 0;
    slot[3] = 0;
}

static void fill_endpoint_context(xhci_controller_t *ctrl,
                                  const xhci_device_t *dev, uint8_t dci,
                                  uint8_t ep_type, uint16_t max_packet,
                                  uint8_t interval) {
    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    uint32_t *ep = input_device_context(ctrl, dci);
    uint64_t dequeue =
        dma_addr(priv->endpoint[dev->index][dci].trbs) | XHCI_TRB_CYCLE;
    ep[0] = (uint32_t)interval << 16U;
    ep[1] = (3U << 1U) | ((uint32_t)ep_type << 3U) |
            ((uint32_t)max_packet << 16U);
    ep[2] = (uint32_t)dequeue;
    ep[3] = (uint32_t)(dequeue >> 32U);
    ep[4] = (uint32_t)max_packet | ((uint32_t)max_packet << 16U);
}

static int alloc_device_index(xhci_controller_t *ctrl) {
    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    for (uint8_t i = 0; i < XHCI_MAX_DEVICES; i++) {
        if (!priv->device_used[i]) {
            priv->device_used[i] = 1;
            return i;
        }
    }
    return -1;
}

int xhci_address_device(xhci_controller_t *ctrl, uint8_t port_index,
                        uint8_t address, xhci_device_t *out) {
    if (ctrl == 0 || out == 0 ||
        ctrl->priv_index >= XHCI_MAX_CONTROLLERS ||
        port_index >= ctrl->max_ports) {
        return -1;
    }

    uint32_t portsc = xhci_read32(ctrl->op_base, portsc_offset(port_index));
    if ((portsc & XHCI_PORT_PED) == 0U) {
        return -1;
    }

    int dev_index = alloc_device_index(ctrl);
    if (dev_index < 0) {
        return -1;
    }

    xhci_device_t dev;
    dev.ctrl = ctrl;
    dev.index = (uint8_t)dev_index;
    dev.slot_id = 0;
    dev.port_id = (uint8_t)(port_index + 1U);
    dev.port_speed = (uint8_t)((portsc & XHCI_PORT_SPEED_MASK) >>
                               XHCI_PORT_SPEED_SHIFT);
    dev.device_addr = address;
    dev.ep0_max_packet = ep0_packet_for_speed(dev.port_speed);

    uint8_t slot_id = 0;
    if (command_enable_slot(ctrl, &slot_id) != 0 ||
        slot_id == 0U || slot_id > ctrl->max_slots) {
        g_priv[ctrl->priv_index].device_used[dev.index] = 0;
        return -1;
    }
    dev.slot_id = slot_id;

    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    kmemzero(priv->input_context, sizeof(priv->input_context));
    kmemzero(priv->device_context[dev.index],
               sizeof(priv->device_context[dev.index]));
    kmemzero(priv->endpoint_configured[dev.index],
               sizeof(priv->endpoint_configured[dev.index]));
    kmemzero(priv->pending_intr_trb[dev.index],
               sizeof(priv->pending_intr_trb[dev.index]));
    kmemzero(priv->pending_intr_len[dev.index],
               sizeof(priv->pending_intr_len[dev.index]));
    ring_init(&priv->endpoint[dev.index][1],
              priv->transfer_ring[dev.index][1],
              XHCI_TRANSFER_RING_SIZE);

    uint32_t *ctrl_ctx = input_control_context(ctrl);
    ctrl_ctx[0] = 0;
    ctrl_ctx[1] = (1U << 0U) | (1U << 1U);

    fill_slot_context(ctrl, &dev, 1);
    fill_endpoint_context(ctrl, &dev, 1, XHCI_EP_TYPE_CONTROL,
                          dev.ep0_max_packet, 0);

    priv->dcbaa[slot_id] = dma_addr(priv->device_context[dev.index]);
    if (command_address_device(ctrl, slot_id) != 0) {
        priv->dcbaa[slot_id] = 0;
        priv->device_used[dev.index] = 0;
        return -1;
    }

    priv->endpoint_configured[dev.index][1] = 1;
    *out = dev;

    if (!priv->has_default_device) {
        priv->default_device = dev;
        priv->has_default_device = 1;
        ctrl->slot_id = dev.slot_id;
        ctrl->port_id = dev.port_id;
        ctrl->port_speed = dev.port_speed;
        ctrl->device_addr = dev.device_addr;
        ctrl->ep0_max_packet = dev.ep0_max_packet;
    }

    return 0;
}

static int setup_is_set_address(const usb_setup_t *setup) {
    return setup->bmRequestType == 0x00U &&
           setup->bRequest == USB_REQ_SET_ADDRESS &&
           setup->wLength == 0U;
}

int xhci_control_transfer_device(xhci_device_t *dev,
                                 const void *setup_buf, uint8_t setup_size,
                                 void *data, uint16_t data_size) {
    if (dev == 0 || dev->ctrl == 0 || setup_buf == 0 ||
        setup_size < sizeof(usb_setup_t) ||
        dev->ctrl->priv_index >= XHCI_MAX_CONTROLLERS ||
        dev->slot_id == 0U || dev->index >= XHCI_MAX_DEVICES) {
        return -1;
    }

    xhci_controller_t *ctrl = dev->ctrl;
    const usb_setup_t *setup = (const usb_setup_t *)setup_buf;
    if (setup_is_set_address(setup)) {
        dev->device_addr = (uint8_t)setup->wValue;
        return 0;
    }

    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    if (priv->in_control_xfer) {
        return -1;
    }
    priv->in_control_xfer = 1;

    if (data_size > XHCI_CONTROL_BUF_SIZE) {
        data_size = XHCI_CONTROL_BUF_SIZE;
    }

    uint8_t is_in = (setup->bmRequestType & 0x80U) != 0U ? 1U : 0U;
    if (!is_in && data != 0 && data_size > 0U) {
        kmemcpy(priv->control_buffer, data, data_size);
    }

    kmemzero(priv->setup_buffer, sizeof(priv->setup_buffer));
    kmemcpy(priv->setup_buffer, setup_buf, sizeof(usb_setup_t));

    uint64_t setup_param = 0;
    for (uint8_t i = 0; i < sizeof(usb_setup_t); i++) {
        setup_param |= ((uint64_t)priv->setup_buffer[i]) << (8U * i);
    }

    uint32_t setup_trt = 0;
    if (data_size > 0U) {
        setup_trt = is_in ? 3U : 2U;
    }

    xhci_ring_t *ring = &priv->endpoint[dev->index][1];
    xhci_trb_t *setup_trb = ring_push(ring, setup_param, 8,
                                      trb_type(XHCI_TRB_SETUP_STAGE) |
                                          XHCI_TRB_IDT |
                                          (setup_trt << 16U));
    xhci_trb_t *data_trb = 0;
    if (data_size > 0U) {
        data_trb = ring_push(ring, dma_addr(priv->control_buffer),
                             data_size,
                             trb_type(XHCI_TRB_DATA_STAGE) |
                                 XHCI_TRB_ISP |
                                 (is_in ? XHCI_TRB_DIR : 0U));
    }
    uint32_t status_dir = is_in ? 0U : XHCI_TRB_DIR;
    xhci_trb_t *status_trb = ring_push(ring, 0, 0,
                                       trb_type(XHCI_TRB_STATUS_STAGE) |
                                           status_dir | XHCI_TRB_IOC);
    if (setup_trb == 0 || status_trb == 0 ||
        (data_size > 0U && data_trb == 0)) {
        priv->in_control_xfer = 0;
        return -1;
    }

    doorbell(ctrl, dev->slot_id, 1);

    uint16_t actual = 0;
    int rc = wait_transfer(ctrl, status_trb, dev->slot_id, 1, 1000000U,
                           data_size, &actual);
    if (rc != 0) {
        priv->in_control_xfer = 0;
        return -1;
    }

    if (is_in && data != 0 && data_size > 0U) {
        if (actual > data_size) {
            actual = data_size;
        }
        kmemcpy(data, priv->control_buffer, actual);
    }

    priv->in_control_xfer = 0;
    return (int)actual;
}

int xhci_control_transfer(xhci_controller_t *ctrl, const void *setup_buf,
                          uint8_t setup_size, void *data,
                          uint16_t data_size) {
    if (ctrl == 0 || setup_buf == 0 || setup_size < sizeof(usb_setup_t) ||
        ctrl->priv_index >= XHCI_MAX_CONTROLLERS) {
        return -1;
    }

    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    const usb_setup_t *setup = (const usb_setup_t *)setup_buf;
    if (setup_is_set_address(setup) && !priv->has_default_device) {
        uint8_t port_index = 0xFFU;
        if (ctrl->port_id != 0U) {
            port_index = (uint8_t)(ctrl->port_id - 1U);
        } else {
            for (uint8_t i = 0; i < ctrl->max_ports; i++) {
                uint32_t portsc =
                    xhci_read32(ctrl->op_base, portsc_offset(i));
                if ((portsc & XHCI_PORT_PED) != 0U) {
                    port_index = i;
                    break;
                }
            }
        }
        if (port_index == 0xFFU) {
            return -1;
        }
        return xhci_address_device(ctrl, port_index,
                                   (uint8_t)setup->wValue,
                                   &priv->default_device);
    }
    if (!priv->has_default_device) {
        return -1;
    }
    return xhci_control_transfer_device(&priv->default_device, setup_buf,
                                        setup_size, data, data_size);
}

static int configure_interrupt_endpoint(xhci_device_t *dev,
                                        uint8_t endpoint,
                                        uint16_t max_packet,
                                        uint8_t usb_interval) {
    uint8_t dci = endpoint_dci((uint8_t)(endpoint | 0x80U));
    xhci_controller_t *ctrl = dev->ctrl;
    if (dci >= XHCI_MAX_ENDPOINTS || dci == 1U ||
        dev->slot_id == 0U || dev->index >= XHCI_MAX_DEVICES) {
        return -1;
    }

    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    if (priv->endpoint_configured[dev->index][dci]) {
        return 0;
    }
    if (max_packet == 0U || max_packet > XHCI_INTR_BUF_SIZE) {
        max_packet = XHCI_INTR_BUF_SIZE;
    }

    kmemzero(priv->input_context, sizeof(priv->input_context));
    ring_init(&priv->endpoint[dev->index][dci],
              priv->transfer_ring[dev->index][dci],
              XHCI_TRANSFER_RING_SIZE);

    uint32_t *ctrl_ctx = input_control_context(ctrl);
    ctrl_ctx[0] = 0;
    ctrl_ctx[1] = (1U << 0U) | (1U << dci);

    fill_slot_context(ctrl, dev, dci);
    fill_endpoint_context(ctrl, dev, dci, XHCI_EP_TYPE_INTR_IN, max_packet,
                          interval_for_interrupt(dev->port_speed,
                                                 usb_interval));

    if (command_configure_endpoint(ctrl, dev->slot_id) != 0) {
        return -1;
    }

    priv->endpoint_configured[dev->index][dci] = 1;
    return 0;
}

int xhci_interrupt_in_device(xhci_device_t *dev, uint8_t endpoint,
                             uint16_t max_packet, uint8_t interval,
                             void *buf, uint16_t buf_size) {
    if (dev == 0 || dev->ctrl == 0 || buf == 0 || buf_size == 0U ||
        dev->ctrl->priv_index >= XHCI_MAX_CONTROLLERS ||
        dev->slot_id == 0U || endpoint == 0U ||
        dev->index >= XHCI_MAX_DEVICES) {
        return -1;
    }

    xhci_controller_t *ctrl = dev->ctrl;
    uint8_t dci = endpoint_dci((uint8_t)(endpoint | 0x80U));
    if (dci >= XHCI_MAX_ENDPOINTS) {
        return -1;
    }
    if (configure_interrupt_endpoint(dev, endpoint, max_packet,
                                     interval) != 0) {
        return -1;
    }

    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    uint16_t xfer_len = buf_size;
    if (xfer_len > max_packet) {
        xfer_len = max_packet;
    }
    if (xfer_len > XHCI_INTR_BUF_SIZE) {
        xfer_len = XHCI_INTR_BUF_SIZE;
    }

    if (priv->pending_intr_trb[dev->index][dci] == 0) {
        kmemzero(priv->interrupt_buffer[dev->index][dci],
                   XHCI_INTR_BUF_SIZE);
        xhci_trb_t *trb = ring_push(&priv->endpoint[dev->index][dci],
                                    dma_addr(priv->interrupt_buffer
                                                 [dev->index][dci]),
                                    xfer_len,
                                    trb_type(XHCI_TRB_NORMAL) |
                                        XHCI_TRB_ISP | XHCI_TRB_IOC);
        if (trb == 0) {
            return -1;
        }
        priv->pending_intr_trb[dev->index][dci] = trb;
        priv->pending_intr_len[dev->index][dci] = xfer_len;
        doorbell(ctrl, dev->slot_id, dci);
    }

    uint16_t actual = 0;
    int rc = wait_transfer(ctrl, priv->pending_intr_trb[dev->index][dci],
                           dev->slot_id, dci, 5000U,
                           priv->pending_intr_len[dev->index][dci],
                           &actual);
    if (rc > 0) {
        return 0;
    }
    if (rc < 0) {
        priv->pending_intr_trb[dev->index][dci] = 0;
        priv->pending_intr_len[dev->index][dci] = 0;
        return -1;
    }

    priv->pending_intr_trb[dev->index][dci] = 0;
    priv->pending_intr_len[dev->index][dci] = 0;
    if (actual > buf_size) {
        actual = buf_size;
    }
    kmemcpy(buf, priv->interrupt_buffer[dev->index][dci], actual);
    return (int)actual;
}

int xhci_interrupt_in(xhci_controller_t *ctrl, uint8_t endpoint,
                      uint16_t max_packet, uint8_t interval,
                      void *buf, uint16_t buf_size) {
    if (ctrl == 0 || ctrl->priv_index >= XHCI_MAX_CONTROLLERS) {
        return -1;
    }
    xhci_priv_t *priv = &g_priv[ctrl->priv_index];
    if (!priv->has_default_device) {
        return -1;
    }
    return xhci_interrupt_in_device(&priv->default_device, endpoint,
                                    max_packet, interval, buf, buf_size);
}
