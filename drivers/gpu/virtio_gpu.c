#include "gpu/virtio_gpu.h"

#include <stdint.h>

#include "fb/fb.h"

#define VIRTIO_MMIO_MAGIC_VALUE 0x000
#define VIRTIO_MMIO_VERSION     0x004
#define VIRTIO_MMIO_DEVICE_ID   0x008
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

#define VIRTIO_MMIO_MAGIC       0x74726976U
#define VIRTIO_DEVICE_ID_GPU    16U
#define VIRTIO_STATUS_ACKNOWLEDGE 1U
#define VIRTIO_STATUS_DRIVER    2U
#define VIRTIO_STATUS_DRIVER_OK 4U
#define VIRTIO_STATUS_FEATURES_OK 8U
#define VIRTQ_DESC_F_NEXT       1U
#define VIRTQ_DESC_F_WRITE      2U
#define VIRTIO_F_VERSION_1_HIGH 1U
#define VIRTIO_GPU_QUEUE_SIZE   8U
#define VIRTIO_GPU_WIDTH        640U
#define VIRTIO_GPU_HEIGHT       480U
#define VIRTIO_GPU_RESOURCE_ID  1U

#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D  0x0101U
#define VIRTIO_GPU_CMD_SET_SCANOUT         0x0103U
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH      0x0104U
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105U
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106U
#define VIRTIO_GPU_RESP_OK_NODATA          0x1100U
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM   1U

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_GPU_QUEUE_SIZE];
} virtq_avail_t;

typedef struct {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[VIRTIO_GPU_QUEUE_SIZE];
} virtq_used_t;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} gpu_ctrl_hdr_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} gpu_rect_t;

typedef struct {
    gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} gpu_resource_create_2d_t;

typedef struct {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} gpu_mem_entry_t;

typedef struct {
    gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
    gpu_mem_entry_t entry;
} gpu_resource_attach_backing_t;

typedef struct {
    gpu_ctrl_hdr_t hdr;
    gpu_rect_t rect;
    uint32_t scanout_id;
    uint32_t resource_id;
} gpu_set_scanout_t;

typedef struct {
    gpu_ctrl_hdr_t hdr;
    gpu_rect_t rect;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} gpu_transfer_to_host_2d_t;

typedef struct {
    gpu_ctrl_hdr_t hdr;
    gpu_rect_t rect;
    uint32_t resource_id;
    uint32_t padding;
} gpu_resource_flush_t;

static virtq_desc_t g_desc[VIRTIO_GPU_QUEUE_SIZE] __attribute__((aligned(16)));
static virtq_avail_t g_avail __attribute__((aligned(2)));
static virtq_used_t g_used __attribute__((aligned(4)));
static uint16_t g_last_used_idx;
static uint32_t g_pixels[VIRTIO_GPU_WIDTH * VIRTIO_GPU_HEIGHT]
    __attribute__((aligned(4096)));
static uint64_t g_queue_base;
static uint8_t g_queue_ready;
static uint8_t g_resource_ready;

static volatile uint32_t *gpu_reg(uint64_t base, uint32_t offset) {
    return (volatile uint32_t *)(uintptr_t)(base + offset);
}

static void gpu_write64(uint64_t base, uint32_t low_offset, uint64_t value) {
    *gpu_reg(base, low_offset) = (uint32_t)value;
    *gpu_reg(base, low_offset + 4U) = (uint32_t)(value >> 32);
}

