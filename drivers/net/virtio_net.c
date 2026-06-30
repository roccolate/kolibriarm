#include "net/virtio_net.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/kernel_compiler.h"
#include "kernel/kstring.h"

#define VIRTIO_MMIO_MAGIC_VALUE 0x000
#define VIRTIO_MMIO_VERSION     0x004
#define VIRTIO_MMIO_DEVICE_ID   0x008
#define VIRTIO_MMIO_VENDOR_ID   0x00c
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL   0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM   0x038
#define VIRTIO_MMIO_QUEUE_READY 0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050
#define VIRTIO_MMIO_STATUS      0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW 0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_QUEUE_DRIVER_LOW 0x090
#define VIRTIO_MMIO_QUEUE_DRIVER_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_DEVICE_LOW 0x0a0
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH 0x0a4
#define VIRTIO_MMIO_CONFIG      0x100

#define VIRTIO_MMIO_MAGIC       0x74726976U
#define VIRTIO_DEVICE_ID_NET    1U
#define VIRTIO_STATUS_ACKNOWLEDGE 1U
#define VIRTIO_STATUS_DRIVER    2U
#define VIRTIO_STATUS_DRIVER_OK 4U
#define VIRTIO_STATUS_FEATURES_OK 8U
#define VIRTIO_STATUS_FAILED    128U
#define VIRTQ_DESC_F_NEXT       1U
#define VIRTQ_DESC_F_WRITE      2U
/*
 * The current network stack consumes frames synchronously from a polling path.
 * Sixteen RX descriptors absorb small QEMU bursts without reserving the old
 * 48 KiB receive ring; TX uses one shared frame buffer because send waits for
 * completion before allowing reuse. Each buffer includes the required
 * virtio-net header before the Ethernet frame.
 */
#define VIRTIO_NET_QUEUE_SIZE   16U
#define VIRTIO_NET_POLL_LIMIT   100000U
#define VIRTIO_F_VERSION_1_HIGH 1U
#define VIRTIO_NET_F_MAC        (1U << 5)
#define VIRTIO_NET_F_CSUM       (1U << 9)
#define VIRTIO_NET_F_GUEST_CSUM (1U << 8)

#define VIRTIO_NET_FRAME_SIZE 1536U
/* QEMU virtio-net-device consumes 12 bytes before the Ethernet frame. */
#define VIRTIO_NET_HDR_SIZE   12U
#define RX_BUF_SIZE (VIRTIO_NET_HDR_SIZE + VIRTIO_NET_FRAME_SIZE)
#define TX_BUF_SIZE (VIRTIO_NET_HDR_SIZE + VIRTIO_NET_FRAME_SIZE)

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_NET_QUEUE_SIZE];
} virtq_avail_t;

typedef struct {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[VIRTIO_NET_QUEUE_SIZE];
} virtq_used_t;

static virtq_desc_t g_rx_desc[VIRTIO_NET_QUEUE_SIZE] KERNEL_ALIGNED(16);
static virtq_avail_t g_rx_avail KERNEL_ALIGNED(2);
static volatile virtq_used_t g_rx_used KERNEL_ALIGNED(4);
static uint8_t g_rx_buf[VIRTIO_NET_QUEUE_SIZE][RX_BUF_SIZE] KERNEL_ALIGNED(8);

static virtq_desc_t g_tx_desc[VIRTIO_NET_QUEUE_SIZE] KERNEL_ALIGNED(16);
static virtq_avail_t g_tx_avail KERNEL_ALIGNED(2);
static volatile virtq_used_t g_tx_used KERNEL_ALIGNED(4);
static uint8_t g_tx_buf[TX_BUF_SIZE] KERNEL_ALIGNED(8);

static uint16_t g_tx_last_used = 0;
static uint8_t g_tx_pending = 0;

static volatile uint32_t *virtio_reg(uint64_t base, uint32_t offset) {
    return (volatile uint32_t *)(uintptr_t)(base + offset);
}

