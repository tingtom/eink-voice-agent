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
    esp_codec_dev_handle_t h = es8311_get_record_handle();
    if (!h) {
        ESP_LOGE(TAG, "record handle is NULL");
        if (read) *read = 0;
        return ESP_ERR_INVALID_STATE;
    }

    // NOTE: esp_codec_dev_read returns a status code (0 = ESP_CODEC_DEV_OK),
    // NOT a byte count. On success the full buffer is filled by i2s_channel_read.
    // However, with a 1000ms timeout, partial reads are possible.
    size_t bytes_requested = samples * sizeof(int16_t);
    int ret = esp_codec_dev_read(h, buffer, bytes_requested);
    if (ret < 0) {
        ESP_LOGE(TAG, "esp_codec_dev_read failed: %d (%s)", ret, esp_err_to_name(ret));
        if (read) *read = 0;
        return ESP_FAIL;
    }

    // Success: assume full buffer filled (esp_codec_dev_read doesn't report bytes_read)
    if (read) *read = samples;
    return ESP_OK;
}

// Bypass esp_codec_dev to test I2S hardware directly.
// Call this from a diagnostic task or REPL to verify the I2S RX channel works.
esp_err_t mic_raw_read(int16_t *buffer, size_t samples, size_t *bytes_read)
{
    i2s_chan_handle_t rx = es8311_get_rx_handle();
    if (!rx) {
        ESP_LOGE(TAG, "raw_read: RX handle is NULL");
        if (bytes_read) *bytes_read = 0;
        return ESP_ERR_INVALID_STATE;
    }

    size_t br = 0;
    int ret = i2s_channel_read(rx, buffer, samples * sizeof(int16_t), &br, pdMS_TO_TICKS(1000));
    if (bytes_read) *bytes_read = br / sizeof(int16_t);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "raw i2s_channel_read failed: %s (got %d bytes)", esp_err_to_name(ret), (int)br);
        return ret;
    }
    ESP_LOGI(TAG, "raw_read: requested %d bytes, got %d bytes", (int)(samples * sizeof(int16_t)), (int)br);
    return ESP_OK;
}
