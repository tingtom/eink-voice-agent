#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_codec_dev.h"
#include "app_config.h"
#include "es8311.h"
#include "driver/i2s_std.h"
#include "esp_timer.h"

static const char *TAG = "MIC";

esp_err_t mic_read(int16_t *buffer, size_t samples, size_t *read)
{
    i2s_chan_handle_t rx = es8311_get_rx_handle();
    if (!rx) {
        ESP_LOGE(TAG, "mic_read: RX handle is NULL");
        if (read) *read = 0;
        return ESP_ERR_INVALID_STATE;
    }

    size_t bytes_read = 0;
    size_t bytes_requested = samples * sizeof(int16_t);
    int64_t t0 = esp_timer_get_time();
    esp_err_t ret = i2s_channel_read(rx, buffer, bytes_requested, &bytes_read, pdMS_TO_TICKS(1000));
    int64_t t1 = esp_timer_get_time();
    int32_t elapsed_us = (int32_t)(t1 - t0);

    size_t samples_read = bytes_read / sizeof(int16_t);
    if (read) *read = samples_read;

    ESP_LOGD(TAG, "i2s_channel_read: ret=%s requested=%d got=%d bytes (%d samples) elapsed=%ld us",
             esp_err_to_name(ret), (int)bytes_requested, (int)bytes_read, (int)samples_read, (long)elapsed_us);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_read FAILED: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }
    return ESP_OK;
}