static void virtio_write64(uint64_t base, uint32_t low_offset, uint64_t value) {
    *virtio_reg(base, low_offset) = (uint32_t)value;
    *virtio_reg(base, low_offset + 4U) = (uint32_t)(value >> 32);
}

static void virtio_barrier(void) {
#if defined(__aarch64__)
    __asm__ volatile("dmb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}


static int virtio_net_device_ready(const virtio_net_device_t *device) {
    return device != NULL && device->ready != 0 && device->base != 0 &&
           device->queue_size > 0 &&
           device->queue_size <= VIRTIO_NET_QUEUE_SIZE;
}

static int virtio_net_tx_reclaim_completed(void) {
    virtio_barrier();
    if (g_tx_used.idx != g_tx_last_used) {
        g_tx_last_used = g_tx_used.idx;
        g_tx_pending = 0;
        return 1;
    }
    return 0;
}

int virtio_net_probe(uint64_t base, virtio_net_info_t *info) {
    uint32_t magic;
    uint32_t version;
    uint32_t device_id;

    if (base == 0 || info == NULL) {
        return -1;
    }

    magic = *virtio_reg(base, VIRTIO_MMIO_MAGIC_VALUE);
    version = *virtio_reg(base, VIRTIO_MMIO_VERSION);
    device_id = *virtio_reg(base, VIRTIO_MMIO_DEVICE_ID);

    if (magic != VIRTIO_MMIO_MAGIC || device_id != VIRTIO_DEVICE_ID_NET) {
        return -1;
    }

    if (version != 1U && version != 2U) {
        return -1;
    }

    for (int i = 0; i < 6; i++) {
        info->mac[i] = *((volatile uint8_t *)(uintptr_t)(base + VIRTIO_MMIO_CONFIG + i));
    }

    return 0;
}

int virtio_net_probe_range(uint64_t base, uint64_t size, uint64_t stride,
                           uint64_t *found_base, virtio_net_info_t *info) {
    if (base == 0 || size == 0 || stride == 0 || found_base == NULL ||
        info == NULL) {
        return -1;
    }

    for (uint64_t offset = 0; offset < size; offset += stride) {
        uint64_t candidate = base + offset;

        if (virtio_net_probe(candidate, info) == 0) {
            *found_base = candidate;
            return 0;
        }
    }

    return -1;
}

int virtio_net_init(virtio_net_device_t *device, uint64_t base) {
    virtio_net_info_t info;
    uint32_t queue_max;
    uint32_t status;

    if (device == NULL || virtio_net_probe(base, &info) != 0) {
        return -1;
    }

    *virtio_reg(base, VIRTIO_MMIO_STATUS) = 0;
    virtio_barrier();
    *virtio_reg(base, VIRTIO_MMIO_STATUS) = VIRTIO_STATUS_ACKNOWLEDGE;
    *virtio_reg(base, VIRTIO_MMIO_STATUS) =
        VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;

    *virtio_reg(base, VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 0;
    *virtio_reg(base, VIRTIO_MMIO_DRIVER_FEATURES) = VIRTIO_NET_F_MAC;
    *virtio_reg(base, VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 1;
    *virtio_reg(base, VIRTIO_MMIO_DRIVER_FEATURES) = VIRTIO_F_VERSION_1_HIGH;
    status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
             VIRTIO_STATUS_FEATURES_OK;
    *virtio_reg(base, VIRTIO_MMIO_STATUS) = status;
    virtio_barrier();

    if ((*virtio_reg(base, VIRTIO_MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0) {
        *virtio_reg(base, VIRTIO_MMIO_STATUS) = VIRTIO_STATUS_FAILED;
        return -1;
    }

    *virtio_reg(base, VIRTIO_MMIO_QUEUE_SEL) = 0;
    queue_max = *virtio_reg(base, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (queue_max < VIRTIO_NET_QUEUE_SIZE) {
        *virtio_reg(base, VIRTIO_MMIO_STATUS) = VIRTIO_STATUS_FAILED;
        return -1;
    }

    for (uint32_t i = 0; i < VIRTIO_NET_QUEUE_SIZE; i++) {
        g_rx_desc[i].addr = (uint64_t)(uintptr_t)g_rx_buf[i];
        g_rx_desc[i].len = RX_BUF_SIZE;
        g_rx_desc[i].flags = VIRTQ_DESC_F_WRITE;
        g_rx_desc[i].next = (i + 1) % VIRTIO_NET_QUEUE_SIZE;

        g_tx_desc[i].addr = 0;
        g_tx_desc[i].len = 0;
        g_tx_desc[i].flags = 0;
        g_tx_desc[i].next = 0;

        g_rx_avail.ring[i] = i;
        g_rx_used.ring[i].id = 0;
        g_rx_used.ring[i].len = 0;
        g_tx_avail.ring[i] = 0;
        g_tx_used.ring[i].id = 0;
        g_tx_used.ring[i].len = 0;
    }

    g_rx_avail.flags = 0;
    g_rx_avail.idx = VIRTIO_NET_QUEUE_SIZE;
    g_rx_used.flags = 0;
    g_rx_used.idx = 0;
    g_tx_avail.flags = 0;
    g_tx_avail.idx = 0;
    g_tx_used.flags = 0;
    g_tx_used.idx = 0;

    *virtio_reg(base, VIRTIO_MMIO_QUEUE_NUM) = VIRTIO_NET_QUEUE_SIZE;
    virtio_write64(base, VIRTIO_MMIO_QUEUE_DESC_LOW, (uint64_t)(uintptr_t)g_rx_desc);
    virtio_write64(base, VIRTIO_MMIO_QUEUE_DRIVER_LOW, (uint64_t)(uintptr_t)&g_rx_avail);
    virtio_write64(base, VIRTIO_MMIO_QUEUE_DEVICE_LOW, (uint64_t)(uintptr_t)&g_rx_used);
    *virtio_reg(base, VIRTIO_MMIO_QUEUE_READY) = 1;

    *virtio_reg(base, VIRTIO_MMIO_QUEUE_SEL) = 1;
    *virtio_reg(base, VIRTIO_MMIO_QUEUE_NUM) = VIRTIO_NET_QUEUE_SIZE;
    virtio_write64(base, VIRTIO_MMIO_QUEUE_DESC_LOW, (uint64_t)(uintptr_t)g_tx_desc);
    virtio_write64(base, VIRTIO_MMIO_QUEUE_DRIVER_LOW, (uint64_t)(uintptr_t)&g_tx_avail);
    virtio_write64(base, VIRTIO_MMIO_QUEUE_DEVICE_LOW, (uint64_t)(uintptr_t)&g_tx_used);
    *virtio_reg(base, VIRTIO_MMIO_QUEUE_READY) = 1;

    virtio_barrier();

    status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
             VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK;
    *virtio_reg(base, VIRTIO_MMIO_STATUS) = status;
    virtio_barrier();
    *virtio_reg(base, VIRTIO_MMIO_QUEUE_NOTIFY) = 0;
    virtio_barrier();

    device->base = base;
    device->queue_size = VIRTIO_NET_QUEUE_SIZE;
    device->last_used_idx = 0;
    device->ready = 1;
    for (int i = 0; i < 6; i++) {
        device->mac[i] = info.mac[i];
    }

    g_tx_last_used = 0;
    g_tx_pending = 0;

    return 0;
}

int virtio_net_send(virtio_net_device_t *device, const void *data, uint32_t len) {
    uint16_t avail_idx;
    uint16_t desc_idx;

    if (!virtio_net_device_ready(device) || data == NULL || len == 0) {
        return -1;
    }

    if (len > VIRTIO_NET_FRAME_SIZE) {
        len = VIRTIO_NET_FRAME_SIZE;
    }

    (void)virtio_net_tx_reclaim_completed();
    if (g_tx_pending != 0) {
        return -2;
    }

    avail_idx = g_tx_avail.idx;
    desc_idx = 0;

    for (uint32_t i = 0; i < VIRTIO_NET_HDR_SIZE; i++) {
        g_tx_buf[i] = 0;
    }
    kmemcpy(g_tx_buf + VIRTIO_NET_HDR_SIZE, data, len);

    g_tx_desc[desc_idx].addr = (uint64_t)(uintptr_t)g_tx_buf;
    g_tx_desc[desc_idx].len = VIRTIO_NET_HDR_SIZE + len;
    g_tx_desc[desc_idx].flags = 0;
    g_tx_desc[desc_idx].next = 0;

    g_tx_avail.ring[avail_idx % device->queue_size] = desc_idx;
    virtio_barrier();
    g_tx_avail.idx = avail_idx + 1U;
    virtio_barrier();

    g_tx_pending = 1;
    *virtio_reg(device->base, VIRTIO_MMIO_QUEUE_NOTIFY) = 1;

    for (uint32_t spins = 0; spins < VIRTIO_NET_POLL_LIMIT; spins++) {
        if (virtio_net_tx_reclaim_completed()) {
            return 0;
        }
    }

    return -2;
}

int virtio_net_recv(virtio_net_device_t *device, void *data, uint32_t max_len) {
    uint16_t used_idx;
    uint16_t desc_idx;
    uint32_t copy_len;

    if (!virtio_net_device_ready(device) || data == NULL) {
        return -1;
    }

    if (g_rx_used.idx == device->last_used_idx) {
        return 0;
    }

    used_idx = device->last_used_idx % device->queue_size;
    desc_idx = g_rx_used.ring[used_idx].id;

    if (desc_idx >= device->queue_size) {
        device->last_used_idx++;
        return -1;
    }

    copy_len = g_rx_used.ring[used_idx].len;
    if (copy_len <= VIRTIO_NET_HDR_SIZE) {
        copy_len = 0;
    } else {
        copy_len -= VIRTIO_NET_HDR_SIZE;
    }
    if (copy_len > max_len) {
        copy_len = max_len;
    }
    if (copy_len > VIRTIO_NET_FRAME_SIZE) {
        copy_len = VIRTIO_NET_FRAME_SIZE;
    }

    if (copy_len > 0) {
        kmemcpy(data, g_rx_buf[desc_idx] + VIRTIO_NET_HDR_SIZE, copy_len);
    }

    g_rx_desc[desc_idx].addr = (uint64_t)(uintptr_t)g_rx_buf[desc_idx];
    g_rx_desc[desc_idx].len = RX_BUF_SIZE;
    g_rx_desc[desc_idx].flags = VIRTQ_DESC_F_WRITE;
    g_rx_desc[desc_idx].next = (desc_idx + 1) % device->queue_size;

    g_rx_avail.ring[g_rx_avail.idx % device->queue_size] = desc_idx;
    virtio_barrier();
    g_rx_avail.idx++;
    virtio_barrier();

    *virtio_reg(device->base, VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

    device->last_used_idx++;

    return (int)copy_len;
}

int virtio_net_poll(virtio_net_device_t *device) {
    if (!virtio_net_device_ready(device)) {
        return -1;
    }

    virtio_barrier();
    return (g_rx_used.idx != device->last_used_idx) ? 1 : 0;
}

#ifdef ARMONIOS_TEST
uint32_t virtio_net_test_rx_buffer_bytes(void) {
    return (uint32_t)sizeof(g_rx_buf);
}

uint32_t virtio_net_test_tx_buffer_bytes(void) {
    return (uint32_t)sizeof(g_tx_buf);
}

uint16_t virtio_net_test_rx_available_idx(void) {
    return g_rx_avail.idx;
}
#endif
