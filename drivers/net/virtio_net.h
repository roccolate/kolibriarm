#ifndef KOLIBRIARM_DRIVERS_NET_VIRTIO_NET_H
#define KOLIBRIARM_DRIVERS_NET_VIRTIO_NET_H

#include <stdint.h>

typedef struct {
    uint8_t mac[6];
    uint32_t status;
} virtio_net_info_t;

typedef struct {
    uint64_t base;
    uint32_t queue_size;
    uint16_t last_used_idx;
    uint8_t ready;
    uint8_t mac[6];
} virtio_net_device_t;

int virtio_net_probe(uint64_t base, virtio_net_info_t *info);
int virtio_net_probe_range(uint64_t base, uint64_t size, uint64_t stride,
                           uint64_t *found_base, virtio_net_info_t *info);
int virtio_net_init(virtio_net_device_t *device, uint64_t base);
int virtio_net_send(virtio_net_device_t *device, const void *data, uint32_t len);
int virtio_net_recv(virtio_net_device_t *device, void *data, uint32_t max_len);
int virtio_net_poll(virtio_net_device_t *device);

#endif