#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "app_config.h"

static const char *TAG = "MIC";

static i2s_chan_handle_t mic_chan = NULL;
static bool is_running = false;
static int mic_dbg_counter = 0;

void mic_set_handle(i2s_chan_handle_t handle)
{
    mic_chan = handle;
}

void mic_init(void)
{
    ESP_LOGI(TAG, "Microphone ready (I2S, %d Hz, mono)", AUDIO_SAMPLE_RATE);
}

void mic_start(void)
{
    if (!is_running) {
        ESP_ERROR_CHECK(i2s_channel_enable(mic_chan));
        is_running = true;
        ESP_LOGI(TAG, "Microphone started");
    }
}

void mic_stop(void)
{
    if (is_running) {
        ESP_ERROR_CHECK(i2s_channel_disable(mic_chan));
        is_running = false;
        ESP_LOGI(TAG, "Microphone stopped");
    }
}

esp_err_t mic_read(int16_t *buffer, size_t samples, size_t *read)
{
    size_t bytes_read = 0;
    esp_err_t ret = i2s_channel_read(mic_chan, buffer, samples * sizeof(int16_t), &bytes_read, portMAX_DELAY);
    if (ret == ESP_OK && read) {
        *read = bytes_read / sizeof(int16_t);
    }

    // Debug: log mic levels every ~100 reads (~1s at 50ms intervals)
    mic_dbg_counter++;
    if (mic_dbg_counter % 100 == 0 && *read > 0) {
        // First 4 samples
        ESP_LOGI(TAG, "mic samples[0..3]: %d %d %d %d",
                 buffer[0], buffer[1], buffer[2], buffer[3]);
        // Average absolute level
        int64_t sum_abs = 0;
        for (size_t i = 0; i < *read; i++) {
            int32_t s = buffer[i];
            if (s < 0) s = -s;
            sum_abs += s;
        }
        int32_t avg = (int32_t)(sum_abs / *read);
        // Check if all zeros (suggests no mic data / wrong format)
        int zeros = 0;
        for (size_t i = 0; i < (*read < 32 ? *read : 32); i++) {
            if (buffer[i] == 0) zeros++;
        }
        ESP_LOGI(TAG, "mic avg_abs=%ld  zero_in_first_32=%d/32  read=%u", (long)avg, zeros, (unsigned)*read);
    }

    return ret;
}
