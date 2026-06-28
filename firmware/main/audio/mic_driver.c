#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_codec_dev.h"
#include "app_config.h"
#include "es8311.h"

static const char *TAG = "MIC";

esp_err_t mic_read(int16_t *buffer, size_t samples, size_t *read)
{
    esp_codec_dev_handle_t h = es8311_get_record_handle();
    if (!h) {
        if (read) *read = 0;
        return ESP_ERR_INVALID_STATE;
    }

    int ret = esp_codec_dev_read(h, buffer, samples * sizeof(int16_t));
    if (ret < 0) {
        ESP_LOGE(TAG, "esp_codec_dev_read failed: %d", ret);
        if (read) *read = 0;
        return ESP_FAIL;
    }

    if (read) *read = ret / sizeof(int16_t);
    return ESP_OK;
}
