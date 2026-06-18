#ifndef KOLIBRIARM_DRIVERS_AUDIO_AUDIO_H
#define KOLIBRIARM_DRIVERS_AUDIO_AUDIO_H

#include <stdint.h>

#define AUDIO_MIXER_MAX_CHANNELS 8
#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_BUFFER_SIZE 512

typedef enum {
    AUDIO_FORMAT_PCM_U8,
    AUDIO_FORMAT_PCM_S16,
    AUDIO_FORMAT_PCM_S32,
} audio_format_t;

typedef struct {
    int16_t left;
    int16_t right;
} audio_stereo_sample_t;

typedef struct {
    const int16_t *data;
    uint32_t size;
    uint32_t position;
    int loop;
    int volume;
} audio_channel_t;

typedef struct {
    audio_format_t format;
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t active;
    audio_channel_t channel[AUDIO_MIXER_MAX_CHANNELS];
} audio_mixer_t;

void audio_mixer_init(void);
void audio_mixer_set_format(audio_format_t format, uint32_t sample_rate, uint8_t channels);
int audio_mixer_play(int channel, const int16_t *data, uint32_t size, int loop);
void audio_mixer_stop(int channel);
void audio_mixer_set_volume(int channel, int volume);
int audio_mixer_mix(int16_t *buffer, uint32_t num_samples);

int audio_mixer_has_active_channels(void);

#endif
