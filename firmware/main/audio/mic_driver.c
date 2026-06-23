#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "app_config.h"

static const char *TAG = "MIC";

static i2s_chan_handle_t mic_chan = NULL;
static bool is_running = false;

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
    return ret;
}
