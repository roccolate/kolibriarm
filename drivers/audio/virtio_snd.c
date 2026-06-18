#include "audio/virtio_snd.h"

#include <stddef.h>
#include <stdint.h>

#include "uart/pl011.h"

#define VIRTIO_MMIO_MAGIC_VALUE         0x000
#define VIRTIO_MMIO_VERSION             0x004
#define VIRTIO_MMIO_DEVICE_ID           0x008
#define VIRTIO_MMIO_VENDOR_ID           0x00c
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL           0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034
#define VIRTIO_MMIO_QUEUE_NUM           0x038
#define VIRTIO_MMIO_QUEUE_READY         0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050
#define VIRTIO_MMIO_STATUS              0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW      0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     0x084
#define VIRTIO_MMIO_QUEUE_DRIVER_LOW    0x090
#define VIRTIO_MMIO_QUEUE_DRIVER_HIGH   0x094
#define VIRTIO_MMIO_QUEUE_DEVICE_LOW    0x0a0
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH    0x0a4
#define VIRTIO_MMIO_CONFIG              0x100
#define VIRTIO_MMIO_INT_STATUS           0x60
#define VIRTIO_MMIO_INT_ENABLE           0x064

#define VIRTIO_MMIO_MAGIC               0x74726976U
#define VIRTIO_DEVICE_ID_SOUND          9U
#define VIRTIO_STATUS_ACKNOWLEDGE       1U
#define VIRTIO_STATUS_DRIVER            2U
#define VIRTIO_STATUS_DRIVER_OK         4U
#define VIRTIO_STATUS_FAILED            128U

#define VIRTIO_SND_CONTROL_QUEUE        0
#define VIRTIO_SND_PLAYBACK_QUEUE      1
#define VIRTIO_SND_CAPTURE_QUEUE       2
#define VIRTIO_SND_QUEUE_SIZE          16U

#define VIRTIO_SND_CHMAP_TYPE_NONE     0
#define VIRTIO_SND_CHMAP_TYPE_U8       1
#define VIRTIO_SND_CHMAP_TYPE_S16      2

#define VIRTIO_SND_RATES_5512          (1U << 0)
#define VIRTIO_SND_RATES_8000          (1U << 1)
#define VIRTIO_SND_RATES_11025         (1U << 2)
#define VIRTIO_SND_RATES_16000         (1U << 3)
#define VIRTIO_SND_RATES_22050         (1U << 4)
#define VIRTIO_SND_RATES_32000         (1U << 5)
#define VIRTIO_SND_RATES_44100         (1U << 6)
#define VIRTIO_SND_RATES_48000         (1U << 7)
#define VIRTIO_SND_RATES_64000          (1U << 8)
#define VIRTIO_SND_RATES_88200         (1U << 9)
#define VIRTIO_SND_RATES_96000          (1U << 10)
#define VIRTIO_SND_RATES_176400         (1U << 11)
#define VIRTIO_SND_RATES_192000         (1U << 12)

#define VIRTIO_SND_FMT_S8              0
#define VIRTIO_SND_FMT_U8               1
#define VIRTIO_SND_FMT_S16              2
#define VIRTIO_SND_FMT_U16              3
#define VIRTIO_SND_FMT_S32              4
#define VIRTIO_SND_FMT_U32              5

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_SND_QUEUE_SIZE];
} virtq_avail_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[VIRTIO_SND_QUEUE_SIZE];
} virtq_used_t;

typedef struct __attribute__((packed)) {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t format;
    uint8_t padding[2];
} virtio_snd_pcm_hdr_t;

static virtq_desc_t g_desc[VIRTIO_SND_QUEUE_SIZE] __attribute__((aligned(16)));
static virtq_avail_t g_avail __attribute__((aligned(2)));
static virtq_used_t g_used __attribute__((aligned(4)));
static uint8_t g_buffer[VIRTIO_SND_QUEUE_SIZE * 4096] __attribute__((aligned(4096)));

static volatile uint32_t *virtio_reg(uint64_t base, uint32_t offset) {
    return (volatile uint32_t *)(uintptr_t)(base + offset);
}

static uint32_t virtio_read32(uint64_t base, uint32_t offset) {
    return *virtio_reg(base, offset);
}

static void virtio_write32(uint64_t base, uint32_t offset, uint32_t value) {
    *virtio_reg(base, offset) = value;
}

static void virtio_write64(uint64_t base, uint32_t offset_low, uint32_t offset_high, uint64_t value) {
    virtio_write32(base, offset_low, (uint32_t)value);
    virtio_write32(base, offset_high, (uint32_t)(value >> 32));
}

