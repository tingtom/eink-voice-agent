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

void speaker_init(void)
{
    i2s_chan_config_t chan_cfg = {
        .id = SPK_I2S_PORT,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 8,
        .dma_frame_num = 1024,
        .auto_clear = true,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &spk_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = 16,
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .bit_shift = true,
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SPK_I2S_BCLK_GPIO,
            .ws = SPK_I2S_LRCK_GPIO,
            .dout = SPK_I2S_DATA_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(spk_chan, &std_cfg));
    ESP_LOGI(TAG, "Speaker initialized (I2S, %d Hz)", AUDIO_SAMPLE_RATE);
}

esp_err_t speaker_play(const int16_t *audio, size_t samples)
{
    if (!spk_chan) return ESP_ERR_INVALID_STATE;

    esp_err_t ret = i2s_channel_enable(spk_chan);
    if (ret != ESP_OK) return ret;

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
    ret = i2s_channel_write(spk_chan, src, samples * sizeof(int16_t), &bytes_written, portMAX_DELAY);

    free(scaled);
    is_playing = false;
    i2s_channel_disable(spk_chan);

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

    esp_err_t ret = i2s_channel_enable(spk_chan);
    if (ret != ESP_OK) { fclose(f); return; }

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
    i2s_channel_disable(spk_chan);

    ESP_LOGI(TAG, "Playback finished: %s", pcm_path);
}

void speaker_stop(void)
{
    if (is_playing) {
        i2s_channel_disable(spk_chan);
        is_playing = false;
    }
}

void speaker_set_volume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    volume = vol;
}
