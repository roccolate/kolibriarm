#include "audio/audio.h"

#include <string.h>

static audio_mixer_t g_mixer;

void audio_mixer_init(void) {
    for (size_t i = 0; i < sizeof(g_mixer); i++) {
        ((uint8_t *)&g_mixer)[i] = 0;
    }
    g_mixer.format = AUDIO_FORMAT_PCM_S16;
    g_mixer.sample_rate = AUDIO_SAMPLE_RATE;
    g_mixer.channels = 2;
    g_mixer.active = 0;
}

void audio_mixer_set_format(audio_format_t format, uint32_t sample_rate, uint8_t channels) {
    g_mixer.format = format;
    g_mixer.sample_rate = sample_rate;
    g_mixer.channels = channels;
}

int audio_mixer_play(int channel, const int16_t *data, uint32_t size, int loop) {
    if (channel < 0 || channel >= AUDIO_MIXER_MAX_CHANNELS) {
        return -1;
    }

    if (data == 0 || size == 0) {
        return -1;
    }

    audio_channel_t *ch = &g_mixer.channel[channel];
    ch->data = data;
    ch->size = size;
    ch->position = 0;
    ch->loop = loop;
    ch->volume = 100;

    g_mixer.active = 1;

    return 0;
}

void audio_mixer_stop(int channel) {
    if (channel < 0 || channel >= AUDIO_MIXER_MAX_CHANNELS) {
        return;
    }

    audio_channel_t *ch = &g_mixer.channel[channel];
    ch->data = 0;
    ch->size = 0;
    ch->position = 0;
    ch->loop = 0;
}

void audio_mixer_set_volume(int channel, int volume) {
    if (channel < 0 || channel >= AUDIO_MIXER_MAX_CHANNELS) {
        return;
    }

    if (volume < 0) {
        volume = 0;
    }
    if (volume > 100) {
        volume = 100;
    }

    g_mixer.channel[channel].volume = volume;
}

static int16_t clamp_s16(int32_t sample) {
    if (sample > 32767) {
        return 32767;
    }
    if (sample < -32768) {
        return -32768;
    }
    return (int16_t)sample;
}

int audio_mixer_mix(int16_t *buffer, uint32_t num_samples) {
    if (buffer == 0 || num_samples == 0) {
        return -1;
    }

    if (!g_mixer.active) {
        for (uint32_t i = 0; i < num_samples * 2; i++) {
            buffer[i] = 0;
        }
        return 0;
    }

    for (uint32_t i = 0; i < num_samples; i++) {
        int32_t left = 0;
        int32_t right = 0;

        for (int ch = 0; ch < AUDIO_MIXER_MAX_CHANNELS; ch++) {
            audio_channel_t *channel = &g_mixer.channel[ch];

            if (channel->data == 0 || channel->size == 0) {
                continue;
            }

            int16_t sample = channel->data[channel->position];

            int vol = channel->volume;
            int32_t scaled = (int32_t)sample * vol / 100;

            left += scaled;
            right += scaled;

            channel->position++;

            if (channel->position >= channel->size) {
                if (channel->loop) {
                    channel->position = 0;
                } else {
                    channel->data = 0;
                    channel->size = 0;
                    channel->position = 0;
                }
            }
        }

        buffer[i * 2] = clamp_s16(left);
        buffer[i * 2 + 1] = clamp_s16(right);
    }

    int has_active = 0;
    for (int ch = 0; ch < AUDIO_MIXER_MAX_CHANNELS; ch++) {
        if (g_mixer.channel[ch].data != 0 && g_mixer.channel[ch].size > 0) {
            has_active = 1;
            break;
        }
    }
    if (!has_active) {
        g_mixer.active = 0;
    }

    return 0;
}

int audio_mixer_has_active_channels(void) {
    return g_mixer.active;
}