static void mb(void) {
    __asm__ volatile("dmb sy" ::: "memory");
}

int virtio_snd_probe(uint64_t base) {
    uint32_t magic = virtio_read32(base, VIRTIO_MMIO_MAGIC_VALUE);
    if (magic != VIRTIO_MMIO_MAGIC) {
        return -1;
    }

    uint32_t version = virtio_read32(base, VIRTIO_MMIO_VERSION);
    if (version != 2) {
        return -1;
    }

    uint32_t device_id = virtio_read32(base, VIRTIO_MMIO_DEVICE_ID);
    if (device_id != VIRTIO_DEVICE_ID_SOUND) {
        return -1;
    }

    return 0;
}

static int setup_queue(uint64_t base, uint32_t queue_sel) {
    virtio_write32(base, VIRTIO_MMIO_QUEUE_SEL, queue_sel);

    uint32_t qsize = virtio_read32(base, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (qsize == 0 || qsize > VIRTIO_SND_QUEUE_SIZE) {
        return -1;
    }

    virtio_write32(base, VIRTIO_MMIO_QUEUE_NUM, VIRTIO_SND_QUEUE_SIZE);

    for (uint32_t i = 0; i < VIRTIO_SND_QUEUE_SIZE; i++) {
        g_desc[i].addr = (uint64_t)(uintptr_t)g_buffer + (i * 4096);
        g_desc[i].len = 4096;
        g_desc[i].flags = 1;
        g_desc[i].next = (i + 1) % VIRTIO_SND_QUEUE_SIZE;
    }

    g_avail.flags = 0;
    g_avail.idx = 0;
    for (uint32_t i = 0; i < VIRTIO_SND_QUEUE_SIZE; i++) {
        g_avail.ring[i] = i;
    }

    g_used.flags = 0;
    g_used.idx = 0;

    virtio_write64(base, VIRTIO_MMIO_QUEUE_DESC_LOW, VIRTIO_MMIO_QUEUE_DESC_HIGH,
                   (uint64_t)(uintptr_t)g_desc);
    virtio_write64(base, VIRTIO_MMIO_QUEUE_DRIVER_LOW, VIRTIO_MMIO_QUEUE_DRIVER_HIGH,
                   (uint64_t)(uintptr_t)&g_avail);
    virtio_write64(base, VIRTIO_MMIO_QUEUE_DEVICE_LOW, VIRTIO_MMIO_QUEUE_DEVICE_HIGH,
                   (uint64_t)(uintptr_t)&g_used);

    mb();

    virtio_write32(base, VIRTIO_MMIO_QUEUE_READY, 1);

    return 0;
}

int virtio_snd_init(virtio_snd_device_t *device, uint64_t base, uint32_t irq) {
    device->base = base;
    device->irq = irq;
    device->ready = 0;
    device->num_channels = 1;

    uint8_t status = 0;
    status |= VIRTIO_STATUS_ACKNOWLEDGE;
    virtio_write32(base, VIRTIO_MMIO_STATUS, status);

    status |= VIRTIO_STATUS_DRIVER;
    virtio_write32(base, VIRTIO_MMIO_STATUS, status);

    if (setup_queue(base, VIRTIO_SND_PLAYBACK_QUEUE) != 0) {
        return -1;
    }

    status |= VIRTIO_STATUS_DRIVER_OK;
    virtio_write32(base, VIRTIO_MMIO_STATUS, status);

    device->ready = 1;

    return 0;
}

int virtio_snd_write_samples(virtio_snd_device_t *device, const int16_t *samples, uint32_t count) {
    (void)device;

    if (!device->ready) {
        return -1;
    }

    if (count > VIRTIO_SND_QUEUE_SIZE) {
        count = VIRTIO_SND_QUEUE_SIZE;
    }

    uint16_t slot = g_avail.idx % VIRTIO_SND_QUEUE_SIZE;

    uint8_t *buf = g_buffer + (slot * 4096);
    uint32_t max_samples = (4096 / sizeof(int16_t)) / 2;
    if (count > max_samples) {
        count = max_samples;
    }
    for (uint32_t i = 0; i < count * 2; i++) {
        ((int16_t *)buf)[i] = samples[i];
    }

    mb();

    g_avail.ring[g_avail.idx % VIRTIO_SND_QUEUE_SIZE] = slot;
    g_avail.idx++;

    mb();

    virtio_write32(device->base, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_SND_PLAYBACK_QUEUE);

    return 0;
}
