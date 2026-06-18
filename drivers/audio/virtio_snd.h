#ifndef KOLIBRIARM_DRIVERS_AUDIO_VIRTIO_SND_H
#define KOLIBRIARM_DRIVERS_AUDIO_VIRTIO_SND_H

#include <stdint.h>

#include "audio/audio.h"

typedef struct {
    uint64_t base;
    uint32_t irq;
    uint8_t ready;
    uint8_t num_channels;
} virtio_snd_device_t;

int virtio_snd_probe(uint64_t base);
int virtio_snd_init(virtio_snd_device_t *device, uint64_t base, uint32_t irq);
int virtio_snd_write_samples(virtio_snd_device_t *device, const int16_t *samples, uint32_t count);

#endif
