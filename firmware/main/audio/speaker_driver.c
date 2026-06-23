#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "app_config.h"

static const char *TAG = "SPEAKER";

static i2s_chan_handle_t spk_chan = NULL;
static bool is_playing = false;
static uint8_t volume = 80;

void speaker_set_handle(i2s_chan_handle_t handle)
{
    spk_chan = handle;
}

void speaker_init(void)
{
    ESP_LOGI(TAG, "Speaker ready (I2S, %d Hz)", AUDIO_SAMPLE_RATE);
}

esp_err_t speaker_enable(void)
{
    if (!spk_chan) return ESP_ERR_INVALID_STATE;
    return i2s_channel_enable(spk_chan);
}

esp_err_t speaker_play(const int16_t *audio, size_t samples)
{
    if (!spk_chan) return ESP_ERR_INVALID_STATE;

    is_playing = true;

    int16_t *scaled = NULL;
    int16_t *src = (int16_t *)audio;

    if (volume < 100) {
        scaled = (int16_t *)malloc(samples * sizeof(int16_t));
        if (scaled) {
            for (size_t i = 0; i < samples; i++) {
                scaled[i] = (src[i] * volume) / 100;
            }
            src = scaled;
        }
    }

    size_t bytes_written = 0;
    esp_err_t ret = i2s_channel_write(spk_chan, src, samples * sizeof(int16_t), &bytes_written, portMAX_DELAY);

    free(scaled);
    is_playing = false;

    return ret;
}

void speaker_play_file(const char *pcm_path)
{
    if (!spk_chan) return;

    FILE *f = fopen(pcm_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", pcm_path);
        return;
    }

    is_playing = true;
    const size_t chunk = 512;
    int16_t buf[chunk];
    int16_t scaled[chunk];

    while (1) {
        size_t read = fread(buf, sizeof(int16_t), chunk, f);
        if (read == 0) break;

        int16_t *src = buf;
        if (volume < 100) {
            for (size_t i = 0; i < read; i++)
                scaled[i] = (buf[i] * volume) / 100;
            src = scaled;
        }
        size_t written = 0;
        i2s_channel_write(spk_chan, src, read * sizeof(int16_t), &written, portMAX_DELAY);
    }

    fclose(f);
    is_playing = false;

    ESP_LOGI(TAG, "Playback finished: %s", pcm_path);
}

void speaker_stop(void)
{
    is_playing = false;
}

void speaker_set_volume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    volume = vol;
}
