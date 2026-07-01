#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_codec_dev.h"
#include "app_config.h"
#include "es8311.h"
#include "driver/i2s_std.h"

static const char *TAG = "MIC";

esp_err_t mic_read(int16_t *buffer, size_t samples, size_t *read)
{
    i2s_chan_handle_t rx = es8311_get_rx_handle();
    if (!rx) {
        ESP_LOGE(TAG, "mic_read: RX handle is NULL");
        if (read) *read = 0;
        return ESP_ERR_INVALID_STATE;
    }

    // Bypass esp_codec_dev_read — it wraps i2s_channel_read but has a bug
    // that causes it to return immediately without reading data.
    // Read directly from the I2S RX channel instead.
    size_t bytes_read = 0;
    size_t bytes_requested = samples * sizeof(int16_t);
    int ret = i2s_channel_read(rx, buffer, bytes_requested, &bytes_read, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_read failed: %s (got %d bytes)", esp_err_to_name(ret), (int)bytes_read);
        if (read) *read = 0;
        return ESP_FAIL;
    }

    size_t samples_read = bytes_read / sizeof(int16_t);
    if (read) *read = samples_read;
    return ESP_OK;
}