static void gpu_barrier(void) {
#if defined(__aarch64__)
    __asm__ volatile("dmb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}

static void gpu_init_hdr(gpu_ctrl_hdr_t *hdr, uint32_t type) {
    hdr->type = type;
    hdr->flags = 0;
    hdr->fence_id = 0;
    hdr->ctx_id = 0;
    hdr->padding = 0;
}

static gpu_rect_t gpu_full_rect(void) {
    gpu_rect_t rect;
    rect.x = 0;
    rect.y = 0;
    rect.width = VIRTIO_GPU_WIDTH;
    rect.height = VIRTIO_GPU_HEIGHT;
    return rect;
}

static int virtio_gpu_probe(uint64_t base) {
    if (base == 0) {
        return -1;
    }

    if (*gpu_reg(base, VIRTIO_MMIO_MAGIC_VALUE) != VIRTIO_MMIO_MAGIC ||
        *gpu_reg(base, VIRTIO_MMIO_DEVICE_ID) != VIRTIO_DEVICE_ID_GPU ||
        *gpu_reg(base, VIRTIO_MMIO_VERSION) != 2U) {
        return -1;
    }

    return 0;
}

int virtio_gpu_probe_range(uint64_t base, uint64_t size, uint64_t stride,
                           uint64_t *found_base) {
    if (base == 0 || size == 0 || stride == 0 || found_base == 0) {
        return -1;
    }

    for (uint64_t offset = 0; offset < size; offset += stride) {
        uint64_t candidate = base + offset;

        if (virtio_gpu_probe(candidate) == 0) {
            *found_base = candidate;
            return 0;
        }
    }

    return -1;
}

static int gpu_init_queue(uint64_t base) {
    if (virtio_gpu_probe(base) != 0) {
        return -1;
    }

    *gpu_reg(base, VIRTIO_MMIO_STATUS) = 0;
    *gpu_reg(base, VIRTIO_MMIO_STATUS) = VIRTIO_STATUS_ACKNOWLEDGE;
    *gpu_reg(base, VIRTIO_MMIO_STATUS) =
        VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
    *gpu_reg(base, VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 0;
    *gpu_reg(base, VIRTIO_MMIO_DRIVER_FEATURES) = 0;
    *gpu_reg(base, VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 1;
    *gpu_reg(base, VIRTIO_MMIO_DRIVER_FEATURES) = VIRTIO_F_VERSION_1_HIGH;
    *gpu_reg(base, VIRTIO_MMIO_STATUS) =
        VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
        VIRTIO_STATUS_FEATURES_OK;

    if ((*gpu_reg(base, VIRTIO_MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0) {
        return -1;
    }

    *gpu_reg(base, VIRTIO_MMIO_QUEUE_SEL) = 0;
    if (*gpu_reg(base, VIRTIO_MMIO_QUEUE_NUM_MAX) < 2U) {
        return -1;
    }

    for (uint32_t i = 0; i < VIRTIO_GPU_QUEUE_SIZE; i++) {
        g_desc[i].addr = 0;
        g_desc[i].len = 0;
        g_desc[i].flags = 0;
        g_desc[i].next = 0;
        g_avail.ring[i] = 0;
        g_used.ring[i].id = 0;
        g_used.ring[i].len = 0;
    }
    g_avail.flags = 0;
    g_avail.idx = 0;
    g_used.flags = 0;
    g_used.idx = 0;
    g_last_used_idx = 0;

    *gpu_reg(base, VIRTIO_MMIO_QUEUE_NUM) = VIRTIO_GPU_QUEUE_SIZE;
    gpu_write64(base, VIRTIO_MMIO_QUEUE_DESC_LOW, (uint64_t)(uintptr_t)g_desc);
    gpu_write64(base, VIRTIO_MMIO_QUEUE_DRIVER_LOW, (uint64_t)(uintptr_t)&g_avail);
    gpu_write64(base, VIRTIO_MMIO_QUEUE_DEVICE_LOW, (uint64_t)(uintptr_t)&g_used);
    *gpu_reg(base, VIRTIO_MMIO_QUEUE_READY) = 1;
    gpu_barrier();
    *gpu_reg(base, VIRTIO_MMIO_STATUS) =
        VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
        VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK;

    return 0;
}

static int gpu_submit(uint64_t base, const void *request, uint32_t request_len,
                      gpu_ctrl_hdr_t *response) {
    uint16_t avail_idx = g_avail.idx;

    response->type = 0;
    g_desc[0].addr = (uint64_t)(uintptr_t)request;
    g_desc[0].len = request_len;
    g_desc[0].flags = VIRTQ_DESC_F_NEXT;
    g_desc[0].next = 1;
    g_desc[1].addr = (uint64_t)(uintptr_t)response;
    g_desc[1].len = sizeof(*response);
    g_desc[1].flags = VIRTQ_DESC_F_WRITE;
    g_desc[1].next = 0;
    g_avail.ring[avail_idx % VIRTIO_GPU_QUEUE_SIZE] = 0;
    gpu_barrier();
    g_avail.idx = avail_idx + 1U;
    gpu_barrier();
    *gpu_reg(base, VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

    for (uint32_t spins = 0; spins < 10000000U; spins++) {
        gpu_barrier();
        if (g_used.idx != g_last_used_idx) {
            g_last_used_idx = g_used.idx;
            return response->type == VIRTIO_GPU_RESP_OK_NODATA ? 0 : -1;
        }
    }

    return -1;
}

static void fill_pattern(fb_t *fb, void *context) {
    (void)context;

    fb_fillrect(fb, 0, 0, fb->width, fb->height, 0xff202020U);
    fb_fillrect(fb, 80, 80, 480, 320, 0xff00b050U);
    fb_fillrect(fb, 180, 180, 280, 120, 0xfff0d040U);
}

static int gpu_ensure_ready(uint64_t base) {
    if (g_queue_ready != 0 && g_queue_base == base) {
        return 0;
    }

    g_queue_ready = 0;
    g_resource_ready = 0;
    if (gpu_init_queue(base) != 0) {
        return -1;
    }

    g_queue_base = base;
    g_queue_ready = 1;
    return 0;
}

static int gpu_ensure_resource(uint64_t base, gpu_rect_t rect) {
    gpu_ctrl_hdr_t response;
    gpu_resource_create_2d_t create;
    gpu_resource_attach_backing_t attach;
    gpu_set_scanout_t scanout;

    if (g_resource_ready != 0) {
        return 0;
    }

    gpu_init_hdr(&create.hdr, VIRTIO_GPU_CMD_RESOURCE_CREATE_2D);
    create.resource_id = VIRTIO_GPU_RESOURCE_ID;
    create.format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    create.width = VIRTIO_GPU_WIDTH;
    create.height = VIRTIO_GPU_HEIGHT;

    gpu_init_hdr(&attach.hdr, VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING);
    attach.resource_id = VIRTIO_GPU_RESOURCE_ID;
    attach.nr_entries = 1;
    attach.entry.addr = (uint64_t)(uintptr_t)g_pixels;
    attach.entry.length = sizeof(g_pixels);
    attach.entry.padding = 0;

    gpu_init_hdr(&scanout.hdr, VIRTIO_GPU_CMD_SET_SCANOUT);
    scanout.rect = rect;
    scanout.scanout_id = 0;
    scanout.resource_id = VIRTIO_GPU_RESOURCE_ID;

    if (gpu_submit(base, &create, sizeof(create), &response) != 0 ||
        gpu_submit(base, &attach, sizeof(attach), &response) != 0 ||
        gpu_submit(base, &scanout, sizeof(scanout), &response) != 0) {
        return -1;
    }

    g_resource_ready = 1;
    return 0;
}

int virtio_gpu_draw(uint64_t base, virtio_gpu_render_fn_t render,
                    void *context) {
    gpu_ctrl_hdr_t response;
    gpu_transfer_to_host_2d_t transfer;
    gpu_resource_flush_t flush;
    gpu_rect_t rect;
    fb_t fb;

    if (render == 0 || gpu_ensure_ready(base) != 0 ||
        fb_init(&fb, g_pixels, VIRTIO_GPU_WIDTH, VIRTIO_GPU_HEIGHT,
                VIRTIO_GPU_WIDTH) != 0) {
        return -1;
    }

    render(&fb, context);
    rect = gpu_full_rect();

    if (gpu_ensure_resource(base, rect) != 0) {
        return -1;
    }

    gpu_init_hdr(&transfer.hdr, VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D);
    transfer.rect = rect;
    transfer.offset = 0;
    transfer.resource_id = VIRTIO_GPU_RESOURCE_ID;
    transfer.padding = 0;

    gpu_init_hdr(&flush.hdr, VIRTIO_GPU_CMD_RESOURCE_FLUSH);
    flush.rect = rect;
    flush.resource_id = VIRTIO_GPU_RESOURCE_ID;
    flush.padding = 0;

    if (gpu_submit(base, &transfer, sizeof(transfer), &response) != 0 ||
        gpu_submit(base, &flush, sizeof(flush), &response) != 0) {
        return -1;
    }

    return 0;
}

int virtio_gpu_draw_test_pattern(uint64_t base) {
    return virtio_gpu_draw(base, fill_pattern, 0);
}
