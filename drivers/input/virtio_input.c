#include "input/virtio_input.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH   0x0a4
#define VIRTIO_MMIO_CONFIG              0x100
#define VIRTIO_MMIO_INT_STATUS           0x60
#define VIRTIO_MMIO_INT_ENABLE           0x064

#define VIRTIO_MMIO_MAGIC               0x74726976U
#define VIRTIO_DEVICE_ID_INPUT          18U
#define VIRTIO_STATUS_ACKNOWLEDGE       1U
#define VIRTIO_STATUS_DRIVER            2U
#define VIRTIO_STATUS_DRIVER_OK         4U
#define VIRTIO_STATUS_FAILED            128U
#define VIRTQ_DESC_F_NEXT               1U
#define VIRTQ_DESC_F_WRITE              2U
#define VIRTIO_INPUT_QUEUE_SIZE         16U

#define VIRTIO_INPUT_EVENTQ             0

typedef struct {
    uint16_t type;
    uint16_t code;
    int32_t value;
} virtio_input_event_t;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_INPUT_QUEUE_SIZE];
} virtq_avail_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[VIRTIO_INPUT_QUEUE_SIZE];
} virtq_used_t;

static virtq_desc_t g_desc[VIRTIO_INPUT_QUEUE_SIZE] __attribute__((aligned(16)));
static virtq_avail_t g_avail __attribute__((aligned(2)));
static virtq_used_t g_used __attribute__((aligned(4)));
static virtio_input_event_t g_events[VIRTIO_INPUT_QUEUE_SIZE] __attribute__((aligned(8)));

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

int virtio_input_probe(uint64_t base) {
    uint32_t magic = virtio_read32(base, VIRTIO_MMIO_MAGIC_VALUE);
    if (magic != VIRTIO_MMIO_MAGIC) {
        return -1;
    }

    uint32_t version = virtio_read32(base, VIRTIO_MMIO_VERSION);
    if (version != 2) {
        return -1;
    }

    uint32_t device_id = virtio_read32(base, VIRTIO_MMIO_DEVICE_ID);
    if (device_id != VIRTIO_DEVICE_ID_INPUT) {
        return -1;
    }

    return 0;
}

int virtio_input_probe_range(uint64_t base, uint64_t size, uint64_t stride,
                             uint64_t *found_base) {
    if (base == 0 || size == 0 || stride == 0 || found_base == 0) {
        return -1;
    }

    for (uint64_t offset = 0; offset < size; offset += stride) {
        uint64_t candidate = base + offset;
        if (virtio_input_probe(candidate) == 0) {
            *found_base = candidate;
            return 0;
        }
    }

    return -1;
}

static int setup_event_queue(uint64_t base) {
    virtio_write32(base, VIRTIO_MMIO_QUEUE_SEL, VIRTIO_INPUT_EVENTQ);

    uint32_t qsize = virtio_read32(base, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (qsize == 0) {
        return -1;
    }
    if (qsize > VIRTIO_INPUT_QUEUE_SIZE) {
        qsize = VIRTIO_INPUT_QUEUE_SIZE;
    }

    virtio_write32(base, VIRTIO_MMIO_QUEUE_NUM, qsize);

    for (size_t i = 0; i < VIRTIO_INPUT_QUEUE_SIZE; i++) {
        g_desc[i].addr = 0;
        g_desc[i].len = 0;
        g_desc[i].flags = 0;
        g_desc[i].next = 0;
    }
    g_avail.flags = 0;
    g_avail.idx = 0;
    g_used.flags = 0;
    g_used.idx = 0;

    for (uint32_t i = 0; i < qsize; i++) {
        g_desc[i].addr = (uint64_t)(uintptr_t)&g_events[i];
        g_desc[i].len = sizeof(virtio_input_event_t);
        g_desc[i].flags = VIRTQ_DESC_F_WRITE;
        g_desc[i].next = (i + 1) % qsize;
    }

    g_avail.flags = 0;
    g_avail.idx = 0;
    for (uint32_t i = 0; i < qsize; i++) {
        g_avail.ring[i] = i;
    }

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

int virtio_input_init(virtio_input_device_t *device, uint64_t base) {
    device->base = base;
    device->ready = 0;
    device->queue_size = VIRTIO_INPUT_QUEUE_SIZE;
    device->last_used_idx = 0;

    uint8_t status = 0;
    status |= VIRTIO_STATUS_ACKNOWLEDGE;
    virtio_write32(base, VIRTIO_MMIO_STATUS, status);

    status |= VIRTIO_STATUS_DRIVER;
    virtio_write32(base, VIRTIO_MMIO_STATUS, status);

    if (setup_event_queue(base) != 0) {
        return -1;
    }

    status |= VIRTIO_STATUS_DRIVER_OK;
    virtio_write32(base, VIRTIO_MMIO_STATUS, status);

    device->ready = 1;

    return 0;
}

int virtio_input_has_events(virtio_input_device_t *device) {
    if (!device->ready) {
        return 0;
    }

    mb();
    return g_used.idx != device->last_used_idx;
}

int virtio_input_poll(virtio_input_device_t *device) {
    if (!device->ready) {
        return -1;
    }

    while (device->last_used_idx != g_used.idx) {
        mb();

        uint16_t slot = device->last_used_idx % device->queue_size;
        virtio_input_event_t *ev = &g_events[slot];

        input_event_t event = {0};
        event.timestamp = 0;

        switch (ev->type) {
        case VIRTIO_INPUT_EV_SYN:
            break;
        case VIRTIO_INPUT_EV_KEY:
            /* Mouse buttons live in the same EV_KEY code range as keyboard
             * scancodes (0x110..0x112). Translate them to MOUSE_BUTTON so
             * the GUI layer does not have to know about Linux codes. */
            if (ev->code >= VIRTIO_INPUT_BTN_LEFT &&
                ev->code <= VIRTIO_INPUT_BTN_MIDDLE) {
                event.type = INPUT_EVENT_MOUSE_BUTTON;
                event.data.mouse_button.button =
                    (uint32_t)(ev->code - VIRTIO_INPUT_BTN_LEFT);
                event.data.mouse_button.pressed = (ev->value != 0) ? 1U : 0U;
                input_queue_push(&event);
                break;
            }
            event.type = (ev->value) ? INPUT_EVENT_KEY_PRESS : INPUT_EVENT_KEY_RELEASE;
            event.data.key.key = ev->code;
            input_queue_push(&event);
            break;
        case VIRTIO_INPUT_EV_REL:
            if (ev->code == 0 || ev->code == 1) {
                event.type = INPUT_EVENT_MOUSE_MOVE;
                if (ev->code == 0) {
                    event.data.mouse_move.dx = ev->value;
                    event.data.mouse_move.dy = 0;
                } else {
                    event.data.mouse_move.dx = 0;
                    event.data.mouse_move.dy = ev->value;
                }
                input_queue_push(&event);
            }
            break;
        case VIRTIO_INPUT_EV_ABS:
            break;
        default:
            break;
        }

        device->last_used_idx++;

        mb();
        g_avail.idx++;
        mb();
    }

    return 0;
}
